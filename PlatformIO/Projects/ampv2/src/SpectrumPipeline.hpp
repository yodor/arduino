#pragma once
#include <Arduino.h>
#include "FftEngine.hpp"
#include "Config.hpp"

// Computes, at compile time, the largest number of doubling-scheme bars
// (1,2,4,8,... bins each, starting from bin 0) that fit within an FFT of
// the given size's usable bins (fftSize/2).
constexpr uint8_t computeMaxDoublingBars(uint16_t fftSize) {
    uint16_t usableBins = fftSize / 2;
    uint8_t  n           = 0;
    while (((1u << (n + 1)) - 1) <= usableBins) {
        ++n;
    }
    return n;
}

// Band count when BandLayoutMode::DOUBLING is active -- computed directly
// from FFT_SIZE rather than maintained as a separate literal that could
// drift out of sync with what the doubling scheme actually fits. An
// alternative logarithmic/linear-hybrid scheme was tried and compared
// numerically against this doubling scheme -- it produced nearly
// identical bin boundaries for most bars, with the only real difference
// being how the single top "catch-all" bar was bounded, so it wasn't
// worth the added code path and was removed.
constexpr uint8_t DOUBLING_BAND_COUNT = computeMaxDoublingBars(FFT_SIZE);

// Fully corrected 1/3-octave ISO band boundaries (Hz), used when
// BandLayoutMode::FIXED_BANDS is active. Lives here (not in the .cpp) so
// FIXED_BAND_COUNT below can be computed directly from its size.
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

// Band count when BandLayoutMode::FIXED_BANDS is active -- computed
// directly from the table above.
constexpr uint8_t FIXED_BAND_COUNT = sizeof(kOctaveBands) / sizeof(kOctaveBands[0]);

// Computes, at compile time, the largest band count that keeps a
// SIDE_BY_SIDE layout's bars at least 1px wide -- the binding constraint
// for BandLayoutMode::FULL, since that layout needs 2x the column count
// (separate L/R groups) with zero gap (Full mode's whole point is maximum
// density; TOP_BOTTOM/MONO end up with roughly double this bar width
// since they don't need the 2x split). Never exceeds the FFT's own actual
// usable bin count either, in case a much narrower screen or much smaller
// FFT_SIZE ever made THAT the binding constraint instead.
constexpr uint16_t computeMaxFullBands(uint16_t screenWidth, uint16_t usableBins) {
    uint16_t maxByWidth = screenWidth / 2;
    return (maxByWidth < usableBins) ? maxByWidth : usableBins;
}

// Band count when BandLayoutMode::FULL is active.
constexpr uint16_t FULL_BAND_COUNT = computeMaxFullBands(SCREEN_WIDTH, FFT_SIZE / 2);

// Largest of the three band counts above -- computed via a plain function
// (matching this file's established pattern) rather than an immediately-
// invoked lambda, since that would need C++17's relaxed constexpr lambda
// rules that this toolchain isn't confirmed to support.
constexpr uint16_t computeMaxBandCount(uint16_t a, uint16_t b, uint16_t c) {
    uint16_t m = a;
    if (b > m) m = b;
    if (c > m) m = c;
    return m;
}

// Array sizing for whichever mode is active -- internal arrays are sized
// to the largest of the three, with only the first bandCount() slots
// actually meaningful at any given moment.
constexpr uint16_t MAX_BAND_COUNT = computeMaxBandCount(DOUBLING_BAND_COUNT, FIXED_BAND_COUNT, FULL_BAND_COUNT);

// Merged spectrum pipeline: replaces the former separate Bar9Pipeline
// (doubling-scheme bands, peak aggregation) and OctavePipeline (fixed ISO
// bands, quadrature-sum aggregation) with ONE pipeline supporting THREE
// band-layout schemes (Doubling/Fixed Bands/Full) and BOTH aggregation
// methods, independently selectable at runtime via the menu (FFT Band
// Layout; FFT Energy Mode: Peak/Quadrature Sum).
//
// Mode changes are set from core0 (menu handling in main.cpp) but applied
// on core1 (inside pushChunk(), called from AudioEngine::runCore1()) --
// the mode fields are `volatile` for this reason. On this 32-bit ARM
// platform, single-word reads/writes like these are naturally atomic, so
// no locking is needed, but a mode change can take up to one pushChunk()
// call to actually apply, and getBars()/bandCount() may very briefly (a
// frame or two, right at the moment of switching) reflect a band count
// that doesn't yet match the just-applied layout's freshly-computed
// values. Acceptable given mode switches are rare, deliberate, user-
// initiated events, not a continuously-changing state.
class SpectrumPipeline {
public:
    enum class BandLayoutMode : uint8_t { DOUBLING, FIXED_BANDS, FULL };
    enum class EnergyMode : uint8_t { PEAK, QUADRATURE_SUM };

    SpectrumPipeline();

    // Feeds one physical capture chunk through this pipeline's FFT
    // engine; re-aggregates m_barsL/R (using whichever EnergyMode is
    // currently active) whenever the engine reports a fresh FFT ran.
    void pushChunk(const uint16_t* interleavedChunk);

    void getBars(float* leftBars, float* rightBars) const;

    // How many of getBars()'s output slots are actually valid right now --
    // DOUBLING_BAND_COUNT, FIXED_BAND_COUNT, or FULL_BAND_COUNT depending
    // on the active BandLayoutMode. Callers should never read past this
    // many entries.
    size_t bandCount() const { return m_activeBandCount; }

    void setBandLayoutMode(BandLayoutMode mode) { m_bandLayoutMode = mode; }
    void setEnergyMode(EnergyMode mode)         { m_energyMode = mode; }
    BandLayoutMode getBandLayoutMode() const    { return m_bandLayoutMode; }
    EnergyMode     getEnergyMode() const        { return m_energyMode; }

    uint32_t lastFftDurationUs() const { return m_engine.lastFftDurationUs(); }
    float    observedRefreshHz() const { return m_engine.observedRefreshHz(); }

private:
    void recomputeBinRanges();

    FftEngine m_engine;

    volatile BandLayoutMode m_bandLayoutMode = BandLayoutMode::DOUBLING;
    volatile EnergyMode     m_energyMode     = EnergyMode::PEAK;

    // What recomputeBinRanges() last actually computed FOR -- compared
    // against m_bandLayoutMode each pushChunk() call to detect a
    // core0-initiated mode change and recompute when one occurs.
    BandLayoutMode m_appliedBandLayoutMode = BandLayoutMode::DOUBLING;

    size_t m_activeBandCount = 0;

    size_t m_startBin[MAX_BAND_COUNT] = {};
    size_t m_endBin[MAX_BAND_COUNT]   = {};

    float m_barsL[MAX_BAND_COUNT] = {};
    float m_barsR[MAX_BAND_COUNT] = {};
};