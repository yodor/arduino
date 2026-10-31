#include "ColorTheme.hpp"
#include <cmath>
#include "Config.hpp"

ColorTheme& ColorTheme::instance() {
    static ColorTheme inst;
    return inst;
}

// --- The Custom theme ---------------------------------------------------------------

namespace {
// RGB565 <-> hue (degrees) / saturation / value, all channels in 0..1.
void toHsv(uint16_t c, float& h, float& s, float& v) {
    float r = ((c >> 11) & 0x1F) / 31.0f, g = ((c >> 5) & 0x3F) / 63.0f, b = (c & 0x1F) / 31.0f;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float d  = mx - mn;
    v = mx;
    s = mx > 0.0f ? d / mx : 0.0f;
    if (d <= 0.0f)   h = 0.0f;
    else if (mx == r) h = 60.0f * fmodf((g - b) / d + 6.0f, 6.0f);
    else if (mx == g) h = 60.0f * ((b - r) / d + 2.0f);
    else              h = 60.0f * ((r - g) / d + 4.0f);
}
uint16_t fromHsv(float h, float s, float v) {
    h = fmodf(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    float c = v * s, x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f)), m = v - c;
    float r = 0, g = 0, b = 0;
    if      (h < 60)  { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else              { r = c; b = x; }
    auto q = [](float f, int max) { int x = static_cast<int>((f) * max + 0.5f); return x < 0 ? 0 : (x > max ? max : x); };
    return static_cast<uint16_t>((q(r + m, 31) << 11) | (q(g + m, 63) << 5) | q(b + m, 31));
}
} // namespace

void ColorTheme::setCustom(uint16_t mid, uint8_t spreadDeg) {
    float h, s, v;
    toHsv(mid, h, s, v);
    m_customMid    = mid;
    m_customSpread = spreadDeg;
    m_customLow    = fromHsv(h - spreadDeg, s, v);
    m_customHigh   = fromHsv(h + spreadDeg, s, v);
    m_customHue = h; m_customSat = s; m_customVal = v;
    m_customReady  = true;
}

ColorTheme::Trio ColorTheme::customColors() const {
    if (!m_customReady) const_cast<ColorTheme*>(this)->setCustom(0x07FF /* cyan */, m_customSpread);
    return { m_customLow, m_customMid, m_customHigh };
}

const ColorTheme::UserTheme& ColorTheme::activeUser() const {
    static UserTheme fallback;
    int i = userIndex();
    if (i >= 0 && m_user[i].used) return m_user[i];
    // Never saved: the default Custom look (cyan, 60 degrees).
    Trio c = customColors();
    if (!fallback.used) { fallback.low = c.low; fallback.mid = c.mid; fallback.high = c.high; fallback.spread = 60; fallback.style = 2; }
    return fallback;
}

void ColorTheme::saveCurrentTo(uint8_t slot) {
    if (slot >= kUserThemeSlots) return;
    UserTheme t;
    t.used  = true;
    t.low   = lowColor();
    t.mid   = midColor();
    t.high  = highColor();
    t.style = static_cast<uint8_t>(getStyle());
    int ui  = userIndex();
    t.spread = (m_preset == ColorPreset::CUSTOM) ? m_customSpread : (ui >= 0 ? activeUser().spread : 0);
    uint8_t pos = mixPositionPct(), gain = mixGainPct();
    m_user[slot] = t;
    ColorPreset dst = static_cast<ColorPreset>(static_cast<uint8_t>(ColorPreset::USER1) + slot);
    setMixPositionPct(dst, pos);
    setMixGainPct(dst, gain);
    m_preset = dst;
}

void ColorTheme::restoreUserTheme(uint8_t slot, const UserTheme& t, uint8_t mixPos, uint8_t mixGain) {
    if (slot >= kUserThemeSlots) return;
    m_user[slot] = t;
    ColorPreset dst = static_cast<ColorPreset>(static_cast<uint8_t>(ColorPreset::USER1) + slot);
    setMixPositionPct(dst, mixPos);
    setMixGainPct(dst, mixGain);
}

GradientStyle ColorTheme::getStyle() const {
    if (userIndex() >= 0) return static_cast<GradientStyle>(activeUser().style);
    switch (m_preset) {
        case ColorPreset::SUNSET:
        case ColorPreset::AURORA:
        case ColorPreset::NASTYFFT:
        case ColorPreset::CUSTOM:   return GradientStyle::PER_BAR_FREQUENCY;
        default:                    return GradientStyle::FLAT_ZONES;
    }
}

uint16_t ColorTheme::lowColor() const {
    if (userIndex() >= 0) return activeUser().low;
    switch (m_preset) {
        case ColorPreset::SUNSET:   return 0xD01A; // magenta (slightly deep)
        case ColorPreset::AURORA:   return 0x07E8; // green
        case ColorPreset::NASTYFFT: return 0x001F; // blue
        case ColorPreset::CUSTOM:   return customColors().low;
        default:                    return COLOR_GREEN;
    }
}

uint16_t ColorTheme::midColor() const {
    if (userIndex() >= 0) return activeUser().mid;
    switch (m_preset) {
        case ColorPreset::SUNSET:   return 0xFB40; // orange
        case ColorPreset::AURORA:   return 0x067F; // cyan-azure
        case ColorPreset::NASTYFFT: return 0x780F; // purple
        case ColorPreset::CUSTOM:   return customColors().mid;
        default:                    return COLOR_YELLOW;
    }
}

uint16_t ColorTheme::highColor() const {
    if (userIndex() >= 0) return activeUser().high;
    switch (m_preset) {
        case ColorPreset::SUNSET:   return 0xFF60; // warm yellow
        case ColorPreset::AURORA:   return 0xA01F; // violet
        case ColorPreset::NASTYFFT: return COLOR_RED;
        case ColorPreset::CUSTOM:   return customColors().high;
        default:                    return COLOR_RED;
    }
}

uint16_t ColorTheme::lerp565(uint16_t c0, uint16_t c1, float t) {
    t = constrain(t, 0.0f, 1.0f);

    uint8_t r0 = (c0 >> 11) & 0x1F, g0 = (c0 >> 5) & 0x3F, b0 = c0 & 0x1F;
    uint8_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;

    uint8_t r = static_cast<uint8_t>(r0 + (r1 - r0) * t);
    uint8_t g = static_cast<uint8_t>(g0 + (g1 - g0) * t);
    uint8_t b = static_cast<uint8_t>(b0 + (b1 - b0) * t);

    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

uint16_t ColorTheme::interpolatedColor(float t) const {
    t = constrain(t, 0.0f, 1.0f);
    if (userIndex() >= 0 && activeUser().spread > 0) {
        // A slot saved from a Custom look: around the colour wheel, as Custom.
        const UserTheme& u = activeUser();
        float h, sat, val;
        toHsv(u.mid, h, sat, val);
        return fromHsv(h + u.spread * (2.0f * t - 1.0f), sat, val);
    }
    if (m_preset == ColorPreset::CUSTOM) {
        // Around the colour wheel, not straight through RGB: between colours
        // far apart (a wide spread) a straight blend passes near grey; this
        // keeps every colour on the way as vivid as the chosen one.
        customColors();   // make sure they're set up
        return fromHsv(m_customHue + m_customSpread * (2.0f * t - 1.0f), m_customSat, m_customVal);
    }
    if (t < 0.5f) {
        return lerp565(lowColor(), midColor(), t / 0.5f);
    }
    return lerp565(midColor(), highColor(), (t - 0.5f) / 0.5f);
}

static_assert(static_cast<int>(ColorPreset::CUSTOM) == 4 && static_cast<int>(ColorPreset::USER1) == 5,
              "THEME_MIX_* are indexed by ColorPreset (the five built-in themes; the slots follow)");

uint16_t ColorTheme::mixColor() const {
    uint16_t c = interpolatedColor(mixPositionPct() / 100.0f);
    // Scale R, G, B together (in 0..1 of each channel's range), but never past
    // the point where the strongest one is full -- so the hue stays the same.
    float r = ((c >> 11) & 0x1F) / 31.0f, g = ((c >> 5) & 0x3F) / 63.0f, b = (c & 0x1F) / 31.0f;
    float peak = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (peak <= 0.0f) return c;
    float gain = mixGainPct() / 100.0f;
    if (peak * gain > 1.0f) gain = 1.0f / peak;
    if (gain == 1.0f) return c;   // unchanged, or already as bright as it can be
    auto ch = [gain](float v, int max) { int x = static_cast<int>(v * gain * max + 0.5f); return x > max ? max : x; };
    return static_cast<uint16_t>((ch(r, 31) << 11) | (ch(g, 63) << 5) | ch(b, 31));
}

uint16_t ColorTheme::mixGainLimitPct() const {
    uint16_t c = interpolatedColor(mixPositionPct() / 100.0f);
    float r = ((c >> 11) & 0x1F) / 31.0f, g = ((c >> 5) & 0x3F) / 63.0f, b = (c & 0x1F) / 31.0f;
    float peak = r > g ? (r > b ? r : b) : (g > b ? g : b);
    return peak > 0.0f ? static_cast<uint16_t>(100.0f / peak) : 100;
}

uint16_t ColorTheme::colorForBarIndex(size_t barIndex, size_t totalBars) const {
    if (totalBars <= 1) return interpolatedColor(0.0f);
    float t = static_cast<float>(barIndex) / static_cast<float>(totalBars - 1);
    return interpolatedColor(t);
}
