#pragma once
#include <Arduino.h>
#include "Screen.hpp"

class InfoScreen;
class NoticeOverlay;
class MenuScreen;
class DisplayTestScreen;
class PowerOverlay;
class ClockScreen;

// Decides what's on the panel, every frame, in one place. In priority order:
//
//   1. the power layer -- its popup (a power sequence, or the "press POWER"
//      hint), or in standby the clock (bright or dimmed) or a dark panel
//   2. the display test        (Diagnostics > Display Test)
//   3. the menu
//   4. the Info popup          (volume/mute touched; not on the Info screen
//                               itself, and not over the menu or the test)
//   5. a notice                (resync advised, sensor fault; not over the
//                               menu or the test, gives way to the popup)
//   6. the current screen of the ring (RIGHT/LEFT)
//
// Whatever becomes visible again is redrawn from scratch -- the view that
// was on top painted over it. The backlight follows the power layer.
// main.cpp only asks for things (next screen, open the menu...); the
// manager works out the rest.
class ViewManager {
public:
    enum class View : uint8_t { SCREEN, NOTICE, INFO_POPUP, MENU, DISPLAY_TEST,
                                POWER_POPUP, STANDBY_CLOCK, STANDBY_DARK };

    struct Parts {
        Screen* const*     screens;     // the ring, in RIGHT order
        size_t             screenCount;
        InfoScreen*        info;        // also one of the screens: the popup
        NoticeOverlay*     notice;
        MenuScreen*        menu;
        DisplayTestScreen* displayTest;
        PowerOverlay*      powerOverlay;
        ClockScreen*       standbyClock;
    };

    void begin(const Parts& parts, size_t startScreen);

    // --- The screen ring ---
    void    nextScreen();
    void    prevScreen();
    void    reenterScreen();             // its layout changed (stereo/mono...): rebuild it
    Screen* currentScreen() const        { return m_parts.screens[m_screenIdx]; }
    bool    isCurrent(const Screen* s) const { return currentScreen() == s; }

    // --- Modal views ---
    void openMenu();                     // fresh, at the top
    void closeMenu();                    // back to the screen; saves changed settings
    bool menuOpen() const                { return m_menuOpen; }
    void openDisplayTest();              // over the menu; closing it returns there
    void closeDisplayTest();
    bool displayTestOpen() const         { return m_testOpen; }
    void closeAllModal();                // before standby: menu and test closed

    // --- Every frame ---
    // unitOn: PowerControl says the unit is on (no sequence running).
    void update(bool unitOn);
    void render();

    View        shown() const            { return m_shown; }
    const char* shownName() const;       // for the [Perf] line

private:
    View wantedView(bool unitOn);
    void enter(View v);                  // the transition into v

    Parts  m_parts{};
    size_t m_screenIdx = 0;
    size_t m_screenBeforeMenu = 0;
    bool   m_menuOpen  = false;
    bool   m_testOpen  = false;
    View   m_shown     = View::SCREEN;
    bool   m_started   = false;          // the first update() enters whatever is wanted
};
