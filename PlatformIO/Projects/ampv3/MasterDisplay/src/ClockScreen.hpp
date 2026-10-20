#pragma once
#include "Screen.hpp"
#include "Config.hpp"
#include "RtcClock.hpp"

// Big seven-segment clock from the DS3231 (RtcClock). Layout follows the
// Settings -> Clock options (date line, seconds, 24h/12h), computed in
// onEnter(): the digits are sized to fill the panel, so hiding the date or
// the seconds makes them bigger.
//
// Unlit segments are drawn as a faint "ghost" of the lit color, like a real
// LED/LCD display. Only segments that change state are repainted, so a
// normal second tick costs a few small fills. Colors follow the active color
// theme (its low color), and the colon blinks once a second when seconds
// aren't shown.
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
    void drawDateLine(const RtcDateTime& t);
    void drawSegment(int16_t x, int16_t y, uint8_t seg, uint16_t color);

    // layout (from onEnter)
    bool    m_showDate    = true;
    bool    m_showSeconds = false;
    bool    m_24h         = true;
    uint8_t m_digitCount  = 4;
    int16_t m_digitX[kMaxDigits] = {};
    int16_t m_digitY = 0, m_digitW = 0, m_digitH = 0, m_seg = 0;
    int16_t m_colonX[2] = {};
    uint8_t m_colonCount = 1;
    int16_t m_dateY = 0;

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
