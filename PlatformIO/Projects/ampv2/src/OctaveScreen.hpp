#pragma once
#include "Screen.hpp"
#include "Config.hpp"
#include "BarPhysics.hpp"

// 1/3-octave real-time analyzer: OCTAVE_BAND_COUNT (31) fixed ISO bands
// spanning the full screen width. STEREO: bars split top/bottom around the
// screen's vertical center -- LEFT channel grows UPWARD from center toward
// the top edge, RIGHT channel grows DOWNWARD from center toward the bottom
// edge, so both channels' bars meet at a shared centerline instead of
// being side-by-side like BarSpectrumScreen. MONO: falls back to a single
// row of standard bottom-up bars spanning the full height, using the
// averaged (not summed) L+R magnitude, matching every other screen's mono
// convention -- the "mirror around center" concept has no meaning with
// only one channel's data.
//
// Aggregation is RMS-based (not peak, see AudioEngine::getOctaveBars()),
// since these bands can span far more FFT bins than a BarSpectrumScreen
// bar. Same ColorTheme integration (FLAT_ZONES/SMOOTH/PER_BAR_FREQUENCY)
// as BarSpectrumScreen, just applied to differently-shaped bars.
class OctaveScreen : public Screen {
public:
    void onEnter() override;
    void render() override;
    const char* name() const override { return "OCTAVE"; }

private:


    // Fills [loHeight,hiHeight) for a bar growing UPWARD from `baseline`
    // (top edge at baseline-height) -- same zone/band/flat-color logic as
    // BarSpectrumScreen::fillGradientRange, just parameterized by an
    // explicit baseline instead of hardcoded SCREEN_HEIGHT.
    void fillGradientRangeUp(uint16_t x, uint16_t barWidth, uint16_t loHeight, uint16_t hiHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex);

    // Same, for a bar growing DOWNWARD from `baseline` (top edge fixed at
    // baseline, bottom edge at baseline+height).
    void fillGradientRangeDown(uint16_t x, uint16_t barWidth, uint16_t loHeight, uint16_t hiHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex);

    void updateBarColumnUp(uint16_t x, uint16_t barWidth, uint16_t oldHeight, uint16_t newHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex);
    void updateBarColumnDown(uint16_t x, uint16_t barWidth, uint16_t oldHeight, uint16_t newHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex);

    // Full per-bar pipeline (smoothing, peak-hold, delta redraw). growUp
    // selects direction/baseline: true grows upward from SCREEN_HEIGHT/2
    // (left channel in stereo), false grows downward from SCREEN_HEIGHT/2
    // (right channel in stereo) or upward from SCREEN_HEIGHT (mono,
    // standard bottom-up bars using the full height as baseline).
    void processAndDrawBar(BarPhysicsState& state, size_t i, float val, uint16_t x, uint16_t barWidth, uint16_t maxBarHeight, uint16_t baseline, bool growUp, uint32_t now);

    static void resetPrevHeights(BarPhysicsState* state);

    BarPhysicsState m_left[OCTAVE_BAND_COUNT];
    BarPhysicsState m_right[OCTAVE_BAND_COUNT];
    BarPhysicsState m_mono[OCTAVE_BAND_COUNT];
};