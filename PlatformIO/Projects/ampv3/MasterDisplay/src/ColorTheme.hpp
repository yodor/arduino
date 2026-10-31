#pragma once
#include "Config.hpp"
#include <Arduino.h>

// The colour palettes (Theme > Palette): three built in, five of your own.
//   CLASSIC   green -> yellow -> red, flat zones by level (the original look)
//   SUNSET    magenta -> orange -> yellow, one colour per bar by frequency
//   AURORA    green -> cyan -> violet, one colour per bar by frequency
//   PUNK      blue-violet -> pink-magenta -> red-orange, around the colour wheel
//   USER1..5  the slots: a copy of any palette ("Save to Slot"), or a colour
//             of your own with bottom and top derived from it (Color /
//             Spread) -- a never-saved slot starts as cyan, 60 degrees.
enum class ColorPreset : uint8_t {
    CLASSIC = 0,
    SUNSET  = 1,
    AURORA  = 2,
    PUNK    = 3,
    USER1   = 4,
    USER2   = 5,
    USER3   = 6,
    USER4   = 7,
    USER5   = 8
};
constexpr uint8_t kBuiltinPalettes = 4;
constexpr uint8_t kUserThemeSlots  = 5;
constexpr uint8_t kPalettes        = kBuiltinPalettes + kUserThemeSlots;

enum class GradientStyle : uint8_t {
    FLAT_ZONES,
    SMOOTH,
    PER_BAR_FREQUENCY
};

class ColorTheme {
public:
    static ColorTheme& instance();

    // The palette in use (Theme > Palette > ... > Apply).
    ColorPreset getPreset() const          { return m_preset; }
    void        setPreset(ColorPreset p)   { m_preset = clampPalette(p); }

    // --- The palette in use: what the screens draw with ---
    GradientStyle getStyle() const         { return style(m_preset); }
    uint16_t lowColor() const              { return low(m_preset); }    // gradient's 0% stop
    uint16_t midColor() const              { return mid(m_preset); }    // gradient's 50% stop
    uint16_t highColor() const             { return high(m_preset); }   // gradient's 100% stop
    uint16_t interpolatedColor(float t) const { return interpolated(m_preset, t); }
    // The single-colour "mix" (the mono waveform, the clock in Theme colour).
    uint16_t mixColor() const              { return mix(m_preset); }
    uint16_t colorForBarIndex(size_t barIndex, size_t totalBars) const;

    // --- Any palette (the menu previews the one being edited) ---
    GradientStyle style(ColorPreset p) const;
    uint16_t low(ColorPreset p) const;
    uint16_t mid(ColorPreset p) const;
    uint16_t high(ColorPreset p) const;
    uint16_t interpolated(ColorPreset p, float t) const;
    uint16_t mix(ColorPreset p) const;

    // The mix settings, per palette, in percent: position 0 = bottom colour,
    // 50 = middle, 100 = top; gain 100 = as is, hue-preserving (it stops where
    // the strongest channel is full: mixGainLimitPct()).
    uint8_t  mixPositionPct(ColorPreset p) const        { return m_mixPos[index(p)]; }
    uint8_t  mixGainPct(ColorPreset p) const            { return m_mixGain[index(p)]; }
    void     setMixPositionPct(ColorPreset p, uint8_t v) { m_mixPos[index(p)]  = v > 100 ? 100 : v; }
    void     setMixGainPct(ColorPreset p, uint8_t v)     { m_mixGain[index(p)] = v < 10 ? 10 : (v > 250 ? 250 : v); }
    uint16_t mixGainLimitPct(ColorPreset p) const;

    // --- The slots ---
    struct UserTheme {
        bool     used   = false;       // saved or edited at least once
        uint16_t low = 0, mid = 0, high = 0;
        uint8_t  style  = 2;           // GradientStyle
        uint8_t  spread = 0;           // > 0: bottom/top derived from mid, +- this many degrees round the colour wheel
    };
    static bool        isSlot(ColorPreset p)        { return static_cast<uint8_t>(p) >= kBuiltinPalettes && static_cast<uint8_t>(p) < kPalettes; }
    static uint8_t     slotOf(ColorPreset p)        { return static_cast<uint8_t>(p) - kBuiltinPalettes; }
    static ColorPreset slotPalette(uint8_t slot)    { return static_cast<ColorPreset>(kBuiltinPalettes + (slot < kUserThemeSlots ? slot : 0)); }
    const UserTheme&   userTheme(uint8_t slot) const { return m_user[slot < kUserThemeSlots ? slot : 0]; }

    // Copy palette `src` (colours, style and mix settings) into a slot.
    void saveTo(ColorPreset src, uint8_t slot);
    // A slot's own colour: `color` in the middle, bottom and top `spreadDeg`
    // either side of it on the colour wheel (a copied palette becomes one the
    // first time either is changed). Spread 15..120.
    void     setSlotColor(uint8_t slot, uint16_t color);
    void     setSlotSpread(uint8_t slot, uint8_t spreadDeg);
    uint16_t slotColor(uint8_t slot) const          { return userTheme(slot).mid; }
    uint8_t  slotSpread(uint8_t slot) const         { uint8_t s = userTheme(slot).spread; return s ? s : 60; }
    // For the settings file: restore a slot as saved (mix settings included).
    void restoreUserTheme(uint8_t slot, const UserTheme& t, uint8_t mixPos, uint8_t mixGain);

private:
    ColorTheme();
    static ColorPreset clampPalette(ColorPreset p) { return static_cast<uint8_t>(p) < kPalettes ? p : ColorPreset::SUNSET; }
    static size_t      index(ColorPreset p)        { return static_cast<uint8_t>(clampPalette(p)); }
    static uint16_t    lerp565(uint16_t c0, uint16_t c1, float t);
    void               deriveFromWheel(UserTheme& u) const;   // low/high from mid and spread
    uint8_t            wheelSpread(ColorPreset p) const;      // > 0: blends around the colour wheel

    ColorPreset m_preset = ColorPreset::CLASSIC;
    UserTheme   m_user[kUserThemeSlots];
    uint8_t     m_mixPos[kPalettes]  = {};
    uint8_t     m_mixGain[kPalettes] = {};
};
