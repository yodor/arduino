#include "ClockScreen.hpp"
#include "Renderer.hpp"
#include "DisplaySettings.hpp"
#include <cstdio>
#include <cstring>
#include <cctype>

namespace {
// Segment bits: a=top, b=top-right, c=bottom-right, d=bottom, e=bottom-left,
// f=top-left, g=middle.
enum : uint8_t { SEG_A = 0, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F, SEG_G };
constexpr uint8_t kDigitMask[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};
constexpr uint8_t kMaskBlank = 0x00;
constexpr uint8_t kMaskDash  = 0x40; // just the middle segment

constexpr int16_t  kMargin    = 8;   // left/right
constexpr int16_t  kMarginV   = 4;   // top/bottom -- the digits use (nearly) the full height
constexpr int16_t  kDateGap   = 12;  // between the digits and the date column
constexpr int16_t  kDateRowGap = 8;  // between the weekday and the day number
constexpr uint16_t kColorWeekday = 0xC618; // light gray
constexpr uint8_t  kSmallSize = 2;   // date line / AM-PM text: 12x16 px characters
constexpr int16_t  kSmallH    = 16;
constexpr int16_t  kAmPmW     = 2 * 6 * kSmallSize + 4;

const char* const kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// The same hue at about 1/6 brightness -- the unlit "ghost" segments.
uint16_t dim565(uint16_t c) {
    uint16_t r = ((c >> 11) & 0x1F) / 6;
    uint16_t g = ((c >> 5)  & 0x3F) / 6;
    uint16_t b = ( c        & 0x1F) / 6;
    uint16_t out = static_cast<uint16_t>((r << 11) | (g << 5) | b);
    return out ? out : 0x0841; // never fully black, or the ghost would vanish
}
} // namespace

void ClockScreen::layout() {
    const DisplaySettings& ds = DisplaySettings::instance();
    m_mode = ds.getClockMode();
    m_24h  = ds.getClock24h();

    bool seconds = (m_mode == ClockMode::SECONDS);
    m_digitCount = seconds ? 6 : 4;
    m_colonCount = seconds ? 2 : 1;
    uint8_t pairs = m_digitCount / 2;

    // The digits always get the full height; only the width is shared.
    const int16_t availH = SCREEN_HEIGHT - 2 * kMarginV;
    auto widthFor = [&](float hh) {
        float w = hh * 0.52f, seg = w * 0.17f, gap = w * 0.16f;
        return m_digitCount * w + pairs * gap + m_colonCount * seg * 3.0f;
    };
    auto heightFitting = [&](int16_t maxW) {
        float h  = static_cast<float>(availH);
        float tw = widthFor(h);
        if (tw > maxW) h *= static_cast<float>(maxW) / tw;
        return h;
    };

    int16_t ampm   = m_24h ? 0 : kAmPmW;
    int16_t maxW   = SCREEN_WIDTH - 2 * kMargin - ampm;
    m_wdaySize = m_daySize = 0;
    m_dateW    = 0;
    if (m_mode == ClockMode::DATE) {
        // The biggest weekday text (3 characters) whose column still leaves
        // the digits ~85% of the full height; the day number (2 characters)
        // as big as that column and the remaining height allow. +1 px: the
        // bold second pass.
        for (uint8_t sw = 5; sw >= 2; --sw) {
            int16_t colW = 3 * 6 * sw + 1;
            float h = heightFitting(maxW - colW - kDateGap);
            if (h >= availH * 0.85f || sw == 2) {
                uint8_t byWidth  = static_cast<uint8_t>((colW - 1) / 12);
                uint8_t byHeight = static_cast<uint8_t>((availH - 8 * sw - kDateRowGap) / 8);
                m_wdaySize = sw;
                m_daySize  = byWidth < byHeight ? byWidth : byHeight;
                if (m_daySize > 8) m_daySize = 8;
                m_dateW    = colW;
                break;
            }
        }
        maxW -= m_dateW + kDateGap;
    }

    float h  = heightFitting(maxW);
    m_digitH = static_cast<int16_t>(h);
    m_digitW = static_cast<int16_t>(h * 0.52f);
    m_seg    = static_cast<int16_t>(m_digitW * 0.17f);
    if (m_seg < 3) m_seg = 3;
    int16_t gap    = static_cast<int16_t>(m_digitW * 0.16f);
    int16_t colonW = m_seg * 3;
    int16_t total  = m_digitCount * m_digitW + pairs * gap + m_colonCount * colonW;

    // Digits centred in what's left of the width (and AM/PM after them).
    int16_t areaW = SCREEN_WIDTH - 2 * kMargin - (m_dateW ? m_dateW + kDateGap : 0);
    int16_t x     = kMargin + (areaW - total - ampm) / 2;
    m_digitY      = kMarginV + (availH - m_digitH) / 2;

    for (uint8_t p = 0; p < pairs; ++p) {
        m_digitX[2 * p] = x;
        x += m_digitW + gap;
        m_digitX[2 * p + 1] = x;
        x += m_digitW;
        if (p < pairs - 1) {
            m_colonX[p] = x + colonW / 2;
            x += colonW;
        }
    }

    if (m_dateW) {
        int16_t blockH = 8 * m_wdaySize + kDateRowGap + 8 * m_daySize;
        m_dateX   = SCREEN_WIDTH - kMargin - m_dateW;
        m_dateTop = kMarginV + (availH - blockH) / 2;
        m_dayY    = m_dateTop + 8 * m_wdaySize + kDateRowGap;
    }

    m_onColor    = ds.getClockColor();
    m_ghostColor = dim565(m_onColor);
}

// One hexagonal segment: a rectangle with pointed ends, so neighbouring
// segments meet diagonally at the corners like a real seven-segment digit.
void ClockScreen::drawSegment(int16_t x, int16_t y, uint8_t seg, uint16_t color) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    const int16_t W = m_digitW, H = m_digitH, T = m_seg;
    const int16_t g    = (T / 6 > 1) ? T / 6 : 1; // hairline gap between segments
    const int16_t half = T / 2;

    auto horizontal = [&](int16_t yc) {
        int16_t xa = x + half + g, xb = x + W - 1 - half - g;
        int16_t y0 = yc - half, y1 = y0 + T - 1, ym = (y0 + y1) / 2;
        int16_t x0 = xa + half, x1 = xb - half;
        if (x1 >= x0) gfx->fillRect(x0, y0, x1 - x0 + 1, T, color);
        gfx->fillTriangle(xa, ym, x0, y0, x0, y1, color);
        gfx->fillTriangle(xb, ym, x1, y0, x1, y1, color);
    };
    auto vertical = [&](int16_t xc, int16_t ya, int16_t yb) {
        int16_t x0 = xc - half, x1 = x0 + T - 1, xm = (x0 + x1) / 2;
        int16_t y0 = ya + half, y1 = yb - half;
        if (y1 >= y0) gfx->fillRect(x0, y0, T, y1 - y0 + 1, color);
        gfx->fillTriangle(xm, ya, x0, y0, x1, y0, color);
        gfx->fillTriangle(xm, yb, x0, y1, x1, y1, color);
    };

    const int16_t top = y + half, mid = y + H / 2, bot = y + H - 1 - half;
    const int16_t left = x + half, right = x + W - 1 - half;
    switch (seg) {
        case SEG_A: horizontal(top); break;
        case SEG_G: horizontal(mid); break;
        case SEG_D: horizontal(bot); break;
        case SEG_F: vertical(left,  top + g, mid - g); break;
        case SEG_B: vertical(right, top + g, mid - g); break;
        case SEG_E: vertical(left,  mid + g, bot - g); break;
        case SEG_C: vertical(right, mid + g, bot - g); break;
        default: break;
    }
}

void ClockScreen::drawDigit(uint8_t index, uint8_t newMask, bool force) {
    uint8_t oldMask = m_drawnMask[index];
    for (uint8_t seg = 0; seg < 7; ++seg) {
        bool on  = newMask & (1u << seg);
        bool was = oldMask & (1u << seg);
        if (force || on != was) {
            drawSegment(m_digitX[index], m_digitY, seg, on ? m_onColor : m_ghostColor);
        }
    }
    m_drawnMask[index] = newMask;
}

void ClockScreen::drawColons(bool on, bool force) {
    if (!force && static_cast<int8_t>(on) == m_drawnColon) return;
    Arduino_GFX* gfx = Renderer::instance().gfx();
    uint16_t color = on ? m_onColor : m_ghostColor;
    int16_t  T = m_seg;
    for (uint8_t i = 0; i < m_colonCount; ++i) {
        int16_t x = m_colonX[i] - T / 2;
        gfx->fillRect(x, m_digitY + m_digitH / 3 - T / 2,       T, T, color);
        gfx->fillRect(x, m_digitY + (2 * m_digitH) / 3 - T / 2, T, T, color);
    }
    m_drawnColon = static_cast<int8_t>(on);
}

// DATE mode: the weekday over the day of the month in the right-hand
// column, in white (set apart from the clock colour) and bold -- each line is
// printed twice, the second pass one pixel to the right.
void ClockScreen::drawDateColumn(const RtcDateTime& t) {
    uint16_t key = static_cast<uint16_t>(t.day | (t.month << 5) | ((t.year - 2000) << 9));
    if (key == m_drawnDateKey) return;
    m_drawnDateKey = key;

    char wday[4], day[4];
    const char* w = kWeekdays[RtcClock::weekday(t)];
    for (int i = 0; i < 3; ++i) wday[i] = static_cast<char>(toupper(static_cast<unsigned char>(w[i])));
    wday[3] = '\0';
    snprintf(day, sizeof(day), "%u", static_cast<unsigned>(t.day));

    Arduino_GFX* gfx = Renderer::instance().gfx();
    gfx->fillRect(m_dateX, m_dateTop, m_dateW, m_dayY + 8 * m_daySize - m_dateTop, COLOR_BLACK);
    auto boldCentered = [&](const char* text, uint8_t size, int16_t y, uint16_t color) {
        int16_t tw = static_cast<int16_t>(strlen(text)) * 6 * size;
        int16_t x  = m_dateX + (m_dateW - 1 - tw) / 2;
        gfx->setTextSize(size);
        gfx->setTextColor(color);
        gfx->setCursor(x, y);
        gfx->print(text);
        gfx->setCursor(x + 1, y); // second pass, transparent: bold
        gfx->print(text);
    };
    boldCentered(wday, m_wdaySize, m_dateTop, kColorWeekday);
    boldCentered(day,  m_daySize,  m_dayY,    COLOR_WHITE);
}

void ClockScreen::onEnter() {
    Renderer::instance().clear();
    layout();

    // All segments as ghosts first; render() lights the right ones.
    for (uint8_t i = 0; i < m_digitCount; ++i) {
        m_drawnMask[i] = kMaskBlank;
        drawDigit(i, kMaskBlank, true);
    }
    drawColons(false, true);
    m_drawnPm       = -1;
    m_drawnDateKey  = 0xFFFF;
    m_noClockShown  = false;
    m_lastReadMs    = 0;
    m_haveTime      = false;
}

void ClockScreen::render() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    // The DS3231 is read 5x a second -- often enough to flip right at the
    // second boundary, cheap enough (one short I2C read) not to matter.
    uint32_t nowMs = millis();
    if (m_lastReadMs == 0 || nowMs - m_lastReadMs >= 200) {
        m_lastReadMs = nowMs ? nowMs : 1;
        m_haveTime   = RtcClock::instance().read(m_now);
    }

    if (!m_haveTime) {
        if (!m_noClockShown) {
            for (uint8_t i = 0; i < m_digitCount; ++i) drawDigit(i, kMaskDash, false);
            drawColons(false, false);
            const char* msg = "no clock module";
            int16_t w = static_cast<int16_t>(strlen(msg)) * 6 * kSmallSize;
            gfx->setTextSize(kSmallSize);
            gfx->setTextColor(COLOR_RED, COLOR_BLACK);
            gfx->setCursor((SCREEN_WIDTH - w) / 2, SCREEN_HEIGHT - kSmallH - 2);
            gfx->print(msg);
            m_noClockShown = true;
            m_drawnDateKey = 0xFFFF;
        }
        return;
    }
    if (m_noClockShown) { // clock came back -- start clean
        onEnter();
        m_haveTime = RtcClock::instance().read(m_now);
        if (!m_haveTime) return;
    }

    uint8_t hour = m_now.hour;
    bool    pm   = hour >= 12;
    if (!m_24h) {
        hour %= 12;
        if (hour == 0) hour = 12;
    }

    uint8_t masks[kMaxDigits];
    masks[0] = (!m_24h && hour < 10) ? kMaskBlank : kDigitMask[hour / 10];
    masks[1] = kDigitMask[hour % 10];
    masks[2] = kDigitMask[m_now.minute / 10];
    masks[3] = kDigitMask[m_now.minute % 10];
    masks[4] = kDigitMask[m_now.second / 10];
    masks[5] = kDigitMask[m_now.second % 10];
    for (uint8_t i = 0; i < m_digitCount; ++i) drawDigit(i, masks[i], false);

    // Colon steady with seconds shown; blinking (on for even seconds) without.
    drawColons(m_mode == ClockMode::SECONDS || (m_now.second % 2 == 0), false);

    if (!m_24h && static_cast<int8_t>(pm) != m_drawnPm) {
        m_drawnPm = static_cast<int8_t>(pm);
        int16_t x = m_digitX[m_digitCount - 1] + m_digitW + 6;
        gfx->setTextSize(kSmallSize);
        gfx->setTextColor(m_onColor, COLOR_BLACK);
        gfx->setCursor(x, m_digitY);
        gfx->print(pm ? "PM" : "AM");
    }

    if (m_dateW) drawDateColumn(m_now);
}
