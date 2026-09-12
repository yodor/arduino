#pragma once

#include "Config.hpp"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "../lib/kissfft/kiss_fftr.h"

class AudioEngine {
public:
    static AudioEngine& instance();

    void runCore1();
    void getFFTBins(float* dest, size_t count) const;
    void getSpectrumBars(float* leftBars, float* rightBars) const;
    void getOctaveBars(float* leftBars, float* rightBars) const;
    void getWaveform(int16_t* leftWave, int16_t* rightWave, size_t count) const;

private:
    AudioEngine();
    ~AudioEngine();

    void initHanningWindow();
    void recomputeBinRanges();
    void recomputeOctaveBinRanges();

    uint16_t         m_audioBuffer[2][TOTAL_SAMPLES] = {};
    volatile uint8_t m_activeAudioBuf = 0;
    int              m_adcDmaChan = -1;

    // Left Channel FFT
    kiss_fftr_cfg    m_fftCfgL = nullptr;
    kiss_fft_scalar* m_fftInL  = nullptr;
    kiss_fft_cpx*    m_fftOutL = nullptr;

    // Right Channel FFT
    kiss_fftr_cfg    m_fftCfgR = nullptr;
    kiss_fft_scalar* m_fftInR  = nullptr;
    kiss_fft_cpx*    m_fftOutR = nullptr;

    float*           m_fftBinsL = nullptr;
    float*           m_fftBinsR = nullptr;
    float            m_hanningTable[FFT_SIZE] = {};

    // Raw, DC-removed (but not windowed) samples from the most recent capture,
    // kept around purely so the UI can render an oscilloscope-style waveform.
    int16_t          m_waveL[FFT_SIZE] = {};
    int16_t          m_waveR[FFT_SIZE] = {};

    mutable float    m_barsL[NUM_BARS] = {};
    mutable float    m_barsR[NUM_BARS] = {};

    // Precomputed [start, end) FFT bin range for each bar (bin count
    // doubles per bar: 1,2,4,8,...). Computed once by recomputeBinRanges()
    // rather than every audio frame, since bin boundaries never change
    // after that.
    size_t           m_barStartBin[NUM_BARS] = {};
    size_t           m_barEndBin[NUM_BARS]   = {};

    // Same idea as m_barStartBin/m_barEndBin above, but for OctaveScreen's
    // fixed 1/3-octave ISO bands -- a separate table since the band
    // boundaries and count are unrelated to the 9-bar doubling scheme.
    // Aggregated via RMS (not peak, see recomputeOctaveBinRanges()'s
    // companion aggregation code in runCore1()) since these bands can span
    // many more bins than a doubling-scheme bar.
    mutable float    m_octaveBarsL[OCTAVE_BAND_COUNT] = {};
    mutable float    m_octaveBarsR[OCTAVE_BAND_COUNT] = {};
    size_t           m_octaveStartBin[OCTAVE_BAND_COUNT] = {};
    size_t           m_octaveEndBin[OCTAVE_BAND_COUNT]   = {};
};