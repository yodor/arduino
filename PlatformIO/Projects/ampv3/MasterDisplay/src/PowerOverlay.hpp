#pragma once
#include "Screen.hpp"
#include "Config.hpp"

// Power popup, the counterpart of the volume overlay: shown while a power
// sequence runs ("POWER ON" / "STANDBY" with the step and a progress bar),
// for POWER_POPUP_MS after it finishes, and as a hint when a key other than
// power is pressed in standby ("Press POWER to switch on").
//
// Like the other overlays, main.cpp renders this INSTEAD of the current
// screen while it's active, and re-enters whatever comes next afterwards.
class PowerOverlay : public Screen {
public:
    void onEnter() override;
    void render() override;
    const char* name() const override { return "POWER"; }

    void update();     // every loop: follows PowerControl, expires the popup
    void showHint();   // standby: someone pressed a key that needs power on
    bool isActive() const { return m_active; }

private:
    enum class Mode : uint8_t { NONE, SEQUENCE, HINT };

    void drawFrame();

    bool     m_active      = false;
    Mode     m_mode        = Mode::NONE;
    uint32_t m_shownUntil  = 0;    // popup expiry (after a sequence ends / for a hint)
    bool     m_wasTransition = false;

    // what's on the panel
    bool    m_needFull    = true;
    int8_t  m_drawnState  = -1;
    int16_t m_drawnFill   = -1;
};
