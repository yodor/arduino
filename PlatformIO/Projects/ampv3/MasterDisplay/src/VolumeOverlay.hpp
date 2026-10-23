#pragma once
#include "Screen.hpp"
#include "Config.hpp"

// Transient on-screen volume readout, shown over whatever screen is active
// for VOLUME_OVERLAY_TIMEOUT_MS after the volume or mute changes -- either
// from the buttons/IR (main.cpp calls poke()) or seen in the daughter's
// STATUS (update() notices VOL/MUTE changing, e.g. from its own console).
//
// Not part of the normal screen cycle -- handled like MenuScreen: while
// active, main.cpp renders this INSTEAD of the current screen, and when it
// expires main.cpp calls the current screen's onEnter() so that screen
// redraws from scratch. That keeps every visualization's delta-redraw state
// honest (drawing on top of a live screen would leave it believing pixels
// are still there that the overlay painted over).
//
// Everything shown comes from the daughter's STATUS (VOL, DB, VOLMIN/VOLMAX)
// -- the master never guesses the volume -- plus the local mute state. When
// the daughter is calibrating, in its boot hold, uncalibrated or not
// answering, the overlay says so instead of showing a stale number.
class VolumeOverlay : public Screen {
public:
    void onEnter() override;  // full redraw
    void render() override;   // redraws only what changed
    const char* name() const override { return "VOLUME"; }

    void poke();                    // user touched volume/mute: show, or extend
    void update();                  // call every loop: change detection + expiry
    bool isActive() const { return m_active; }
    void dismiss() { m_active = false; }

private:
    enum class View : uint8_t { NONE, NO_LINK, CHECKING, CALIBRATING, NOT_CALIBRATED, VOLUME };
    View currentView() const;

    void drawStaticForView(View v);
    void drawMuteTag(bool muted);
    void drawRTag();   // "R=650K (auto)" in the top-right corner
    void drawValue(int16_t vol, int16_t volMax, int32_t dbTenths);
    void drawBar(int16_t vol, int16_t volMin, int16_t volMax, bool muted);
    void drawCentered(const char* text, uint8_t size, int16_t y, uint16_t color);

    bool     m_active     = false;
    uint32_t m_lastPokeMs = 0;

    // change detection in update()
    uint32_t m_lastStatusCount = 0;
    bool     m_haveBaseline    = false;
    int16_t  m_baseVol         = -2;
    bool     m_baseMute        = false;

    // what's currently on the panel
    bool     m_fullRedraw    = true;
    View     m_drawnView     = View::NONE;
    int8_t   m_drawnMuted    = -1;
    int32_t  m_drawnDbTenths = INT32_MIN;
    int16_t  m_drawnVol      = -2;
    int16_t  m_drawnVolMin   = -2;
    int16_t  m_drawnVolMax   = -2;
    int16_t  m_drawnLit      = -1;
    bool     m_drawnBarMuted = false;
    uint32_t m_drawnROhms    = 0xFFFFFFFF;
    int8_t   m_drawnRAuto    = -1;
};
