#pragma once
#include "Screen.hpp"
#include "Config.hpp"

// A short popup in the style of the volume overlay: a title and up to two
// lines, for NOTICE_POPUP_MS -- e.g. "CALIBRATION ADVISED" with the reason
// the daughter gives (revision 14's resync hint), or an NTC fault.
//
// Like the other overlays, main.cpp renders it INSTEAD of the current screen
// while it's up, then re-enters the screen. It gives way to the volume
// overlay, the menu and the power layer.
class NoticeOverlay : public Screen {
public:
    void onEnter() override { m_needFull = true; }
    void render() override;
    const char* name() const override { return "NOTICE"; }

    // resync: this is the "CALIBRATION ADVISED" notice, where RIGHT jumps to
    // the calibration start (main.cpp).
    void show(const char* title, const char* line1, const char* line2, uint16_t titleColor, bool resync = false);
    bool isResyncNotice() const { return m_active && m_resync; }

    // Watches the volume board's ENV readings and raises the resync and NTC
    // notices (see NoticeOverlay.cpp). unitOn: no notices in standby.
    void watchDaughter(bool unitOn);
    void update();                     // expiry
    bool isActive() const { return m_active; }
    void dismiss() { m_active = false; }

private:
    bool     m_active   = false;
    bool     m_needFull = true;
    uint32_t m_untilMs  = 0;
    uint16_t m_titleColor = COLOR_YELLOW;
    bool     m_resync     = false;
    char     m_title[24] = "";
    char     m_line1[40] = "";
    char     m_line2[40] = "";
};
