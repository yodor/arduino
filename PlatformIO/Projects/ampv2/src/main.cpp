#include <Arduino.h>
#include "Config.hpp"
#include "AudioEngine.hpp"
#include "InputEngine.hpp"
#include "Renderer.hpp"
#include "VolumeControl.hpp"
#include "Screen.hpp"
#include "BarSpectrumScreen.hpp"
#include "WaveformScreen.hpp"
#include "AnalogVuScreen.hpp"
#include "DigitalVuMeterScreen.hpp"
#include "OctaveScreen.hpp"
#include "DisplaySettings.hpp"
#include "ColorTheme.hpp"
#include "MenuScreen.hpp"
#include "MenuContent.hpp"
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
static BarSpectrumScreen     g_barSpectrumScreen;
static WaveformScreen        g_waveformScreen;
static AnalogVuScreen        g_analogVuScreen;
static DigitalVuMeterScreen  g_digitalVuMeterScreen;
static OctaveScreen          g_octaveScreen;

static Screen* g_screens[] = {
    &g_barSpectrumScreen,
    &g_waveformScreen,
    &g_analogVuScreen,
    &g_digitalVuMeterScreen,
    &g_octaveScreen,
};
constexpr size_t kNumScreens = sizeof(g_screens) / sizeof(g_screens[0]);

static_assert(kNumScreens == kScreenCount,
              "g_screens[] here and kScreenNames[] in ScreenRegistry.hpp must have the same number of "
              "entries, in the same order -- MenuContent.cpp's Starting Screen options select by index "
              "into g_screens[] but only know about it via kScreenNames[]/kScreenCount");

static size_t g_currentScreenIdx = 0;

static MenuScreen g_menuScreen;
static bool       g_menuOpen = false;
static size_t     g_screenIdxBeforeMenu = 0;

void setup() {
    if (CUSTOM_CLOCKS_ENABLED) {
        // 1. Overclock the RP2040 core
        set_sys_clock_khz(CPU_SPEED_KHZ, true);

        // 2. Explicitly bind the peripheral clock back to the system clock.
        // This scales clk_peri to 240 MHz, unlocking your SPI dividers!
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

    // Force the internal SMPS out of PFM mode into low-noise PWM mode
    pinMode(REGULATOR_MODE_PIN, OUTPUT);
    digitalWrite(REGULATOR_MODE_PIN, HIGH);
    
    uint32_t timeout = millis();
    while (!Serial && (millis() - timeout < 3000)) {
        delay(10);
    }

    Serial.println("\n=== RP2040 Booting ===");

    Renderer::instance().init();
    Serial.println("[OK] Renderer initialized.");

    InputEngine::instance().init(BTN_RT_PIN, BTN_LT_PIN, IR_PIN, BTN_UP_PIN, BTN_DN_PIN);
    Serial.println("[OK] InputEngine initialized.");

    VolumeControl::instance().init();
    Serial.println("[OK] VolumeControl initialized.");

    g_menuScreen.init(getRootMenuItems(), getRootMenuItemCount());

    g_currentScreenIdx = DisplaySettings::instance().getStartingScreenIndex();
    if (g_currentScreenIdx >= kNumScreens) g_currentScreenIdx = 0; // safety clamp
    g_screens[g_currentScreenIdx]->onEnter();
}

// Rolling performance counters, reported to Serial once per second.
static uint32_t g_frameCount      = 0;
static uint32_t g_renderTimeSumUs = 0; // time inside render() only
static uint32_t g_loopTimeSumUs   = 0; // full loop() iteration, incl. pacing delay
static uint32_t g_lastReportMs    = 0;

// Timestamps of the last GAIN_UP/GAIN_DOWN IR events, used to treat a
// recent-enough repeat train as "still held" -- see MOTOR_COMMAND_TIMEOUT_MS.
static uint32_t g_lastGainUpMs   = 0;
static uint32_t g_lastGainDownMs = 0;

static void closeMenu() {
    g_menuOpen = false;
    g_currentScreenIdx = g_screenIdxBeforeMenu;
    g_screens[g_currentScreenIdx]->onEnter();
    Serial.println("[Menu] Closed");
}

void loop() {
    uint32_t loopStartUs = micros();

    UIEvent event = InputEngine::instance().pollEvents();

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
            if (!InputEngine::instance().lastIrWasRepeat()) {
                g_menuScreen.moveFocus(-1);
            }
        } else if (event == UIEvent::GAIN_DOWN) {
            if (!InputEngine::instance().lastIrWasRepeat()) {
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

        // Physical UP/DN buttons: these were never wired to reach the menu
        // at all before (they only ever drove continuous motor control,
        // never generated a discrete event pollEvents() could return).
        // While the menu is open, treat them as edge-triggered -- one
        // focus-step per physical press, same reasoning as the IR
        // repeat-filtering above -- checked independently of `event`
        // since these buttons never populate it themselves.
        if (InputEngine::instance().volUpJustPressed())   g_menuScreen.moveFocus(-1);
        if (InputEngine::instance().volDownJustPressed()) g_menuScreen.moveFocus(1);
    } else {
        if (event == UIEvent::MODE_NEXT || event == UIEvent::MODE_PREV) {
            g_currentScreenIdx = (event == UIEvent::MODE_NEXT)
                                      ? (g_currentScreenIdx + 1) % kNumScreens
                                      : (g_currentScreenIdx + kNumScreens - 1) % kNumScreens;

            g_screens[g_currentScreenIdx]->onEnter();
            Serial.printf("[Display] Screen switched to %s\n", g_screens[g_currentScreenIdx]->name());
        } else if (event == UIEvent::MUTE_TOGGLE) {
            VolumeControl::instance().toggleMute();
            Serial.printf("[Volume] Mute toggled -> %s\n",
                           VolumeControl::instance().isMuted() ? "MUTED" : "UNMUTED");
        } else if (event == UIEvent::CHANNEL_MODE_TOGGLE) {
            DisplaySettings::instance().toggleChannelMode();
            // Channel mode changes a screen's whole layout (bar count/
            // width, trace split), so force the current screen to
            // re-enter and rebuild its geometry rather than continuing
            // with stale state from the previous mode.
            g_screens[g_currentScreenIdx]->onEnter();
            Serial.printf("[Display] Channel mode switched to %s\n",
                           DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO ? "STEREO" : "MONO");
        } else if (event == UIEvent::MENU_TOGGLE) {
            g_menuOpen = true;
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
    // it must keep servicing the daughter board link/motor regardless.
    bool wantVolUp = false, wantVolDown = false;
    if (!g_menuOpen) {
        uint32_t nowMsForVolume = millis();
        wantVolUp   = InputEngine::instance().isVolUpHeld()
                        || (nowMsForVolume - g_lastGainUpMs < MOTOR_COMMAND_TIMEOUT_MS);
        wantVolDown = InputEngine::instance().isVolDownHeld()
                        || (nowMsForVolume - g_lastGainDownMs < MOTOR_COMMAND_TIMEOUT_MS);
    }
    VolumeControl::instance().update(wantVolUp, wantVolDown);

    uint32_t renderStartUs = micros();

    if (g_menuOpen) {
        g_menuScreen.render();
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

        Serial.print("[Perf] FPS: ");
        Serial.print(fps, 1);
        Serial.print(" | render: ");
        Serial.print(avgRenderMs, 2);
        Serial.print("ms avg | loop: ");
        Serial.print(avgLoopMs, 2);
        Serial.print("ms avg | screen: ");
        Serial.println(g_menuOpen ? g_menuScreen.name() : g_screens[g_currentScreenIdx]->name());

        g_frameCount      = 0;
        g_renderTimeSumUs = 0;
        g_loopTimeSumUs   = 0;
        g_lastReportMs    = nowMs;
    }
}