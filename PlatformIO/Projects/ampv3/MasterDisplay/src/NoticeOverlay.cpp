#include "NoticeOverlay.hpp"
#include "Console.hpp"
#include "DaughterBoardLink.hpp"
#include "Renderer.hpp"
#include <cstring>
#include <cstdio>

namespace {
void centered(Arduino_GFX* gfx, const char* text, uint8_t size, int16_t y, uint16_t color) {
    int16_t w = static_cast<int16_t>(strlen(text)) * 6 * size;
    int16_t x = (SCREEN_WIDTH - w) / 2;
    gfx->setTextSize(size);
    gfx->setTextColor(color, COLOR_BLACK);
    gfx->setCursor(x < 0 ? 0 : x, y);
    gfx->print(text);
}
} // namespace

void NoticeOverlay::show(const char* title, const char* line1, const char* line2, uint16_t titleColor, bool resync) {
    m_resync = resync;
    snprintf(m_title, sizeof(m_title), "%s", title);
    snprintf(m_line1, sizeof(m_line1), "%s", line1 ? line1 : "");
    snprintf(m_line2, sizeof(m_line2), "%s", line2 ? line2 : "");
    m_titleColor = titleColor;
    m_active     = true;
    m_untilMs    = millis() + NOTICE_POPUP_MS;
    m_needFull   = true;
}

void NoticeOverlay::update() {
    if (m_active && static_cast<int32_t>(millis() - m_untilMs) >= 0) m_active = false;
}

void NoticeOverlay::render() {
    if (!m_needFull) return; // static content: drawn once
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;
    gfx->fillScreen(COLOR_BLACK);
    centered(gfx, m_title, 3, 30, m_titleColor);  // 23 characters fit at size 3
    centered(gfx, m_line1, 2, 74, COLOR_WHITE);   // 35 at size 2
    centered(gfx, m_line2, 2, 102, 0x8410);
    m_needFull = false;
}

// Revision 13/14 ENV (called every loop by main.cpp): while the daughter recommends a resync (RESYNC=1), the
// notice (with its reason) pops up again with each background ENV reading --
// every DAUGHTER_ENV_BACKGROUND_POLL_MS, at most -- until a calibration
// clears it: not intrusive, but insistent. Also once when the block NTC
// develops a fault (which turns the thermal compensation off).
void NoticeOverlay::watchDaughter(bool unitOn) {
    const DaughterEnv& e = DaughterBoardLink::instance().env();
    static uint32_t s_envSeenMs  = 0;
    static bool     s_lastNtcOk  = true;
    static bool     s_wasOn      = false;
    static uint32_t s_resyncShownMs = 0;
    static bool     s_resyncShown   = false;

    auto showResync = [&]() {
        const char* why = !strcmp(e.why, "SPAN")   ? "temperature beyond learned range"
                        : !strcmp(e.why, "LIMIT")  ? "30 C or more from calibration temp"
                        : !strcmp(e.why, "NOCOMP") ? "block moved >1 C, no compensation"
                        : !strcmp(e.why, "SEEK")   ? "last quiet-point seek didn't settle"
                                                   : "temperature changed";
        show("CALIBRATION ADVISED", why, "press RIGHT to calibrate", COLOR_YELLOW, true);
        if (!s_resyncShown) Console::log("Daughter", "Resync recommended (WHY=%s)", e.why);
        s_resyncShown   = true;
        s_resyncShownMs = millis();
    };

    bool on = unitOn;
    if (on && !s_wasOn && e.valid && e.resync == 1) showResync(); // switched on with the hint still up
    s_wasOn = on;

    if (!e.valid || e.receivedMs == s_envSeenMs) return;
    s_envSeenMs = e.receivedMs;

    // Each fresh reading with the hint up shows it again -- but no more often
    // than the background poll (a calibration page polls ENV every 5 s).
    if (e.resync == 1 && on &&
        (!s_resyncShown || millis() - s_resyncShownMs >= DAUGHTER_ENV_BACKGROUND_POLL_MS - 1000)) {
        showResync();
    }
    if (e.resync == 0) s_resyncShown = false; // cleared (a calibration ran): log it afresh next time

    bool ntcOk = e.ntcOk();
    if (!ntcOk && s_lastNtcOk) {
        char line[40];
        snprintf(line, sizeof(line), "block NTC %s: compensation off", e.ntc);
        if (on) show("SENSOR FAULT", line, "check the NTC on the driver block", COLOR_RED);
        Console::log("Daughter", "Block NTC fault: NTC=%s -- thermal compensation off", e.ntc);
    }
    s_lastNtcOk = ntcOk;
}
