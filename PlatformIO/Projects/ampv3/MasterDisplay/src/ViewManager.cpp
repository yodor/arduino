#include "ViewManager.hpp"
#include "InfoScreen.hpp"
#include "NoticeOverlay.hpp"
#include "MenuScreen.hpp"
#include "DisplayTestScreen.hpp"
#include "PowerOverlay.hpp"
#include "ClockScreen.hpp"
#include "Renderer.hpp"
#include "DisplaySettings.hpp"
#include "PersistentSettings.hpp"
#include "Console.hpp"

void ViewManager::begin(const Parts& parts, size_t startScreen) {
    m_parts     = parts;
    m_screenIdx = (startScreen < parts.screenCount) ? startScreen : 0;
    m_shown     = View::SCREEN;
    m_started   = false;
    currentScreen()->onEnter();
}

// --- The screen ring -----------------------------------------------------------

void ViewManager::nextScreen() {
    m_screenIdx = (m_screenIdx + 1) % m_parts.screenCount;
    m_parts.info->dismiss();
    reenterScreen();
    Console::log("Display", "Screen switched to %s", currentScreen()->name());
}

void ViewManager::prevScreen() {
    m_screenIdx = (m_screenIdx + m_parts.screenCount - 1) % m_parts.screenCount;
    m_parts.info->dismiss();
    reenterScreen();
    Console::log("Display", "Screen switched to %s", currentScreen()->name());
}

void ViewManager::reenterScreen() {
    currentScreen()->onEnter();
    // Shown right away if nothing is on top; otherwise when it's uncovered.
    if (m_shown == View::INFO_POPUP || m_shown == View::NOTICE) m_shown = View::SCREEN;
}

// --- Modal views ---------------------------------------------------------------

void ViewManager::openMenu() {
    if (m_menuOpen) return;
    m_menuOpen         = true;
    m_screenBeforeMenu = m_screenIdx;
    m_parts.info->dismiss();
    m_parts.menu->onEnter();   // fresh, at the top
    m_shown = View::MENU;      // entered here (enter() would only redraw)
    Console::log("Menu", "Opened");
}

void ViewManager::closeMenu() {
    if (!m_menuOpen) return;
    m_menuOpen  = false;
    m_screenIdx = m_screenBeforeMenu;
    Console::log("Menu", "Closed");
    // Only touches flash if something changed while the menu was open: one
    // commit per menu session, not one per setting.
    PersistentSettings::instance().saveIfDirty();
}

// The test opens over the menu (Diagnostics > Display Test) and leaving it
// uncovers the menu again, at the same place.
void ViewManager::openDisplayTest() {
    m_testOpen = true;
    m_parts.info->dismiss();
    m_parts.displayTest->onEnter();
    m_shown = View::DISPLAY_TEST;
    Console::log("Diag", "Display test opened");
}

void ViewManager::closeDisplayTest() {
    if (!m_testOpen) return;
    m_testOpen = false;
    Console::log("Diag", "Display test closed");
}

void ViewManager::closeAllModal() {
    closeMenu();
    closeDisplayTest();
}

// --- Every frame ----------------------------------------------------------------

ViewManager::View ViewManager::wantedView(bool unitOn) {
    // 1. The power layer.
    if (m_parts.powerOverlay->isActive()) return View::POWER_POPUP;
    if (!unitOn) {
        return (DisplaySettings::instance().getStandbyDisplay() == StandbyDisplay::OFF)
                   ? View::STANDBY_DARK : View::STANDBY_CLOCK;
    }
    // 2./3. Modal views.
    if (m_testOpen) return View::DISPLAY_TEST;
    if (m_menuOpen) return View::MENU;
    // 4. The Info popup -- pointless on the Info screen itself.
    if (isCurrent(m_parts.info)) m_parts.info->dismiss();
    if (m_parts.info->isActive()) return View::INFO_POPUP;
    // 5. A notice.
    if (m_parts.notice->isActive()) return View::NOTICE;
    // 6. The screen.
    return View::SCREEN;
}

void ViewManager::enter(View v) {
    uint8_t brightness = DisplaySettings::instance().getBrightnessPercent();
    bool fromPower = (m_shown == View::POWER_POPUP || m_shown == View::STANDBY_CLOCK || m_shown == View::STANDBY_DARK);
    switch (v) {
        case View::POWER_POPUP:
            Renderer::instance().setBacklightPercent(brightness);
            m_parts.powerOverlay->onEnter();
            break;
        case View::STANDBY_CLOCK:
            Renderer::instance().setBacklightPercent(
                DisplaySettings::instance().getStandbyDisplay() == StandbyDisplay::DIM_CLOCK ? STANDBY_DIM_PERCENT : brightness);
            m_parts.standbyClock->onEnter();
            break;
        case View::STANDBY_DARK:
            Renderer::instance().clear();
            Renderer::instance().setBacklightPercent(0);
            break;
        case View::DISPLAY_TEST:
            if (fromPower) Renderer::instance().setBacklightPercent(brightness);
            m_parts.displayTest->onEnter();
            break;
        case View::MENU:
            if (fromPower) Renderer::instance().setBacklightPercent(brightness);
            m_parts.menu->invalidate();   // redraw, keeping the place in the menu
            break;
        case View::INFO_POPUP:
            m_parts.info->onEnter();
            break;
        case View::NOTICE:
            m_parts.notice->onEnter();
            break;
        case View::SCREEN:
            if (fromPower) Renderer::instance().setBacklightPercent(brightness);
            currentScreen()->onEnter();
            break;
    }
    m_shown = v;
}

void ViewManager::update(bool unitOn) {
    // In the power layer the popup and the notice simply stop -- nothing
    // underneath is shown.
    View want = wantedView(unitOn);
    if (want == View::POWER_POPUP || want == View::STANDBY_CLOCK || want == View::STANDBY_DARK) {
        m_parts.info->dismiss();
    }
    // The volume readout takes over from a notice.
    if (want == View::INFO_POPUP && m_shown == View::NOTICE) m_parts.notice->dismiss();
    // Over the menu or the test, the popup doesn't wait.
    if (m_menuOpen || m_testOpen) m_parts.info->dismiss();

    if (!m_started || want != m_shown) {
        m_started = true;
        enter(want);
    }
}

void ViewManager::render() {
    switch (m_shown) {
        case View::POWER_POPUP:   m_parts.powerOverlay->render(); break;
        case View::STANDBY_CLOCK: m_parts.standbyClock->render(); break;
        case View::STANDBY_DARK:  break; // backlight off: nothing to draw
        case View::DISPLAY_TEST:  m_parts.displayTest->render();  break;
        case View::MENU:          m_parts.menu->render();         break;
        case View::INFO_POPUP:    m_parts.info->render();         break;
        case View::NOTICE:        m_parts.notice->render();       break;
        case View::SCREEN:        currentScreen()->render();      break;
    }
}

const char* ViewManager::shownName() const {
    switch (m_shown) {
        case View::POWER_POPUP:   return m_parts.powerOverlay->name();
        case View::STANDBY_CLOCK: return "STANDBY CLOCK";
        case View::STANDBY_DARK:  return "STANDBY (display off)";
        case View::DISPLAY_TEST:  return m_parts.displayTest->name();
        case View::MENU:          return m_parts.menu->name();
        case View::INFO_POPUP:    return m_parts.info->name();
        case View::NOTICE:        return m_parts.notice->name();
        case View::SCREEN:        return currentScreen()->name();
    }
    return "?";
}
