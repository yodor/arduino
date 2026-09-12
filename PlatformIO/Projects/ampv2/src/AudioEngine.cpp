#include "AudioEngine.hpp"
#include <cmath>
#include <cstdlib>

namespace {
// Fully corrected 1/3-octave ISO band boundaries (Hz).
// Frequency boundaries are strictly separated by 1Hz to prevent dual-bin registration
// across adjacent RMS averaging loops, and the DC offset spectrum below 20Hz is omitted.
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

AudioEngine::AudioEngine() {
    // Left Channel Allocations
    m_fftCfgL  = kiss_fftr_alloc(FFT_SIZE, 0, nullptr, nullptr);
    m_fftInL   = static_cast<kiss_fft_scalar*>(malloc(sizeof(kiss_fft_scalar) * FFT_SIZE));
    m_fftOutL  = static_cast<kiss_fft_cpx*>(malloc(sizeof(kiss_fft_cpx) * (FFT_SIZE / 2 + 1)));
    m_fftBinsL = static_cast<float*>(malloc(sizeof(float) * (FFT_SIZE / 2)));

    // Right Channel Allocations
    m_fftCfgR  = kiss_fftr_alloc(FFT_SIZE, 0, nullptr, nullptr);
    m_fftInR   = static_cast<kiss_fft_scalar*>(malloc(sizeof(kiss_fft_scalar) * FFT_SIZE));
    m_fftOutR  = static_cast<kiss_fft_cpx*>(malloc(sizeof(kiss_fft_cpx) * (FFT_SIZE / 2 + 1)));
    m_fftBinsR = static_cast<float*>(malloc(sizeof(float) * (FFT_SIZE / 2)));
}

AudioEngine::~AudioEngine() {
    if (m_fftCfgL)  kiss_fftr_free(m_fftCfgL);
    if (m_fftInL)   free(m_fftInL);
    if (m_fftOutL)  free(m_fftOutL);
    if (m_fftBinsL) free(m_fftBinsL);

    if (m_fftCfgR)  kiss_fftr_free(m_fftCfgR);
    if (m_fftInR)   free(m_fftInR);
    if (m_fftOutR)  free(m_fftOutR);
    if (m_fftBinsR) free(m_fftBinsR);
}

AudioEngine& AudioEngine::instance() {
    static AudioEngine inst;
    return inst;
}

void AudioEngine::initHanningWindow() {
    for (int i = 0; i < FFT_SIZE; ++i) {
        m_hanningTable[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (FFT_SIZE - 1)));
    }
}

void AudioEngine::recomputeBinRanges() {
    // Bin count doubles per bar (1,2,4,8,...), skipping bins 0-1 (DC and
    // the bin needing different scaling).
    size_t startBin = 2;
    for (size_t bar = 0; bar < NUM_BARS; ++bar) {
        size_t binCount = 1u << bar; // 1, 2, 4, 8, 16, 32, 64, 128
        size_t endBin   = startBin + binCount;
        if (endBin > (FFT_SIZE / 2)) endBin = FFT_SIZE / 2;
        if (endBin <= startBin) endBin = startBin + 1;

        m_barStartBin[bar] = startBin;
        m_barEndBin[bar]   = endBin;

        startBin += binCount;
    }
}

void AudioEngine::recomputeOctaveBinRanges() {
    float binWidthHz = AUDIO_SAMPLE_RATE_HZ / static_cast<float>(FFT_SIZE);

    for (size_t band = 0; band < OCTAVE_BAND_COUNT; ++band) {
        size_t startBin = static_cast<size_t>(kOctaveBands[band].loHz / binWidthHz);
        size_t endBin   = static_cast<size_t>(kOctaveBands[band].hiHz / binWidthHz);

        // Skip bins 0-1 (DC and the bin needing different scaling), same
        // convention as recomputeBinRanges() above -- the lowest bands'
        // nominal Hz range floors to below this anyway given how coarse a
        // single bin is down there (see OCTAVE_BAND_COUNT's comment).
        if (startBin < 2) startBin = 2;
        if (endBin <= startBin) endBin = startBin + 1;
        if (endBin > (FFT_SIZE / 2)) endBin = FFT_SIZE / 2;

        m_octaveStartBin[band] = startBin;
        m_octaveEndBin[band]   = endBin;
    }
}

void AudioEngine::runCore1() {
    initHanningWindow();
    recomputeBinRanges();       // populate the 9-bar doubling-scheme bin-range lookup table before first use
    recomputeOctaveBinRanges(); // same, for OctaveScreen's fixed ISO-band table

    adc_init();
    adc_gpio_init(AUDIO_PIN_L); // ADC2 - GPIO28
    adc_gpio_init(AUDIO_PIN_R); // ADC1 - GPIO27

    m_adcDmaChan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(m_adcDmaChan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, DREQ_ADC);

    while (true) {
        uint8_t captureBufIdx = m_activeAudioBuf ^ 1;
        uint16_t* captureBuf = m_audioBuffer[captureBufIdx];

        // --- Hardware Phase Lock Fix ---
        adc_run(false);             // Stop ADC sampling during setup
        adc_fifo_drain();           // Flush residual samples from previous frame
        adc_select_input(2);        // Force starting input explicitly to ADC2 (Left)
        adc_set_round_robin(0x06);  // Alternates between ADC2 (L) and ADC1 (R)
        adc_fifo_setup(true, true, 1, false, false);
        adc_set_clkdiv(544.0f);     // ~44.1 kHz sampling per channel

        // Start DMA block capture
        dma_channel_configure(
            m_adcDmaChan, &c,
            captureBuf,
            &adc_hw->fifo,
            TOTAL_SAMPLES,
            true
        );

        adc_run(true);              // Start sampling cleanly on L channel

        dma_channel_wait_for_finish_blocking(m_adcDmaChan);
        adc_run(false);             // Pause ADC immediately after transfer completes
        m_activeAudioBuf = captureBufIdx;

        const uint16_t* rawSamples = m_audioBuffer[m_activeAudioBuf];

        // --- Diagnostic: raw ADC peak-to-peak, before ANY processing ---
        // (no DC removal, no windowing, nothing) -- the cleanest possible
        // checkpoint to tell whether cross-channel bleed exists at the
        // hardware/ADC level or gets introduced somewhere downstream in
        // our own code.
        static uint32_t s_lastRawDebugMs = 0;
        uint16_t rawMinL = 4095, rawMaxL = 0;
        uint16_t rawMinR = 4095, rawMaxR = 0;
        for (size_t i = 0; i < FFT_SIZE; ++i) {
            uint16_t vL = rawSamples[i * 2]     & 0x0FFF;
            uint16_t vR = rawSamples[i * 2 + 1] & 0x0FFF;
            if (vL < rawMinL) rawMinL = vL;
            if (vL > rawMaxL) rawMaxL = vL;
            if (vR < rawMinR) rawMinR = vR;
            if (vR > rawMaxR) rawMaxR = vR;
        }
        uint32_t rawDebugNowMs = millis();
        if (rawDebugNowMs - s_lastRawDebugMs >= 1000) {
            s_lastRawDebugMs = rawDebugNowMs;
            Serial.print("[RawADC] L pp=");
            Serial.print(rawMaxL - rawMinL);
            Serial.print(" (min=");
            Serial.print(rawMinL);
            Serial.print(" max=");
            Serial.print(rawMaxL);
            Serial.print(") | R pp=");
            Serial.print(rawMaxR - rawMinR);
            Serial.print(" (min=");
            Serial.print(rawMinR);
            Serial.print(" max=");
            Serial.print(rawMaxR);
            Serial.println(")");
        }

        // 1. Calculate precise per-channel dynamic DC offset
        float meanL = 0.0f;
        float meanR = 0.0f;
        for (size_t i = 0; i < FFT_SIZE; ++i) {
            meanL += static_cast<float>(rawSamples[i * 2] & 0x0FFF);
            meanR += static_cast<float>(rawSamples[i * 2 + 1] & 0x0FFF);
        }
        meanL /= static_cast<float>(FFT_SIZE);
        meanR /= static_cast<float>(FFT_SIZE);

        // 2. Separate interleaved channels and apply Hanning window
        for (size_t i = 0; i < FFT_SIZE; ++i) {
            float sampleL = static_cast<float>(rawSamples[i * 2] & 0x0FFF) - meanL;
            float sampleR = static_cast<float>(rawSamples[i * 2 + 1] & 0x0FFF) - meanR;

            m_fftInL[i] = (sampleL / 2048.0f) * m_hanningTable[i];
            m_fftInR[i] = (sampleR / 2048.0f) * m_hanningTable[i];

            // Stash the un-windowed, DC-removed sample for the waveform view,
            // gating out sub-threshold noise/crosstalk on a silent channel.
            int16_t rawL = static_cast<int16_t>(sampleL);
            int16_t rawR = static_cast<int16_t>(sampleR);
            m_waveL[i] = (abs(rawL) < WAVE_NOISE_GATE) ? 0 : rawL;
            m_waveR[i] = (abs(rawR) < WAVE_NOISE_GATE) ? 0 : rawR;
        }

        // 3. Execute dual Real FFT
        kiss_fftr(m_fftCfgL, m_fftInL, m_fftOutL);
        kiss_fftr(m_fftCfgR, m_fftInR, m_fftOutR);

        // 4. Calculate magnitude spectrum
        //
        // 2/N is the standard single-sided amplitude scale for a real FFT,
        // but it doesn't account for the Hanning window's coherent gain
        // (its average value, 0.5) attenuating the signal before the FFT
        // sees it. Without correcting for that, a genuine full-scale input
        // reads back as magnitude ~0.5 instead of ~1.0 -- about 6dB low --
        // so dbNormalize() (AudioLevel.hpp) never quite reaches 0 dBFS and
        // the bars never reach full height even when clipping.
        constexpr float kHannCoherentGain = 0.5f;
        constexpr float scale = 2.0f / (FFT_SIZE * kHannCoherentGain);
        for (size_t i = 0; i < FFT_SIZE / 2; ++i) {
            float realL = m_fftOutL[i].r;
            float imagL = m_fftOutL[i].i;
            m_fftBinsL[i] = sqrtf(realL * realL + imagL * imagL) * scale;

            float realR = m_fftOutR[i].r;
            float imagR = m_fftOutR[i].i;
            m_fftBinsR[i] = sqrtf(realR * realR + imagR * imagR) * scale;
        }

        // 5. Aggregate bars using PEAK bin magnitude, per the active
        // bin-mapping mode's precomputed [start,end) ranges.
        for (size_t bar = 0; bar < NUM_BARS; ++bar) {
            size_t startBin = m_barStartBin[bar];
            size_t endBin   = m_barEndBin[bar];

            float maxL = 0.0f;
            float maxR = 0.0f;

            for (size_t binIdx = startBin; binIdx < endBin; ++binIdx) {
                if (binIdx >= FFT_SIZE / 2) break;

                if (m_fftBinsL[binIdx] > maxL) maxL = m_fftBinsL[binIdx];
                if (m_fftBinsR[binIdx] > maxR) maxR = m_fftBinsR[binIdx];
            }

            // Apply noise gate to zero out low-level hardware noise (see
            // BAR_NOISE_GATE in Config.hpp for tuning notes).
            m_barsL[bar] = (maxL > BAR_NOISE_GATE) ? maxL : 0.0f;
            m_barsR[bar] = (maxR > BAR_NOISE_GATE) ? maxR : 0.0f;
        }

        // 6. Aggregate OctaveScreen's bands using RMS (average of magnitude
        // squared, then sqrt) rather than peak -- these bands can span far
        // more bins than a doubling-scheme bar (the top band alone covers
        // ~93 bins), so peak-bin would badly under-represent broadband
        // content spread across many bins, and averaging (not summing)
        // keeps a wide band from reading louder than a narrow one purely
        // from summing more terms.
        for (size_t band = 0; band < OCTAVE_BAND_COUNT; ++band) {
            size_t startBin = m_octaveStartBin[band];
            size_t endBin   = m_octaveEndBin[band];

            float sumSqL = 0.0f;
            float sumSqR = 0.0f;
            size_t count = 0;

            for (size_t binIdx = startBin; binIdx < endBin; ++binIdx) {
                if (binIdx >= FFT_SIZE / 2) break;

                sumSqL += m_fftBinsL[binIdx] * m_fftBinsL[binIdx];
                sumSqR += m_fftBinsR[binIdx] * m_fftBinsR[binIdx];
                ++count;
            }

            float rmsL = (count > 0) ? sqrtf(sumSqL / static_cast<float>(count)) : 0.0f;
            float rmsR = (count > 0) ? sqrtf(sumSqR / static_cast<float>(count)) : 0.0f;

            m_octaveBarsL[band] = (rmsL > BAR_NOISE_GATE) ? rmsL : 0.0f;
            m_octaveBarsR[band] = (rmsR > BAR_NOISE_GATE) ? rmsR : 0.0f;
        }
    }
}

void AudioEngine::getSpectrumBars(float* leftBars, float* rightBars) const {
    for (size_t i = 0; i < NUM_BARS; ++i) {
        if (leftBars)  leftBars[i]  = m_barsL[i];
        if (rightBars) rightBars[i] = m_barsR[i];
    }
}

void AudioEngine::getOctaveBars(float* leftBars, float* rightBars) const {
    for (size_t i = 0; i < OCTAVE_BAND_COUNT; ++i) {
        if (leftBars)  leftBars[i]  = m_octaveBarsL[i];
        if (rightBars) rightBars[i] = m_octaveBarsR[i];
    }
}

void AudioEngine::getWaveform(int16_t* leftWave, int16_t* rightWave, size_t count) const {
    if (!leftWave && !rightWave) return;
    if (count == 0) return;

    // Decimate the FFT_SIZE-sample capture buffer down to whatever the
    // caller (typically one sample per screen column) asked for.
    size_t limit  = (count < FFT_SIZE) ? count : FFT_SIZE;
    float  stride = static_cast<float>(FFT_SIZE) / static_cast<float>(limit);

    for (size_t i = 0; i < limit; ++i) {
        size_t srcIdx = static_cast<size_t>(i * stride);
        if (srcIdx >= FFT_SIZE) srcIdx = FFT_SIZE - 1;

        if (leftWave)  leftWave[i]  = m_waveL[srcIdx];
        if (rightWave) rightWave[i] = m_waveR[srcIdx];
    }

    // If more samples were requested than we have, pad the tail with the
    // last known value instead of leaving it uninitialized.
    for (size_t i = limit; i < count; ++i) {
        if (leftWave)  leftWave[i]  = leftWave[limit - 1];
        if (rightWave) rightWave[i] = rightWave[limit - 1];
    }
}

void AudioEngine::getFFTBins(float* dest, size_t count) const {
    if (!m_fftBinsL) return;
    size_t limit = (count < (FFT_SIZE / 2)) ? count : (FFT_SIZE / 2);
    for (size_t i = 0; i < limit; ++i) {
        dest[i] = m_fftBinsL[i];
    }
}