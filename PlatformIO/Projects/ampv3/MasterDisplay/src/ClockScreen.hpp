#pragma once
#include "Screen.hpp"
#include "Config.hpp"
#include "RtcClock.hpp"
#include "DisplaySettings.hpp"

// Big seven-segment clock from the DS3231 (RtcClock), laid out in onEnter()
// from Settings -> Clock: Display = Clock only (HH:MM), Seconds (HH:MM:SS) or
// Date (HH:MM plus a four-row date column on the right: weekday, day, month,
// year, in the largest text that fits). The digits always use (nearly) the
// full panel height; 12-hour mode adds AM/PM after them.
//
// Unlit segments are drawn as a faint "ghost" of the lit color, like a real
// LED/LCD display. Only segments that change state are repainted, so a
// normal second tick costs a few small fills. The color is Settings -> Clock
// -> Color; the colon blinks once a second when seconds aren't shown.
class ClockScreen : public Screen {
public:
    void onEnter() override;
    void render() override;
    const char* name() const override { return "CLOCK"; }

private:
    static constexpr uint8_t kMaxDigits = 6;

    void layout();
    void drawDigit(uint8_t index, uint8_t newMask, bool force);
    void drawColons(bool on, bool force);
    void drawDateColumn(const RtcDateTime& t);
    void drawSegment(int16_t x, int16_t y, uint8_t seg, uint16_t color);

    // layout (from onEnter)
    ClockMode m_mode      = ClockMode::DATE;
    bool    m_24h         = true;
    uint8_t m_digitCount  = 4;
    int16_t m_digitX[kMaxDigits] = {};
    int16_t m_digitY = 0, m_digitW = 0, m_digitH = 0, m_seg = 0;
    int16_t m_colonX[2] = {};
    uint8_t m_colonCount = 1;
    // Date column (DATE mode): four rows -- weekday, day, month, year -- in
    // the largest text size that still leaves the digits nearly full height.
    int16_t m_dateX = 0, m_dateW = 0, m_dateTop = 0, m_dateRowH = 0;
    uint8_t m_dateSize = 0;

    uint16_t m_onColor    = 0;
    uint16_t m_ghostColor = 0;

    // what's on the panel
    uint8_t  m_drawnMask[kMaxDigits] = {};
    int8_t   m_drawnColon = -1;
    int8_t   m_drawnPm    = -1;
    uint16_t m_drawnDateKey = 0xFFFF;
    bool     m_noClockShown = false;

    uint32_t    m_lastReadMs = 0;
    RtcDateTime m_now;
    bool        m_haveTime = false;
};
