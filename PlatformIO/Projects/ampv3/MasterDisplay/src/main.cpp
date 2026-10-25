#include <Arduino.h>

#include "hardware/clocks.h"
#include "hardware/structs/sysinfo.h" // Fixes the 'sysinfo_hw' declaration error

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
#include "VolumeOverlay.hpp"
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

// Order must match kScreenNames (ScreenRegistry.hpp).
static Screen* g_screens[] = {
    &g_clockScreen,
    &g_waveformScreen,
    &g_spectrumScreen,
    &g_digitalVuMeterScreen,
    &g_analogVuScreen,
};
constexpr size_t kNumScreens = sizeof(g_screens) / sizeof(g_screens[0]);

static_assert(kNumScreens == kScreenCount,
              "g_screens[] here and kScreenNames[] in ScreenRegistry.hpp must have the same number of "
              "entries, in the same order -- MenuContent.cpp's Starting Screen options select by index "
              "into g_screens[] but only know about it via kScreenNames[]/kScreenCount");

static size_t g_currentScreenIdx = 0;

static MenuScreen g_menuScreen;

// Transient volume readout (see VolumeOverlay.hpp). g_overlayShown tracks
// whether it currently owns the panel, so the transitions in and out can
// trigger the right full redraws.
// Settings -> Diagnostics -> Display Test: an exclusive mode like the menu
// (see DisplayTestScreen.hpp for the tests and controls).
static DisplayTestScreen g_displayTest;
static bool              g_displayTestOpen = false;

// Power / standby. g_powerView is what the power layer currently owns the
// panel with (NONE = the normal screens/menu/overlays below it).
enum class PowerView : uint8_t { NONE, POPUP, STANDBY_CLOCK, STANDBY_DARK };
static PowerOverlay g_powerOverlay;
static PowerView    g_powerView = PowerView::NONE;

static VolumeOverlay g_volumeOverlay;
static bool          g_overlayShown = false;

// Notices from the daughter (resync advised, NTC fault) -- see
// daughterNotices(). Shown like the volume overlay, when nothing else is.
static NoticeOverlay g_noticeOverlay;
static bool          g_noticeShown = false;
static bool       g_menuOpen = false;
static size_t     g_screenIdxBeforeMenu = 0;

void bootInfo() {

    Serial.println("\n--- " FW_NAME " " FW_VERSION " ---");
    Serial.println("--- HARDWARE & CLOCK VALIDATION ---");

    // 1. Compile-time check
    #if defined(PICO_RP2350) && PICO_RP2350
        Serial.println("Compile Target: RP2350 Core (Pico 2)");
        
        // 2. Runtime Chip Revision via Hardware Registers
        // This confirms the exact silicon version running live on your board
        uint8_t chip_rev = (sysinfo_hw->chip_id >> 28) & 0xF; 
        Serial.print("Silicon Revision: RP2350 A");
        Serial.println(chip_rev); // e.g., 2 for A2, 4 for A4

        #if defined(__riscv) || defined(__riscv__)
                Serial.println("Arch: NATIVE RISC-V (Hazard3 Core Active!)");
        #elif defined(__arm__)
            Serial.println("Arch: NATIVE ARM (Cortex-M33 Core Active)");
        #else
            Serial.println("Arch: Unknown Core Architecture");
        #endif

    #else
        Serial.println("Compile Target: Older RP2040 Core (Pico 1)");
    #endif

    // 3. Confirm Flash ID from the core helper wrapper
    Serial.print("Unique Flash ID: ");
    Serial.println(rp2040.getChipID());

    // 4. Verify Clock Mappings (Confirming your overclock configurations)
    Serial.print("Active CPU Clock: ");
    Serial.print(rp2040.f_cpu() / 1000000);
    Serial.println(" MHz");

    Serial.print("Peripheral Clock (clk_peri): ");
    Serial.print(clock_get_hz(clk_peri) / 1000000);
    Serial.println(" MHz");
    
    Serial.print("ADC Clock (clk_adc): ");
    Serial.print(clock_get_hz(clk_adc) / 1000000);
    Serial.println(" MHz");
}

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

    bootInfo();
    
    Renderer::instance().init();
    Serial.println("[OK] Renderer initialized.");

    InputEngine::instance().init(BTN_RT_PIN, BTN_LT_PIN, IR_PIN, BTN_UP_PIN, BTN_DN_PIN);
    Serial.println("[OK] InputEngine initialized.");

    VolumeControl::instance().init();
    Serial.println("[OK] VolumeControl initialized.");
    Serial.printf("[OK] Power control: %s (IR power key toggles)\n", PowerControl::instance().isOn() ? "ON" : "STANDBY");

    RtcClock::instance().begin(); // logs its own outcome

    g_menuScreen.init(getRootMenuItems(), getRootMenuItemCount());

    // Applies any saved settings (falls back to defaults if none are
    // valid) before anything below reads them -- in particular, before
    // g_currentScreenIdx picks up the starting screen just below.
    PersistentSettings::instance().loadAndApply();

    g_currentScreenIdx = kDefaultScreenIndex; // always boots on the Clock
    g_screens[g_currentScreenIdx]->onEnter();

    MasterConsole::instance().begin();
}

// Rolling performance counters, reported to Serial once per second.
static uint32_t g_frameCount      = 0;
static uint32_t g_renderTimeSumUs = 0; // time inside render() only
static uint32_t g_loopTimeSumUs   = 0; // full loop() iteration, incl. pacing delay
static uint32_t g_lastReportMs    = 0;

// Timestamps of the last GAIN_UP/GAIN_DOWN IR events, used to treat a
// recent-enough repeat train as "still held" -- see IR_VOLUME_HOLD_MS.
static uint32_t g_lastGainUpMs   = 0;
static uint32_t g_lastGainDownMs = 0;

static void closeMenu() {
    g_menuOpen = false;
    g_currentScreenIdx = g_screenIdxBeforeMenu;
    g_screens[g_currentScreenIdx]->onEnter();
    Serial.println("[Menu] Closed");

    // Only actually touches flash if something changed while the menu was
    // open (see PersistentSettings::markDirty()) -- one commit per menu
    // session, not one per individual setting change.
    PersistentSettings::instance().saveIfDirty();
}

// Revision 13/14 ENV: while the daughter recommends a resync (RESYNC=1), the
// notice (with its reason) pops up again with each background ENV reading --
// every DAUGHTER_ENV_BACKGROUND_POLL_MS, at most -- until a calibration
// clears it: not intrusive, but insistent. Also once when the block NTC
// develops a fault (which turns the thermal compensation off).
static void daughterNotices() {
    const DaughterEnv& e = DaughterBoardLink::instance().env();
    PowerControl& power  = PowerControl::instance();
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
        g_noticeOverlay.show("CALIBRATION ADVISED", why, "press RIGHT to calibrate", COLOR_YELLOW, true);
        if (!s_resyncShown) Serial.printf("[Daughter] Resync recommended (WHY=%s)\n", e.why);
        s_resyncShown   = true;
        s_resyncShownMs = millis();
    };

    bool on = power.isOn();
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
        if (on) g_noticeOverlay.show("SENSOR FAULT", line, "check the NTC on the driver block", COLOR_RED);
        Serial.printf("[Daughter] Block NTC fault: NTC=%s -- thermal compensation off\n", e.ntc);
    }
    s_lastNtcOk = ntcOk;
}

// Auto standby (Settings -> Power, see AutoStandby): when it says so, run
// the normal standby sequence -- closing the menu / display test first.
static void autoStandbyUpdate(bool keyActivity) {
    if (!AutoStandby::instance().update(keyActivity)) return;
    Serial.printf("[Power] Auto standby: no audio or input for %u min\n",
                  DisplaySettings::instance().getAutoStandbyMinutes());
    if (g_menuOpen) closeMenu();
    g_displayTestOpen = false;
    PowerControl::instance().toggle();
}

void loop() {
    uint32_t loopStartUs = micros();

    MasterConsole::instance().update(); // USB console + daughter pass-through

    UIEvent event = InputEngine::instance().pollEvents();

    // Any key or remote press counts as activity for auto standby.
    bool keyActivity = (event != UIEvent::NONE) ||
                       InputEngine::instance().isVolUpHeld() || InputEngine::instance().isVolDownHeld();

    // --- Power / standby ---
    // The power key works everywhere. Switching off closes the menu and the
    // display test first, so standby always starts from a clean state.
    PowerControl& power = PowerControl::instance();
    if (event == UIEvent::POWER_TOGGLE) {
        if (power.isOn()) {
            if (g_menuOpen) closeMenu();
            g_displayTestOpen = false;
        }
        power.toggle();
        event = UIEvent::NONE;
    }
    power.update();
    g_powerOverlay.update();
    autoStandbyUpdate(keyActivity);
    daughterNotices();
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

    if (g_displayTestOpen) {
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
        if (event == UIEvent::MODE_PREV || event == UIEvent::MENU_TOGGLE) {
            g_displayTestOpen = false;
            g_screens[g_currentScreenIdx]->onEnter();
            Serial.println("[Diag] Display test closed");
        }
        event = UIEvent::NONE;
    }

    if (g_menuOpen) {
        // While the menu is open, MODE_NEXT/PREV/GAIN_UP/DOWN/MENU_TOGGLE
        // are all consumed by menu navigation instead of their normal
        // meanings (screen switching, volume control). Everything else
        // (MUTE_TOGGLE, CHANNEL_MODE_TOGGLE) still works normally -- only
        // the controls the menu specifically repurposes are intercepted
        // here.
        if (event == UIEvent::MENU_TOGGLE) {
            closeMenu();
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
            if (!g_menuScreen.back()) {
                closeMenu();
            }
        } else if (event == UIEvent::MUTE_TOGGLE) {
            VolumeControl::instance().toggleMute();
        } else if (event == UIEvent::CHANNEL_MODE_TOGGLE) {
            DisplaySettings::instance().toggleChannelMode();
        }

        // Physical UP/DN buttons: outside the menu they're read as held
        // levels (volume), not as discrete events pollEvents() returns.
        // While the menu is open, treat them as edge-triggered -- one
        // focus-step per physical press, same reasoning as the IR
        // repeat-filtering above -- checked independently of `event`
        // since these buttons never populate it themselves.
        bool upPressed   = InputEngine::instance().volUpJustPressed();
        bool downPressed = InputEngine::instance().volDownJustPressed();
        if (upPressed)   g_menuScreen.moveFocus(-1);
        if (downPressed) g_menuScreen.moveFocus(1);

        // In an editor, a held physical Up/Down auto-repeats too: after
        // MENU_REPEAT_DELAY_MS, one step every MENU_REPEAT_RATE_MS.
        static uint32_t s_holdStartMs = 0, s_lastRepeatMs = 0;
        int heldDir = InputEngine::instance().isVolUpHeld()   ? -1
                    : InputEngine::instance().isVolDownHeld() ?  1 : 0;
        uint32_t nowMsMenu = millis();
        if (heldDir == 0 || upPressed || downPressed) {
            s_holdStartMs = s_lastRepeatMs = nowMsMenu;
        } else if (g_menuScreen.acceptsRepeats() &&
                   nowMsMenu - s_holdStartMs >= MENU_REPEAT_DELAY_MS &&
                   nowMsMenu - s_lastRepeatMs >= MENU_REPEAT_RATE_MS) {
            g_menuScreen.moveFocus(heldDir);
            s_lastRepeatMs = nowMsMenu;
        }
    } else {
        if (event == UIEvent::MODE_NEXT && g_noticeShown && g_noticeOverlay.isResyncNotice()) {
            // RIGHT on the "CALIBRATION ADVISED" notice: straight to the
            // resync start (Settings > Calibration > Rtotal, Cancel / Start).
            g_menuOpen = true;
            g_noticeOverlay.dismiss();
            g_volumeOverlay.dismiss();
            g_overlayShown = false;
            g_noticeShown  = false;
            g_screenIdxBeforeMenu = g_currentScreenIdx;
            g_menuScreen.onEnter();
            menuOpenResyncStart(g_menuScreen);
            Serial.println("[Menu] Opened at the calibration start (from the notice)");
        } else if (event == UIEvent::MODE_NEXT || event == UIEvent::MODE_PREV) {
            g_currentScreenIdx = (event == UIEvent::MODE_NEXT)
                                      ? (g_currentScreenIdx + 1) % kNumScreens
                                      : (g_currentScreenIdx + kNumScreens - 1) % kNumScreens;

            g_volumeOverlay.dismiss();
            g_overlayShown = false;
            g_noticeShown = false;
            g_screens[g_currentScreenIdx]->onEnter();
            Serial.printf("[Display] Screen switched to %s\n", g_screens[g_currentScreenIdx]->name());
        } else if (event == UIEvent::MUTE_TOGGLE) {
            VolumeControl::instance().toggleMute();
            g_volumeOverlay.poke();
            Serial.printf("[Volume] Mute toggled -> %s\n",
                           VolumeControl::instance().isMuted() ? "MUTED" : "UNMUTED");
        } else if (event == UIEvent::CHANNEL_MODE_TOGGLE) {
            DisplaySettings::instance().toggleChannelMode();
            g_volumeOverlay.dismiss();
            g_overlayShown = false;
            g_noticeShown = false;
            // Channel mode changes a screen's whole layout (bar count/
            // width, trace split), so force the current screen to
            // re-enter and rebuild its geometry rather than continuing
            // with stale state from the previous mode.
            g_screens[g_currentScreenIdx]->onEnter();
            Serial.printf("[Display] Channel mode switched to %s\n",
                           DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO ? "STEREO" : "MONO");
        } else if (event == UIEvent::MENU_TOGGLE) {
            g_menuOpen = true;
            g_volumeOverlay.dismiss();
            g_overlayShown = false;
            g_noticeShown = false;
            g_screenIdxBeforeMenu = g_currentScreenIdx;
            g_menuScreen.onEnter();
            Serial.println("[Menu] Opened");
        } else if (event == UIEvent::GAIN_UP) {
            g_lastGainUpMs = millis();
        } else if (event == UIEvent::GAIN_DOWN) {
            g_lastGainDownMs = millis();
        } else if (event != UIEvent::NONE) {
            Serial.printf("[Input] Event ID: %d\n", static_cast<int>(event));
        }
    }

    // Volume control: forced false while the menu is open, regardless of
    // actual button states, so navigating the menu (which repurposes the
    // same physical volume buttons) never also changes real volume.
    // VolumeControl::update() is still called every loop either way, since
    // it must keep servicing the daughter board link regardless.
    // The Display Test menu action fires inside the menu's enter() above.
    if (DisplayTestScreen::consumeStartRequest()) {
        if (g_menuOpen) closeMenu();
        g_volumeOverlay.dismiss();
        g_overlayShown    = false;
        g_noticeShown = false;
        g_displayTestOpen = true;
        g_displayTest.onEnter();
        Serial.println("[Diag] Display test opened");
    }

    bool wantVolUp = false, wantVolDown = false;
    if (power.isOn() && !g_menuOpen && !g_displayTestOpen) {
        uint32_t nowMsForVolume = millis();
        wantVolUp   = InputEngine::instance().isVolUpHeld()
                        || (nowMsForVolume - g_lastGainUpMs < IR_VOLUME_HOLD_MS);
        wantVolDown = InputEngine::instance().isVolDownHeld()
                        || (nowMsForVolume - g_lastGainDownMs < IR_VOLUME_HOLD_MS);
    }
    VolumeControl::instance().update(wantVolUp, wantVolDown);

    // Volume overlay: any held volume input keeps it up (the daughter's own
    // VOL/DB arrive a few ms later via STATUS and update the readout);
    // update() also pops it for changes seen in STATUS and expires it.
    if (wantVolUp || wantVolDown) g_volumeOverlay.poke();
    g_volumeOverlay.update();
    if (g_menuOpen || g_displayTestOpen) g_volumeOverlay.dismiss();

    // What the power layer wants on the panel: its popup (sequence or hint),
    // the standby display, or nothing (normal operation).
    PowerView wantPower = PowerView::NONE;
    if (g_powerOverlay.isActive()) {
        wantPower = PowerView::POPUP;
    } else if (!power.isOn()) {
        wantPower = (DisplaySettings::instance().getStandbyDisplay() == StandbyDisplay::OFF)
                        ? PowerView::STANDBY_DARK : PowerView::STANDBY_CLOCK;
    }
    if (wantPower != PowerView::NONE) {
        // The power layer owns the panel: the volume overlay just stops (no
        // re-entering the screen underneath, which isn't shown).
        g_volumeOverlay.dismiss();
        g_overlayShown = false;
        g_noticeShown = false;
    }
    if (wantPower != g_powerView) {
        uint8_t brightness = DisplaySettings::instance().getBrightnessPercent();
        switch (wantPower) {
            case PowerView::POPUP:
                Renderer::instance().setBacklightPercent(brightness);
                g_powerOverlay.onEnter();
                break;
            case PowerView::STANDBY_CLOCK:
                Renderer::instance().setBacklightPercent(
                    DisplaySettings::instance().getStandbyDisplay() == StandbyDisplay::DIM_CLOCK ? STANDBY_DIM_PERCENT : brightness);
                g_clockScreen.onEnter();
                break;
            case PowerView::STANDBY_DARK:
                Renderer::instance().clear();
                Renderer::instance().setBacklightPercent(0);
                break;
            case PowerView::NONE:
                // Back to normal: the screen the user had (menu and test
                // were closed on the way into standby).
                Renderer::instance().setBacklightPercent(brightness);
                g_screens[g_currentScreenIdx]->onEnter();
                break;
        }
        g_powerView = wantPower;
    }

    if (g_powerView == PowerView::NONE) {
        // The volume overlay wins over a notice; a notice waits while the
        // menu or the display test is open (and is dropped if it runs out).
        bool wantOverlay = g_volumeOverlay.isActive();
        bool wantNotice  = !wantOverlay && g_noticeOverlay.isActive() && !g_menuOpen && !g_displayTestOpen;
        bool wasShowing  = g_overlayShown || g_noticeShown;
        if (wantOverlay && !g_overlayShown) {
            g_volumeOverlay.onEnter();
        } else if (wantNotice && !g_noticeShown) {
            g_noticeOverlay.onEnter();
        } else if (!wantOverlay && !wantNotice && wasShowing && !g_menuOpen && !g_displayTestOpen) {
            // The overlay painted over the whole panel -- let the screen
            // rebuild from scratch rather than trust its delta-redraw state.
            g_screens[g_currentScreenIdx]->onEnter();
        }
        if (wantOverlay && g_noticeShown) g_noticeOverlay.dismiss(); // the volume readout takes over
        g_overlayShown = wantOverlay;
        g_noticeShown  = wantNotice;
    }

    uint32_t renderStartUs = micros();

    if (g_powerView == PowerView::POPUP) {
        g_powerOverlay.render();
    } else if (g_powerView == PowerView::STANDBY_CLOCK) {
        g_clockScreen.render();
    } else if (g_powerView == PowerView::STANDBY_DARK) {
        // nothing to draw -- backlight off
    } else if (g_displayTestOpen) {
        g_displayTest.render();
    } else if (g_menuOpen) {
        g_menuScreen.render();
    } else if (g_overlayShown) {
        g_volumeOverlay.render();
    } else if (g_noticeShown) {
        g_noticeOverlay.render();
    } else {
        g_screens[g_currentScreenIdx]->render();
    }

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
    g_frameCount++;
    g_renderTimeSumUs += renderUs;
    g_loopTimeSumUs   += loopUs;

    uint32_t nowMs = millis();
    if (nowMs - g_lastReportMs >= FPS_REPORT_INTERVAL_MS) {
        float elapsedMs    = static_cast<float>(nowMs - g_lastReportMs);
        float fps          = (g_frameCount * 1000.0f) / elapsedMs;
        float avgRenderMs  = (g_renderTimeSumUs / 1000.0f) / g_frameCount;
        float avgLoopMs    = (g_loopTimeSumUs   / 1000.0f) / g_frameCount;

        if (DEBUG_PERF && !DaughterBoardLink::instance().isPassthrough()) { // quiet during a pass-through session
            Serial.print("[Perf] FPS: ");
            Serial.print(fps, 1);
            Serial.print(" | render: ");
            Serial.print(avgRenderMs, 2);
            Serial.print("ms avg | loop: ");
            Serial.print(avgLoopMs, 2);
            Serial.print("ms avg | screen: ");
            Serial.print(g_powerView == PowerView::POPUP ? g_powerOverlay.name()
                         : g_powerView == PowerView::STANDBY_CLOCK ? "STANDBY CLOCK"
                         : g_powerView == PowerView::STANDBY_DARK ? "STANDBY (display off)"
                         : g_displayTestOpen ? g_displayTest.name()
                         : g_menuOpen ? g_menuScreen.name()
                         : g_overlayShown ? g_volumeOverlay.name()
                         : g_noticeShown ? g_noticeOverlay.name()
                         : g_screens[g_currentScreenIdx]->name());

            // RTC time -- one short I2C read, once a second.
            RtcDateTime now;
            if (RtcClock::instance().read(now)) {
                char timeBuf[20];
                RtcClock::format(now, timeBuf, sizeof(timeBuf));
                Serial.print(" | time: ");
                Serial.print(timeBuf);
            }
            Serial.println();
        }


        g_frameCount      = 0;
        g_renderTimeSumUs = 0;
        g_loopTimeSumUs   = 0;
        g_lastReportMs    = nowMs;
    }
}