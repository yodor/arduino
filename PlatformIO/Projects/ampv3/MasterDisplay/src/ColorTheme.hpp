#pragma once
#include "Config.hpp"
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
    NASTYFFT = 3, // blue -> purple -> red, ONE flat color per bar based on its FREQUENCY position (bar index), independent of level
    CUSTOM   = 4, // one chosen colour in the middle, bottom and top derived from it (setCustom()), one colour per bar by frequency
    USER1    = 5, // the five saved slots (Theme > Save Current): any look, as it was when saved
    USER2    = 6,
    USER3    = 7,
    USER4    = 8,
    USER5    = 9
};
constexpr uint8_t kUserThemeSlots = 5;

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

    // The theme's "mix" colour for single-colour uses (the mono waveform, the
    // clock in Theme colour): THEME_MIX_POSITION along the gradient, with the
    // hue-preserving THEME_MIX_BRIGHTNESS gain -- per theme (Config.hpp).
    uint16_t mixColor() const;

    // The mix settings, per theme (Theme > Mix Position / Mix Brightness),
    // in percent; they start from Config.hpp's THEME_MIX_* values.
    //   position: 0 = the bottom colour, 50 = the middle, 100 = the top
    //   gain:     100 = as is; hue-preserving, so it stops where the
    //             strongest channel is full (mixGainLimitPct())
    uint8_t mixPositionPct() const               { return m_mixPos[presetIndex()]; }
    uint8_t mixGainPct() const                   { return m_mixGain[presetIndex()]; }
    void    setMixPositionPct(uint8_t v)         { setMixPositionPct(m_preset, v); }
    void    setMixGainPct(uint8_t v)             { setMixGainPct(m_preset, v); }
    uint8_t mixPositionPct(ColorPreset p) const  { return m_mixPos[index(p)]; }
    uint8_t mixGainPct(ColorPreset p) const      { return m_mixGain[index(p)]; }
    void    setMixPositionPct(ColorPreset p, uint8_t v) { m_mixPos[index(p)]  = v > 100 ? 100 : v; }
    void    setMixGainPct(ColorPreset p, uint8_t v)     { m_mixGain[index(p)] = v < 10 ? 10 : (v > 250 ? 250 : v); }
    // The highest gain this theme's mix colour can take before its strongest
    // channel is full, in percent (100 = none).
    uint16_t mixGainLimitPct() const;

    // The Custom theme: `mid` is the chosen colour (the middle, and the mix);
    // the bottom and top sit `spreadDeg` degrees either side of it on the
    // colour wheel, at its saturation and brightness (Theme > Custom Color /
    // Custom Spread).
    void     setCustom(uint16_t mid, uint8_t spreadDeg);
    uint16_t customColor() const  { return m_customMid; }
    uint8_t  customSpread() const { return m_customSpread; }

    // The saved slots (Theme > Save Current, Slot 1..5): a snapshot of the
    // current look -- its three colours, how they're applied, whether they
    // blend around the colour wheel (a Custom look), and its mix settings.
    // Saving also switches to that slot. A slot never saved starts as the
    // default Custom look.
    struct UserTheme {
        bool     used   = false;
        uint16_t low = 0, mid = 0, high = 0;
        uint8_t  style  = 2;          // GradientStyle
        uint8_t  spread = 0;          // > 0: blend around the colour wheel, +- this many degrees
    };
    void saveCurrentTo(uint8_t slot);                    // 0..4
    const UserTheme& userTheme(uint8_t slot) const       { return m_user[slot < kUserThemeSlots ? slot : 0]; }
    // For the settings file: restore a slot as saved (mix settings included).
    void restoreUserTheme(uint8_t slot, const UserTheme& t, uint8_t mixPos, uint8_t mixGain);

    // For GradientStyle::PER_BAR_FREQUENCY presets: ONE flat color for the
    // entire bar at position barIndex out of totalBars (0 = lowest
    // frequency/leftmost, totalBars-1 = highest/rightmost). Reuses the same
    // low->mid->high stops and interpolation as interpolatedColor(), just
    // indexed by bar position instead of level.
    uint16_t colorForBarIndex(size_t barIndex, size_t totalBars) const;

private:
    static constexpr size_t kPresets = 5 + kUserThemeSlots;
    struct Trio { uint16_t low, mid, high; };
    Trio customColors() const;
    static size_t index(ColorPreset p) { size_t i = static_cast<size_t>(p); return i < kPresets ? i : 0; }
    size_t presetIndex() const         { return index(m_preset); }
    uint8_t m_mixPos[kPresets] = {
        static_cast<uint8_t>(THEME_MIX_POSITION[0] * 100 + 0.5f), static_cast<uint8_t>(THEME_MIX_POSITION[1] * 100 + 0.5f),
        static_cast<uint8_t>(THEME_MIX_POSITION[2] * 100 + 0.5f), static_cast<uint8_t>(THEME_MIX_POSITION[3] * 100 + 0.5f),
        static_cast<uint8_t>(THEME_MIX_POSITION[4] * 100 + 0.5f), 50, 50, 50, 50, 50 };
    uint8_t m_mixGain[kPresets] = {
        static_cast<uint8_t>(THEME_MIX_BRIGHTNESS[0] * 100 + 0.5f), static_cast<uint8_t>(THEME_MIX_BRIGHTNESS[1] * 100 + 0.5f),
        static_cast<uint8_t>(THEME_MIX_BRIGHTNESS[2] * 100 + 0.5f), static_cast<uint8_t>(THEME_MIX_BRIGHTNESS[3] * 100 + 0.5f),
        static_cast<uint8_t>(THEME_MIX_BRIGHTNESS[4] * 100 + 0.5f), 100, 100, 100, 100, 100 };
    // The Custom theme's colours (setCustom()); a teal default.
    uint16_t m_customLow = 0, m_customMid = 0, m_customHigh = 0;
    uint8_t  m_customSpread = 60;
    float    m_customHue = 180.0f, m_customSat = 1.0f, m_customVal = 1.0f;   // the chosen colour, as hue/saturation/value
    bool     m_customReady  = false;
    UserTheme m_user[kUserThemeSlots];
    // A slot in use: its index, or -1 for the built-in themes.
    int userIndex() const {
        int i = static_cast<int>(m_preset) - static_cast<int>(ColorPreset::USER1);
        return (i >= 0 && i < static_cast<int>(kUserThemeSlots)) ? i : -1;
    }
    const UserTheme& activeUser() const;   // the slot in use (a never-saved one: the default Custom look)

    ColorTheme() = default;
    ColorPreset m_preset = ColorPreset::CLASSIC;

    static uint16_t lerp565(uint16_t c0, uint16_t c1, float t);
};
