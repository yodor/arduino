#pragma once
#include "Screen.hpp"
#include "Config.hpp"
#include "BarPhysics.hpp" // Ensure the unified struct definition is visible

// Spectrum bars. STEREO: NUM_BARS bars per channel, split left/right
// across the panel width (the original layout). MONO: NUM_BARS bars
// spanning the full width, using the averaged (not summed) L+R magnitude
// per bin, so a mono-duplicated stereo source reads at the same level as
// either channel alone rather than doubling. Same smoothing/decay/
// peak-hold ballistics and delta-only redraw logic either way -- both
// paths funnel through one shared per-bar processing routine.
class BarSpectrumScreen : public Screen {
public:
    void onEnter() override;
    void render() override;
    const char* name() const override { return "SPECTRUM"; }

private:


    // Fills only the strip of bar height in [loHeight, hiHeight), splitting
    // it into whichever green/yellow/red zones that strip actually crosses.
    void fillGradientRange(uint16_t x, uint16_t barWidth, uint16_t loHeight, uint16_t hiHeight, uint16_t maxBarHeight);

    // Redraws only the delta between last frame's bar height and this
    // frame's for one bar column. barIndex is only used when the active
    // ColorTheme style is PER_BAR_FREQUENCY (color depends on which bar
    // this is, not its height) -- ignored otherwise.
    void updateBarColumn(uint16_t x, uint16_t barWidth, uint16_t oldHeight, uint16_t newHeight, uint16_t maxBarHeight, size_t barIndex);

    // Full per-bar pipeline (smoothing, peak-hold, delta redraw) for one
    // bar in one channel-state set. val is already dB-normalized (0..1).
    void processAndDrawBar(BarPhysicsState& state, size_t i, float val, uint16_t x, uint16_t barWidth, uint16_t maxBarHeight, uint32_t now);

    static void resetPrevHeights(BarPhysicsState* state);

    // Upgraded array states driven by the standardized engine struct
    BarPhysicsState m_left[NUM_BARS];
    BarPhysicsState m_right[NUM_BARS];
    BarPhysicsState m_mono[NUM_BARS];
};