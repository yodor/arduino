#pragma once
#include "Screen.hpp"
#include "Config.hpp"

// Oscilloscope-style waveform. STEREO: left channel top half, right
// channel bottom half (the original layout). MONO: one trace spanning the
// full height, using the averaged (not summed) L+R sample so a
// mono-duplicated stereo source reads at the same amplitude as either
// channel alone. Same chunked delta-redraw and run-merging either way.
class WaveformScreen : public Screen {
public:
    void onEnter() override;
    void render() override;
    const char* name() const override { return "WAVEFORM"; }

private:
    void renderStereo(const int16_t* leftWave, const int16_t* rightWave, size_t sampleCount);
    void renderMono(const int16_t* leftWave, const int16_t* rightWave, size_t sampleCount);

    // Draws a polyline through y[0..count-1] over the sample-index range
    // [rangeStart, rangeEnd), merging consecutive samples that share the
    // same Y value into a single drawLine call within that range.
    void drawWavePolylineRange(const int16_t* y, size_t count, size_t rangeStart, size_t rangeEndExclusive, uint16_t color);

    // Previous/current frame trace, stereo.
    int16_t m_prevWaveYL[WAVEFORM_SAMPLES] = {0};
    int16_t m_prevWaveYR[WAVEFORM_SAMPLES] = {0};
    int16_t m_curWaveYL[WAVEFORM_SAMPLES]  = {0};
    int16_t m_curWaveYR[WAVEFORM_SAMPLES]  = {0};

    // Previous/current frame trace, mono. Kept separate from the stereo
    // arrays so switching channel modes never mixes up trace history.
    int16_t m_prevWaveYMono[WAVEFORM_SAMPLES] = {0};
    int16_t m_curWaveYMono[WAVEFORM_SAMPLES]  = {0};

    bool m_waveTraceValid = false;
};