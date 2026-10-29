#include <Arduino.h>

#include "hardware/clocks.h"
#include "Console.hpp"
#include "SystemInfo.hpp"
#include "Profiler.hpp"
#include "ViewManager.hpp"

#include "Config.hpp"
#include "Version.hpp"
#include "AudioEngine.hpp"
#include "InputEngine.hpp"
#include "Renderer.hpp"
#include "VolumeControl.hpp"
#include "Screen.hpp"
#include "SpectrumScreen.hpp"
#include "WaveformScreen.hpp"
#include "AnalogVuScreen.hpp"
#include "DigitalVuMeterScreen.hpp"
#include "ClockScreen.hpp"
#include "DisplayTestScreen.hpp"
#include "PowerControl.hpp"
#include "PowerOverlay.hpp"
#include "AutoStandby.hpp"
#include "NoticeOverlay.hpp"
#include "DisplaySettings.hpp"
#include "ColorTheme.hpp"
#include "MenuScreen.hpp"
#include "InfoScreen.hpp"
#include "DebugFlags.hpp"
#include "BeatDetector.hpp"
#include "RtcClock.hpp"
#include "MasterConsole.hpp"
#include "DaughterBoardLink.hpp"
#include "MenuContent.hpp"
#include "PersistentSettings.hpp"
#include "ScreenRegistry.hpp"

void setup1() {
    AudioEngine::instance().runCore1();
}

void loop1() {
    delay(1000);
}

// Fixed set of available screens, cycled by MODE_NEXT/MODE_PREV. Adding a
// new screen is one array entry here plus its own Screen subclass -- no
// changes needed to the switching logic below. Order/count here must match
// ScreenRegistry.hpp's kScreenNames[] exactly (enforced below), since
// MenuContent.cpp's "Starting Screen" options select by index into this
// same array.
static SpectrumScreen        g_spectrumScreen;
static WaveformScreen        g_waveformScreen;
static AnalogVuScreen        g_analogVuScreen;
static DigitalVuMeterScreen  g_digitalVuMeterScreen;
static ClockScreen           g_clockScreen;
static InfoScreen            g_infoScreen;   // also the volume popup over the other screens

// Order must match kScreenNames (ScreenRegistry.hpp).
static Screen* g_screens[] = {
    &g_infoScreen,
    &g_waveformScreen,
    &g_spectrumScreen,
    &g_clockScreen,
    &g_digitalVuMeterScreen,
    &g_analogVuScreen,
};
constexpr size_t kNumScreens = sizeof(g_screens) / sizeof(g_screens[0]);

static_assert(kNumScreens == kScreenCount,
              "g_screens[] here and kScreenNames[] in ScreenRegistry.hpp must have the same number of "
              "entries, in the same order -- MenuContent.cpp's Starting Screen options select by index "
              "into g_screens[] but only know about it via kScreenNames[]/kScreenCount");

static MenuScreen        g_menuScreen;
static DisplayTestScreen g_displayTest;    // Diagnostics > Display Test (DisplayTestScreen.hpp)
static PowerOverlay      g_powerOverlay;   // power sequence / "press POWER" popup
static NoticeOverlay     g_noticeOverlay;  // resync advised, sensor fault

// What's on the panel -- the screen ring, the menu, the display test, the
// popups and the standby views -- is decided by the view manager.
static ViewManager g_views;

void setup() {

    // First thing: the amp's mains relay to its boot state (off by default),
    // before anything else gets a chance to run.
    PowerControl::instance().init();

    // Force the internal SMPS out of PFM mode into low-noise PWM mode
    digitalWrite(REGULATOR_MODE_PIN, HIGH);
    pinMode(REGULATOR_MODE_PIN, OUTPUT);

    if (CUSTOM_CLOCKS_ENABLED) {
        // Overclock the CPU core(s).
        set_sys_clock_khz(CPU_SPEED_KHZ, true);

        // Explicitly bind the peripheral clock back to the system clock.
        // This scales clk_peri to match CPU_SPEED_KHZ, unlocking finer SPI
        // dividers than the fixed 48MHz clk_peri default would allow.
        uint32_t freq = clock_get_hz(clk_sys);
        clock_configure(
            clk_peri,
            0,                                         // No specific clock division at the aux mux
            CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, // Hook aux source to clk_sys
            freq,                                      // Input frequency
            freq                                       // Output frequency target
        );
    }

    Serial.begin(115200);

    uint32_t timeout = millis();
    while (!Serial && (millis() - timeout < 3000)) {
        delay(10);
    }

    SystemInfo::printBootReport();
    
    Renderer::instance().init();
    Console::log("OK", "Renderer initialized.");

    InputEngine::instance().init(BTN_RT_PIN, BTN_LT_PIN, IR_PIN, BTN_UP_PIN, BTN_DN_PIN);
    Console::log("OK", "InputEngine initialized.");

    VolumeControl::instance().init();
    Console::log("OK", "VolumeControl initialized.");
    Console::log("OK", "Power control: %s (IR power key toggles)", PowerControl::instance().isOn() ? "ON" : "STANDBY");

    RtcClock::instance().begin(); // logs its own outcome

    g_menuScreen.init(getRootMenuItems(), getRootMenuItemCount());

    // Applies any saved settings (falls back to defaults if none are
    // valid) before anything below reads them.
    PersistentSettings::instance().loadAndApply();

    g_views.begin({ g_screens, kNumScreens, &g_infoScreen, &g_noticeOverlay, &g_menuScreen,
                    &g_displayTest, &g_powerOverlay, &g_clockScreen },
                  kDefaultScreenIndex); // always boots on the first screen (Info)

    MasterConsole::instance().begin();
}

// Rolling performance counters, reported to Serial once per second.
static PerfStat g_renderStat;          // time inside render() only
static PerfStat g_loopStat;            // full loop() iteration, incl. pacing delay
static uint32_t g_lastReportMs = 0;

// Auto standby (Settings -> Power, see AutoStandby): when it says so, run
// the normal standby sequence -- closing the menu / display test first.
static void autoStandbyUpdate(bool keyActivity) {
    if (!AutoStandby::instance().update(keyActivity)) return;
    Console::log("Power", "Auto standby: no audio or input for %u min", DisplaySettings::instance().getAutoStandbyMinutes());
    g_views.closeAllModal();
    PowerControl::instance().toggle();
}

void loop() {
    uint32_t loopStartUs = micros();

    MasterConsole::instance().update(); // USB console + daughter pass-through
    Console::service();                 // send the lines core 1 queued

    // DOWN's double-click (mute) only outside the menu, the display test and
    // standby -- there DOWN must act at once.
    InputEngine::instance().setDownGestures(!g_views.menuOpen() && !g_views.displayTestOpen() && PowerControl::instance().isOn());
    UIEvent event = InputEngine::instance().pollEvents();

    // Any key or remote press counts as activity for auto standby.
    bool keyActivity = (event != UIEvent::NONE) ||
                       InputEngine::instance().isVolUpHeld() || InputEngine::instance().isVolDownHeld();

    // --- Power / standby ---
    // The power key works everywhere. Switching off closes the menu and the
    // display test first, so standby always starts from a clean state.
    PowerControl& power = PowerControl::instance();
    if (event == UIEvent::POWER_TOGGLE) {
        if (power.isOn()) g_views.closeAllModal();
        power.toggle();
        event = UIEvent::NONE;
    }
    power.update();
    g_powerOverlay.update();
    autoStandbyUpdate(keyActivity);

    // Deferred save for settings changed outside the menu (the Options key
    // on the Clock): written once nothing has changed for a few seconds, so
    // stepping through the options costs one flash write, not one per press.
    // (Menu changes save when the menu closes.)
    if (PersistentSettings::instance().isDirty() && !g_views.menuOpen() &&
        PersistentSettings::instance().msSinceDirty() >= 5000) {
        PersistentSettings::instance().saveIfDirty();
    }
    BeatDetector::instance().update(); // tempo estimate, about once a second
    g_noticeOverlay.watchDaughter(PowerControl::instance().isOn());
    g_noticeOverlay.update();

    // Not on (standby, or a power sequence running): nothing but the power
    // key does anything. In standby, any other key shows the
    // "press POWER" popup.
    if (!power.isOn()) {
        bool anyKey = (event != UIEvent::NONE) ||
                      InputEngine::instance().volUpJustPressed() ||
                      InputEngine::instance().volDownJustPressed();
        if (anyKey && power.isStandby()) g_powerOverlay.showHint();
        event = UIEvent::NONE;
    }

    if (g_views.displayTestOpen()) {
        // Up/Down pick the test, Right clears it, Left or the menu button
        // leaves. Everything else is swallowed while the test runs.
        bool irRepeat  = InputEngine::instance().lastIrWasRepeat();
        bool upPress   = InputEngine::instance().volUpJustPressed();
        bool downPress = InputEngine::instance().volDownJustPressed();
        if ((event == UIEvent::GAIN_UP && !irRepeat) || upPress)     g_displayTest.prevTest();
        if ((event == UIEvent::GAIN_DOWN && !irRepeat) || downPress) g_displayTest.nextTest();
        if (event == UIEvent::MODE_NEXT) g_displayTest.restart();
        if (event == UIEvent::MUTE_TOGGLE)         g_displayTest.toggleColumnFix(); // A/B the panel workarounds
        if (event == UIEvent::CHANNEL_MODE_TOGGLE) g_displayTest.toggleMinRows();
        if (event == UIEvent::MODE_PREV || event == UIEvent::MENU_TOGGLE) g_views.closeDisplayTest();
        event = UIEvent::NONE;
    }

    if (g_views.menuOpen() && !g_views.displayTestOpen()) {
        // (With the display test over the menu, the test above has the keys.)
        // While the menu is open, MODE_NEXT/PREV/GAIN_UP/DOWN/MENU_TOGGLE
        // are all consumed by menu navigation instead of their normal
        // meanings (screen switching, volume control). Everything else
        // (MUTE_TOGGLE, CHANNEL_MODE_TOGGLE) still works normally -- only
        // the controls the menu specifically repurposes are intercepted
        // here.
        if (event == UIEvent::MENU_TOGGLE) {
            g_views.closeMenu();
        } else if (event == UIEvent::GAIN_UP) {
            // Menu navigation is single-step-per-press, not continuous --
            // ignore IR repeat frames so holding the remote button doesn't
            // rocket through several items at once. GAIN_UP/DOWN's
            // CONTINUOUS policy stays as-is (needed for real volume
            // control outside the menu); this only filters how the menu
            // specifically consumes it.
            // In an editor, held IR keys auto-repeat (to run values up fast).
            if (!InputEngine::instance().lastIrWasRepeat() || g_menuScreen.acceptsRepeats()) {
                g_menuScreen.moveFocus(-1);
            }
        } else if (event == UIEvent::GAIN_DOWN) {
            if (!InputEngine::instance().lastIrWasRepeat() || g_menuScreen.acceptsRepeats()) {
                g_menuScreen.moveFocus(1);
            }
        } else if (event == UIEvent::MODE_NEXT) {
            g_menuScreen.enter();
        } else if (event == UIEvent::MODE_PREV) {
            if (!g_menuScreen.back()) g_views.closeMenu();
        } else if (event == UIEvent::MUTE_TOGGLE) {
            VolumeControl::instance().toggleMute();
        } else if (event == UIEvent::CHANNEL_MODE_TOGGLE) {
            DisplaySettings::instance().toggleChannelMode();
        }

        // The physical Up/Down buttons (they never produce events -- volume
        // reads them as held levels): one focus step per press, auto-repeat
        // while held in an editor.
        g_menuScreen.handleUpDownButtons();
    } else {
        bool noticeShown = g_views.shown() == ViewManager::View::NOTICE;
        bool popupShown  = g_views.shown() == ViewManager::View::INFO_POPUP;
        if (event == UIEvent::MODE_NEXT && noticeShown && g_noticeOverlay.isResyncNotice()) {
            // RIGHT on the "CALIBRATION ADVISED" notice: straight to the
            // resync start (Calibration > Rtotal, Cancel / Start).
            g_noticeOverlay.dismiss();
            g_views.openMenu();
            menuOpenResyncStart(g_menuScreen);
            Console::log("Menu", "...at the calibration start (from the notice)");
        } else if (event == UIEvent::MODE_NEXT) {
            g_views.nextScreen();
        } else if (event == UIEvent::MODE_PREV) {
            g_views.prevScreen();
        } else if (event == UIEvent::MUTE_TOGGLE) {
            VolumeControl::instance().toggleMute();
            g_infoScreen.poke();
            Console::log("Volume", "Mute toggled -> %s", VolumeControl::instance().isMuted() ? "MUTED" : "UNMUTED");
        } else if (event == UIEvent::CHANNEL_MODE_TOGGLE &&
                   (g_views.isCurrent(&g_infoScreen) || popupShown)) {
            // On the Info screen (or its popup) the Options key switches the
            // right-hand readout between Rtotal and the detected tempo -- the
            // Info screen has no stereo/mono layout of its own.
            g_infoScreen.toggleRightInfo();
            if (popupShown) g_infoScreen.poke();
            Console::log("Display", "Info screen: next middle-right view");
        } else if (event == UIEvent::CHANNEL_MODE_TOGGLE && g_views.isCurrent(&g_clockScreen)) {
            // On the Clock screen the Options key steps the Display setting
            // (Settings > Clock > Display): date -> seconds -> clock only.
            DisplaySettings& ds = DisplaySettings::instance();
            ClockMode next = (ds.getClockMode() == ClockMode::DATE)    ? ClockMode::SECONDS
                           : (ds.getClockMode() == ClockMode::SECONDS) ? ClockMode::CLOCK_ONLY
                                                                       : ClockMode::DATE;
            ds.setClockMode(next);
            PersistentSettings::instance().markDirty(); // saved a few seconds later (deferred save below)
            g_clockScreen.onEnter();
            Console::log("Display", "Clock shows %s", next == ClockMode::DATE ? "date" : next == ClockMode::SECONDS ? "seconds" : "time only");
        } else if (event == UIEvent::CHANNEL_MODE_TOGGLE) {
            DisplaySettings::instance().toggleChannelMode();
            g_infoScreen.dismiss();
            // Channel mode changes a screen's whole layout (bar count/
            // width, trace split): rebuild it rather than continue with
            // stale state from the previous mode.
            g_views.reenterScreen();
            Console::log("Display", "Channel mode switched to %s", DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO ? "STEREO" : "MONO");
        } else if (event == UIEvent::MENU_TOGGLE) {
            g_views.openMenu();
        } else if (event == UIEvent::GAIN_UP || event == UIEvent::GAIN_DOWN) {
            // Volume: InputEngine tracks these (volUp/DownRequested()), below.
        } else if (event != UIEvent::NONE) {
            Console::log("Input", "Event ID: %d", static_cast<int>(event));
        }
    }

    // Volume control: forced false while the menu is open, regardless of
    // actual button states, so navigating the menu (which repurposes the
    // same physical volume buttons) never also changes real volume.
    // VolumeControl::update() is still called every loop either way, since
    // it must keep servicing the daughter board link regardless.
    // The Display Test menu action fires inside the menu's enter() above.
    if (DisplayTestScreen::consumeStartRequest()) g_views.openDisplayTest();

    bool wantVolUp = false, wantVolDown = false;
    if (power.isOn() && !g_views.menuOpen() && !g_views.displayTestOpen()) {
        wantVolUp   = InputEngine::instance().volUpRequested();
        wantVolDown = InputEngine::instance().volDownRequested();
    }
    bool onInfoScreen = g_views.isCurrent(&g_infoScreen);
    bool showInfo     = g_infoScreen.filterFirstVolumePress(wantVolUp, wantVolDown, onInfoScreen);
    VolumeControl::instance().update(wantVolUp, wantVolDown);

    // Info popup: any volume input keeps it up (the daughter's own VOL/DB
    // arrive a few ms later via STATUS and update the readout); update() also
    // pops it for changes seen in STATUS and expires it. Not needed while the
    // info screen itself is showing.
    if (showInfo) g_infoScreen.poke();
    g_infoScreen.update();

    // What's on the panel this frame -- power layer, test, menu, popups or
    // the screen -- and its transitions: see ViewManager.
    g_views.update(power.isOn());

    uint32_t renderStartUs = micros();
    g_views.render();
    uint32_t renderUs = micros() - renderStartUs;

    // Only sleep off whatever's left of the frame budget. Previously this
    // was an unconditional delay(16), which always added 16ms on top of
    // however long rendering took rather than pacing to a 16ms period.
    uint32_t elapsedUs = micros() - loopStartUs;
    if (elapsedUs < TARGET_FRAME_US) {
        delayMicroseconds(TARGET_FRAME_US - elapsedUs);
    }

    uint32_t loopUs = micros() - loopStartUs;

    // --- Performance reporting ---
    g_renderStat.add(renderUs);
    g_loopStat.add(loopUs);

    uint32_t nowMs = millis();
    if (nowMs - g_lastReportMs >= FPS_REPORT_INTERVAL_MS) {
        float elapsedMs    = static_cast<float>(nowMs - g_lastReportMs);
        float fps          = (g_loopStat.count * 1000.0f) / elapsedMs;

        if (DebugFlags::perf && !DaughterBoardLink::instance().isPassthrough()) { // quiet during a pass-through session
            const char* screen = g_views.shownName();
            // RTC time -- one short I2C read, once a second.
            char timeBuf[20] = "--";
            RtcDateTime now;
            if (RtcClock::instance().read(now)) RtcClock::format(now, timeBuf, sizeof(timeBuf));
            Console::log("Perf", "FPS: %s | render: %sms avg (max %s) | loop: %sms avg (max %s) | screen: %s | time: %s",
                         Console::num(fps, 1).s,
                         Console::num(g_renderStat.avgMs(), 2).s, Console::num(g_renderStat.maxMs(), 2).s,
                         Console::num(g_loopStat.avgMs(), 2).s, Console::num(g_loopStat.maxMs(), 2).s,
                         screen, timeBuf);
        }

        g_renderStat.reset();
        g_loopStat.reset();
        g_lastReportMs    = nowMs;
    }
}