#include "ColorTheme.hpp"
#include <cmath>

static_assert(static_cast<int>(ColorPreset::PUNK) == 3 && static_cast<int>(ColorPreset::USER1) == 4,
              "THEME_MIX_* are indexed by ColorPreset (the built-in palettes; the slots follow)");

ColorTheme& ColorTheme::instance() {
    static ColorTheme inst;
    return inst;
}

namespace {
// RGB565 <-> hue (degrees) / saturation / value, all in 0..1.
void toHsv(uint16_t c, float& h, float& s, float& v) {
    float r = ((c >> 11) & 0x1F) / 31.0f, g = ((c >> 5) & 0x3F) / 63.0f, b = (c & 0x1F) / 31.0f;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float d  = mx - mn;
    v = mx;
    s = mx > 0.0f ? d / mx : 0.0f;
    if (d <= 0.0f)    h = 0.0f;
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
    auto q = [](float f, int max) { int x = static_cast<int>(f * max + 0.5f); return x < 0 ? 0 : (x > max ? max : x); };
    return static_cast<uint16_t>((q(r + m, 31) << 11) | (q(g + m, 63) << 5) | q(b + m, 31));
}
} // namespace

ColorTheme::ColorTheme() {
    for (uint8_t i = 0; i < kBuiltinPalettes; ++i) {
        m_mixPos[i]  = static_cast<uint8_t>(THEME_MIX_POSITION[i] * 100 + 0.5f);
        m_mixGain[i] = static_cast<uint8_t>(THEME_MIX_BRIGHTNESS[i] * 100 + 0.5f);
    }
    for (uint8_t s = 0; s < kUserThemeSlots; ++s) {
        UserTheme& u = m_user[s];
        u.mid = 0x07FF;   // cyan
        u.spread = 60;
        u.style = static_cast<uint8_t>(GradientStyle::PER_BAR_FREQUENCY);
        deriveFromWheel(u);
        m_mixPos[kBuiltinPalettes + s]  = 50;
        m_mixGain[kBuiltinPalettes + s] = 100;
    }
}

void ColorTheme::deriveFromWheel(UserTheme& u) const {
    float h, s, v;
    toHsv(u.mid, h, s, v);
    u.low  = fromHsv(h - u.spread, s, v);
    u.high = fromHsv(h + u.spread, s, v);
}

// --- Any palette ---------------------------------------------------------------------

GradientStyle ColorTheme::style(ColorPreset p) const {
    if (isSlot(p)) return static_cast<GradientStyle>(m_user[slotOf(p)].style);
    return p == ColorPreset::CLASSIC ? GradientStyle::FLAT_ZONES : GradientStyle::PER_BAR_FREQUENCY;
}

uint16_t ColorTheme::low(ColorPreset p) const {
    if (isSlot(p)) return m_user[slotOf(p)].low;
    switch (p) {
        case ColorPreset::SUNSET: return 0xD01A;      // magenta
        case ColorPreset::AURORA: return 0x07E8;      // green
        case ColorPreset::PUNK:   return 0x181F;      // blue-violet
        default:                  return COLOR_GREEN; // Classic
    }
}

uint16_t ColorTheme::mid(ColorPreset p) const {
    if (isSlot(p)) return m_user[slotOf(p)].mid;
    switch (p) {
        case ColorPreset::SUNSET: return 0xFB40;       // orange
        case ColorPreset::AURORA: return 0x067F;       // cyan-azure
        case ColorPreset::PUNK:   return 0xF81C;       // pink-magenta
        default:                  return COLOR_YELLOW; // Classic
    }
}

uint16_t ColorTheme::high(ColorPreset p) const {
    if (isSlot(p)) return m_user[slotOf(p)].high;
    switch (p) {
        case ColorPreset::SUNSET: return 0xFF60;     // warm yellow
        case ColorPreset::AURORA: return 0xA01F;     // violet
        case ColorPreset::PUNK:   return 0xF8C0;     // red-orange
        default:                  return COLOR_RED;  // Classic
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

uint8_t ColorTheme::wheelSpread(ColorPreset p) const {
    if (isSlot(p)) return m_user[slotOf(p)].spread;
    return p == ColorPreset::PUNK ? 60 : 0;   // Punk: built from one colour, 60 degrees either side
}

uint16_t ColorTheme::interpolated(ColorPreset p, float t) const {
    t = constrain(t, 0.0f, 1.0f);
    if (uint8_t spread = wheelSpread(p)) {
        // A slot's own colour: around the colour wheel, not straight through
        // RGB -- between colours far apart a straight blend passes near grey;
        // this keeps every colour on the way as vivid as the chosen one.
        float h, s, v;
        toHsv(mid(p), h, s, v);
        return fromHsv(h + spread * (2.0f * t - 1.0f), s, v);
    }
    if (t <= 0.5f) return lerp565(low(p), mid(p), t / 0.5f);
    return lerp565(mid(p), high(p), (t - 0.5f) / 0.5f);
}

uint16_t ColorTheme::mix(ColorPreset p) const {
    uint16_t c = interpolated(p, mixPositionPct(p) / 100.0f);
    // Scale R, G, B together (in 0..1 of each channel's range), but never past
    // the point where the strongest one is full -- so the hue stays the same.
    float r = ((c >> 11) & 0x1F) / 31.0f, g = ((c >> 5) & 0x3F) / 63.0f, b = (c & 0x1F) / 31.0f;
    float peak = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (peak <= 0.0f) return c;
    float gain = mixGainPct(p) / 100.0f;
    if (peak * gain > 1.0f) gain = 1.0f / peak;
    if (gain == 1.0f) return c;   // unchanged, or already as bright as it can be
    auto ch = [gain](float x, int max) { int y = static_cast<int>(x * gain * max + 0.5f); return y > max ? max : y; };
    return static_cast<uint16_t>((ch(r, 31) << 11) | (ch(g, 63) << 5) | ch(b, 31));
}

uint16_t ColorTheme::mixGainLimitPct(ColorPreset p) const {
    uint16_t c = interpolated(p, mixPositionPct(p) / 100.0f);
    float r = ((c >> 11) & 0x1F) / 31.0f, g = ((c >> 5) & 0x3F) / 63.0f, b = (c & 0x1F) / 31.0f;
    float peak = r > g ? (r > b ? r : b) : (g > b ? g : b);
    return peak > 0.0f ? static_cast<uint16_t>(100.0f / peak) : 100;
}

uint16_t ColorTheme::colorForBarIndex(size_t barIndex, size_t totalBars) const {
    if (totalBars <= 1) return interpolatedColor(0.0f);
    float t = static_cast<float>(barIndex) / static_cast<float>(totalBars - 1);
    return interpolatedColor(t);
}

// --- The slots ------------------------------------------------------------------------

void ColorTheme::saveTo(ColorPreset src, uint8_t slot) {
    if (slot >= kUserThemeSlots) return;
    UserTheme t;
    t.used   = true;
    t.low    = low(src);
    t.mid    = mid(src);
    t.high   = high(src);
    t.style  = static_cast<uint8_t>(style(src));
    t.spread = wheelSpread(src);
    uint8_t pos = mixPositionPct(src), gain = mixGainPct(src);
    m_user[slot] = t;
    setMixPositionPct(slotPalette(slot), pos);
    setMixGainPct(slotPalette(slot), gain);
}

void ColorTheme::setSlotColor(uint8_t slot, uint16_t color) {
    if (slot >= kUserThemeSlots) return;
    UserTheme& u = m_user[slot];
    u.used   = true;
    u.mid    = color;
    if (!u.spread) u.spread = 60;
    u.style  = static_cast<uint8_t>(GradientStyle::PER_BAR_FREQUENCY);
    deriveFromWheel(u);
}

void ColorTheme::setSlotSpread(uint8_t slot, uint8_t spreadDeg) {
    if (slot >= kUserThemeSlots) return;
    UserTheme& u = m_user[slot];
    u.used   = true;
    u.spread = spreadDeg < 15 ? 15 : (spreadDeg > 120 ? 120 : spreadDeg);
    u.style  = static_cast<uint8_t>(GradientStyle::PER_BAR_FREQUENCY);
    deriveFromWheel(u);
}

void ColorTheme::restoreUserTheme(uint8_t slot, const UserTheme& t, uint8_t mixPos, uint8_t mixGain) {
    if (slot >= kUserThemeSlots) return;
    m_user[slot] = t;
    setMixPositionPct(slotPalette(slot), mixPos);
    setMixGainPct(slotPalette(slot), mixGain);
}
