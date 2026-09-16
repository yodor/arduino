#pragma once
#include <Arduino.h>
#include "Screen.hpp"
#include "Config.hpp"
#include "BarPhysics.hpp"
#include "SpectrumPipeline.hpp" // for MAX_BAND_COUNT

// Merged spectrum screen: replaces the former separate BarSpectrumScreen
// (side-by-side layout) and OctaveScreen (top-bottom mirrored layout) with
// ONE screen supporting both, selected via DisplaySettings' StereoLayout
// setting. Both predecessors used identical per-bar smoothing/peak-hold/
// delta-redraw logic and differed only in geometry (baseline position and
// growth direction), so keeping them separate meant duplicating all of
// that just to vary a layout choice -- this screen expresses SIDE_BY_SIDE
// as "both channels grow up from the bottom edge" and TOP_BOTTOM as "left
// grows up from center, right grows down from center," both through the
// SAME baseline/growUp-parameterized drawing helpers, rather than
// maintaining two parallel sets of nearly-identical code.
//
// Band count and content come from AudioEngine::getSpectrumBars()/
// getSpectrumBandCount(), which vary based on SpectrumPipeline's own Band
// Layout / Energy Mode settings (also menu-selectable) -- this screen
// doesn't know or care which combination is active, it just draws however
// many bands it's given, up to MAX_BAND_COUNT.
class SpectrumScreen : public Screen {
public:
    void onEnter() override;
    void render() override;
    const char* name() const override { return "SPECTRUM"; }

private:
    // Fills [loHeight,hiHeight) for a bar growing UPWARD from `baseline`.
    // SIDE_BY_SIDE and MONO both use growUp=true with baseline=
    // SCREEN_HEIGHT (identical to the original BarSpectrumScreen's fixed-
    // baseline behavior); TOP_BOTTOM's left channel uses baseline=
    // SCREEN_HEIGHT/2 instead (identical to the original OctaveScreen).
    void fillGradientRangeUp(uint16_t x, uint16_t barWidth, uint16_t loHeight, uint16_t hiHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex, size_t totalBars);

    // Same, for a bar growing DOWNWARD from `baseline` -- only used by
    // TOP_BOTTOM's right channel.
    void fillGradientRangeDown(uint16_t x, uint16_t barWidth, uint16_t loHeight, uint16_t hiHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex, size_t totalBars);

    void updateBarColumnUp(uint16_t x, uint16_t barWidth, uint16_t oldHeight, uint16_t newHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex, size_t totalBars);
    void updateBarColumnDown(uint16_t x, uint16_t barWidth, uint16_t oldHeight, uint16_t newHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex, size_t totalBars);

    // Full per-bar pipeline (smoothing, peak-hold, delta redraw). growUp
    // selects direction/baseline -- see the two fillGradientRange* methods
    // above for how each layout/channel-mode combination maps onto this.
    void processAndDrawBar(BarPhysicsState& state, size_t i, size_t totalBars, float val, uint16_t x, uint16_t barWidth, uint16_t maxBarHeight, uint16_t baseline, bool growUp, uint32_t now);

    static void resetPrevHeights(BarPhysicsState* state);

    BarPhysicsState m_left[MAX_BAND_COUNT];
    BarPhysicsState m_right[MAX_BAND_COUNT];
    BarPhysicsState m_mono[MAX_BAND_COUNT];
};