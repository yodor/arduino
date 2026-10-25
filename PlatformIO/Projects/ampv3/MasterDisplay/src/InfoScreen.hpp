#pragma once
#include "Screen.hpp"
#include "Config.hpp"

// The info screen: volume plus the unit's state at a glance. It's the first
// screen in the cycle, AND the popup shown over the other screens for a
// moment when the volume or mute changes (main.cpp; the old volume overlay).
//
//   header:  COMP  +0.05dB  24.3C  MUTE           21:47
//   middle:  -23.5 dB                       R=95K (auto)
//                                                  14/31
//   bottom:  the volume bar, one block per step, lit up to the current one
//
// Header tags are short colored words in fixed slots (each appears only when
// it has something to say): COMP green when the thermal compensation is on,
// gray when off; the last calibration's L/R match; the driver-block
// temperature (red "NTC!" on a sensor fault); an alert slot shared by MUTE
// (red) and RESYNC (yellow, the daughter recommends one) -- alternating each
// second when both apply; the time on the right. Text rather than icons: at 16 px with the
// panel's bitmap font, words read instantly where icons would be blobs.
//
// When the daughter is calibrating, uncalibrated or not answering, the big
// area says so instead of showing a stale number. Everything is redrawn only
// when it changes.
class InfoScreen : public Screen {
public:
    void onEnter() override;  // full redraw
    void render() override;   // redraws only what changed
    const char* name() const override { return "INFO"; }

    // --- As the popup over other screens ---
    void poke();                    // show / extend (volume or mute touched)
    void update();                  // call every loop: change detection + expiry
    bool isActive() const { return m_active; }
    void dismiss() { m_active = false; }

private:
    enum class View : uint8_t { NONE, NO_LINK, CHECKING, CALIBRATING, NOT_CALIBRATED, VOLUME };
    View currentView() const;

    void drawStaticForView(View v);
    void drawRTag();
    void drawTags();
    void drawValue(int16_t vol, int16_t volMax, int32_t dbTenths);
    void drawBar(int16_t vol, int16_t volMin, int16_t volMax, bool muted);
    void drawCentered(const char* text, uint8_t size, int16_t y, uint16_t color);
    void drawSlot(uint8_t slot, int16_t x, uint8_t widthChars, const char* text, uint16_t color);

    bool     m_active     = false;
    uint32_t m_lastPokeMs = 0;

    // change detection in update()
    uint32_t m_lastStatusCount = 0;
    bool     m_haveBaseline    = false;
    int16_t  m_baseVol         = -2;
    bool     m_baseMute        = false;

    // what's on the panel
    bool     m_fullRedraw    = true;
    View     m_drawnView     = View::NONE;
    int32_t  m_drawnDbTenths = INT32_MIN;
    int16_t  m_drawnVol      = -2;
    int16_t  m_drawnVolMin   = -2;
    int16_t  m_drawnVolMax   = -2;
    int16_t  m_drawnLit      = -1;
    bool     m_drawnBarMuted = false;
    char     m_drawnR[24]    = "";
    static constexpr uint8_t kSlots = 5;
    char     m_drawnSlot[kSlots][12] = {};
    uint16_t m_drawnSlotColor[kSlots] = {};
    uint32_t m_lastTimeReadMs = 0;
};
