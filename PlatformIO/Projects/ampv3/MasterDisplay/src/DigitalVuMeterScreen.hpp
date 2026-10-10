#pragma once
#include "Screen.hpp"
#include "Config.hpp"
#include "BarPhysics.hpp"

// Horizontal bar-style VU meter(s): overall per-channel signal level (RMS
// of the raw waveform, same level source as AnalogVuScreen -- a single
// overall-level meter per channel, like a mixer channel strip, not a
// spectrum analyzer). STEREO: two horizontal bars stacked top/bottom, one
// per channel. MONO: one bar spanning nearly the full height, using the
// averaged (not summed) L+R signal, matching the "full screen" mono
// convention used by WaveformScreen.
class DigitalVuMeterScreen : public Screen {
public:
    void onEnter() override;
    void render() override;
    const char* name() const override { return "DIGITAL VU"; }

private:

    // Horizontal analogues of SpectrumScreen's fillGradientRangeUp/
    // updateBarColumnUp: same green/yellow/red zone logic and same
    // reclaim-one-unit-to-cover-a-pinned-peak-erase trick, just filling
    // left-to-right instead of bottom-to-top.
    void fillGradientRangeHorizontal(uint16_t y, uint16_t barHeight, uint16_t loWidth, uint16_t hiWidth, uint16_t maxBarWidth);
    void updateBarRow(uint16_t y, uint16_t barHeight, uint16_t oldWidth, uint16_t newWidth, uint16_t maxBarWidth);
    void processAndDrawBar(BarPhysicsState& state, float val, uint16_t y, uint16_t barHeight, uint16_t maxBarWidth);

    static void resetBarState(BarPhysicsState& state);

    BarPhysicsState m_left;
    BarPhysicsState m_right;
    BarPhysicsState m_mono;
};