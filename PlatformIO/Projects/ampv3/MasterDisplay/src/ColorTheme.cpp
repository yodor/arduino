#include "ColorTheme.hpp"
#include "Config.hpp"

ColorTheme& ColorTheme::instance() {
    static ColorTheme inst;
    return inst;
}

GradientStyle ColorTheme::getStyle() const {
    switch (m_preset) {
        case ColorPreset::COLORFUL: return GradientStyle::SMOOTH;
        case ColorPreset::NASTYFFT: return GradientStyle::PER_BAR_FREQUENCY;
        default:                    return GradientStyle::FLAT_ZONES;
    }
}

uint16_t ColorTheme::lowColor() const {
    switch (m_preset) {
        case ColorPreset::OCEAN:    return 0x07FF; // cyan
        case ColorPreset::COLORFUL: return 0x001F; // blue
        case ColorPreset::NASTYFFT: return 0x001F; // blue
        default:                    return COLOR_GREEN;
    }
}

uint16_t ColorTheme::midColor() const {
    switch (m_preset) {
        case ColorPreset::OCEAN:    return 0x001F; // blue
        case ColorPreset::COLORFUL: return 0x780F; // purple
        case ColorPreset::NASTYFFT: return 0x780F; // purple
        default:                    return COLOR_YELLOW;
    }
}

uint16_t ColorTheme::highColor() const {
    switch (m_preset) {
        case ColorPreset::OCEAN:    return 0xF81F; // magenta
        case ColorPreset::COLORFUL: return COLOR_RED;
        case ColorPreset::NASTYFFT: return COLOR_RED;
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
    if (t < 0.5f) {
        return lerp565(lowColor(), midColor(), t / 0.5f);
    }
    return lerp565(midColor(), highColor(), (t - 0.5f) / 0.5f);
}

uint16_t ColorTheme::colorForBarIndex(size_t barIndex, size_t totalBars) const {
    if (totalBars <= 1) return interpolatedColor(0.0f);
    float t = static_cast<float>(barIndex) / static_cast<float>(totalBars - 1);
    return interpolatedColor(t);
}