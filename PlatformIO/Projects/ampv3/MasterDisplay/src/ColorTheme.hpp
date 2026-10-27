#pragma once
#include <Arduino.h>

// Runtime-selectable color scheme for the low/mid/high level-gradient used
// by bar-style meters (SpectrumScreen, DigitalVuMeterScreen). Other
// screens' colors (waveform trace, analog VU needle/scale) aren't themed
// yet -- extending them the same way later is straightforward.
// The numbers are what the settings store: SUNSET and AURORA took the slots
// of the retired OCEAN and COLORFUL themes, so an old save maps to them.
enum class ColorPreset : uint8_t {
    CLASSIC  = 0, // green -> yellow -> red, flat zones (the original, default look)
    SUNSET   = 1, // magenta -> orange -> yellow, one colour per bar by its frequency (as NASTYFFT)
    AURORA   = 2, // green -> cyan -> violet, one colour per bar by its frequency (as NASTYFFT)
    NASTYFFT = 3  // blue -> purple -> red, ONE flat color per bar based on its FREQUENCY position (bar index), independent of level
};

// How a preset's low/mid/high colors get applied:
//  - FLAT_ZONES: 3 solid color bands based on a bar's LEVEL -- a pixel's
//    color depends only on which of 3 height zones it falls in.
//  - SMOOTH: continuous LEVEL-based interpolation across a bar's full
//    range, approximated with discrete bands (see GRADIENT_BAND_COUNT in
//    Config.hpp) for the same reason FLAT_ZONES uses zones rather than a
//    true per-pixel gradient -- bounding SPI transaction count during
//    large height changes.
//  - PER_BAR_FREQUENCY: color depends on which bar this is (its position
//    across the row, i.e. which frequency it represents), NOT on its
//    current level -- the whole bar renders as one flat color regardless
//    of height.
enum class GradientStyle : uint8_t {
    FLAT_ZONES,
    SMOOTH,
    PER_BAR_FREQUENCY
};

class ColorTheme {
public:
    static ColorTheme& instance();

    ColorPreset getPreset() const { return m_preset; }
    void setPreset(ColorPreset preset) { m_preset = preset; }

    GradientStyle getStyle() const;

    uint16_t lowColor() const;  // gradient's 0% stop (e.g. "green"/"blue")
    uint16_t midColor() const;  // gradient's 50% stop (e.g. "yellow"/"purple")
    uint16_t highColor() const; // gradient's 100% stop (e.g. "red")

    // For GradientStyle::SMOOTH presets: blends low->mid->high based on t
    // in [0,1] (0 = bottom of bar/quietest, 1 = top/loudest).
    uint16_t interpolatedColor(float t) const;

    // For GradientStyle::PER_BAR_FREQUENCY presets: ONE flat color for the
    // entire bar at position barIndex out of totalBars (0 = lowest
    // frequency/leftmost, totalBars-1 = highest/rightmost). Reuses the same
    // low->mid->high stops and interpolation as interpolatedColor(), just
    // indexed by bar position instead of level.
    uint16_t colorForBarIndex(size_t barIndex, size_t totalBars) const;

private:
    ColorTheme() = default;
    ColorPreset m_preset = ColorPreset::CLASSIC;

    static uint16_t lerp565(uint16_t c0, uint16_t c1, float t);
};
