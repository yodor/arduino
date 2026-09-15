#include "OctavePipeline.hpp"
#include <cmath>

namespace {
// Fully corrected 1/3-octave ISO band boundaries (Hz).
// Frequency boundaries are strictly separated by 1Hz to prevent dual-bin
// registration across adjacent aggregation loops, and the DC offset
// spectrum below 20Hz is omitted.
struct OctaveBand { float loHz; float hiHz; };
constexpr OctaveBand kOctaveBands[] = {
    {20,    25},    {26,    31},    {32,    40},    {41,    50},    // Sub-Bass
    {51,    63},    {64,    80},    {81,    100},   {101,   125},   // Bass
    {126,   160},   {161,   200},   {201,   250},   {251,   315},   // Low-Mids
    {316,   400},   {401,   500},   {501,   630},   {631,   800},   // Midrange
    {801,   1000},  {1001,  1250},  {1251,  1600},  {1601,  2000},  // Upper-Mids
    {2001,  2500},  {2501,  3150},  {3151,  4000},  {4001,  5000},  // Presence
    {5001,  6300},  {6301,  8000},  {8001,  10000}, {10001, 12500}, // Brilliance
    {12501, 14000}, {14001, 16000}, {16001, 20000}                  // Air / Ultra-Highs
};

static_assert(sizeof(kOctaveBands) / sizeof(kOctaveBands[0]) == OCTAVE_BAND_COUNT,
              "kOctaveBands and OCTAVE_BAND_COUNT (Config.hpp) must agree");
} // namespace

OctavePipeline::OctavePipeline() : m_engine(OCTAVE_FFT_SIZE) {
    recomputeBinRanges();
}

void OctavePipeline::recomputeBinRanges() {
    float binWidthHz = AUDIO_SAMPLE_RATE_HZ / static_cast<float>(OCTAVE_FFT_SIZE);

    for (size_t band = 0; band < OCTAVE_BAND_COUNT; ++band) {
        size_t startBin = static_cast<size_t>(kOctaveBands[band].loHz / binWidthHz);
        size_t endBin   = static_cast<size_t>(kOctaveBands[band].hiHz / binWidthHz);

        if (startBin < 0) startBin = 0;
        if (endBin <= startBin) endBin = startBin + 1;
        if (endBin > (OCTAVE_FFT_SIZE / 2)) endBin = OCTAVE_FFT_SIZE / 2;

        m_octaveStartBin[band] = startBin;
        m_octaveEndBin[band]   = endBin;
    }
}

void OctavePipeline::pushChunk(const uint16_t* interleavedChunk) {
    if (!m_engine.pushChunk(interleavedChunk)) return;

    const float* binsL    = m_engine.magnitudeBinsL();
    const float* binsR    = m_engine.magnitudeBinsR();
    size_t       binCount = m_engine.binCount();

    // Aggregate using the quadrature sum of bin magnitudes (sqrt(sum of
    // magnitude^2) -- proportional to the band's total energy via
    // Parseval's theorem), NOT an average/RMS. A swept-tone test at
    // constant level showed bar height dropping toward higher frequencies
    // with an averaged approach: a tone's energy always lands in the same
    // handful of bins regardless of frequency, but octave bands get wider
    // in bin count as frequency rises, so dividing by bin count was
    // shrinking a fixed amount of real energy purely because of which
    // band it happened to land in.
    for (size_t band = 0; band < OCTAVE_BAND_COUNT; ++band) {
        size_t startBin = m_octaveStartBin[band];
        size_t endBin   = m_octaveEndBin[band];

        float sumSqL = 0.0f, sumSqR = 0.0f;
        for (size_t binIdx = startBin; binIdx < endBin; ++binIdx) {
            if (binIdx >= binCount) break;
            sumSqL += binsL[binIdx] * binsL[binIdx];
            sumSqR += binsR[binIdx] * binsR[binIdx];
        }

        float bandEnergyL = sqrtf(sumSqL);
        float bandEnergyR = sqrtf(sumSqR);

        // Own noise gate (OCTAVE_NOISE_GATE, not BAR_NOISE_GATE) since
        // this sum scales with bin count the same way real signal does --
        // a wide band's noise floor is genuinely higher than a narrow
        // band's, and needs its own calibrated threshold.
        m_octaveBarsL[band] = (bandEnergyL > OCTAVE_NOISE_GATE) ? bandEnergyL : 0.0f;
        m_octaveBarsR[band] = (bandEnergyR > OCTAVE_NOISE_GATE) ? bandEnergyR : 0.0f;
    }
}

void OctavePipeline::getBars(float* leftBars, float* rightBars) const {
    for (size_t i = 0; i < OCTAVE_BAND_COUNT; ++i) {
        if (leftBars)  leftBars[i]  = m_octaveBarsL[i];
        if (rightBars) rightBars[i] = m_octaveBarsR[i];
    }
}