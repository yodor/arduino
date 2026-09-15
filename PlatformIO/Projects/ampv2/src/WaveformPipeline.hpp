#pragma once
#include "Config.hpp"

// Raw waveform sliding window -- deliberately NOT built on FftEngine,
// since there's no spectral processing here at all. Refreshed on every
// single physical capture chunk for maximum responsiveness, genuinely
// independent of either FFT pipeline's size or cadence.
class WaveformPipeline {
public:
    void pushChunk(const uint16_t* interleavedChunk);
    void getWaveform(int16_t* leftWave, int16_t* rightWave, size_t count) const;

private:
    uint16_t m_slideL[WAVEFORM_WINDOW_SAMPLES] = {};
    uint16_t m_slideR[WAVEFORM_WINDOW_SAMPLES] = {};

    // Raw, DC-removed (but not windowed) samples from the current
    // waveform window, kept around purely so the UI can render an
    // oscilloscope-style waveform.
    int16_t m_waveL[WAVEFORM_WINDOW_SAMPLES] = {};
    int16_t m_waveR[WAVEFORM_WINDOW_SAMPLES] = {};
};