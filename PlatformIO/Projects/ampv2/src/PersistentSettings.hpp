#pragma once
#include <Arduino.h>

// Persists the menu-driven settings (color preset, channel mode, starting
// screen, stereo layout, mirror-left-bars, FFT band layout, FFT energy
// mode) across reboots, stored in the last flash sector.
//
// Deliberately does NOT use Arduino-Pico's EEPROM library. That library's
// commit() internally calls rp2040.idleOtherCore() to pause core1 during
// the flash erase/write (RP-series chips can't execute from flash while
// it's being modified), and that pausing mechanism has multiple
// documented hangs specifically when the OTHER core is genuinely busy
// (github.com/earlephilhower/arduino-pico issues #719, #1561, #1745,
// #2485) -- exactly our situation, since core1 runs a continuously-busy
// audio loop with no natural idle points. The library maintainer has
// acknowledged this as an open architectural gap (discussion #83): "If
// either core tries to update the flash... the whole board will crash
// hard... we need an interlock between both cores... not something I've
// had time to hack on."
//
// Instead, this builds a small COOPERATIVE interlock of our own: core0
// (about to write) sets a flag; core1 checks it once per main loop
// iteration (AudioEngine::runCore1(), at a safe point between chunk
// dispatches) and, if set, parks itself in a tight spin loop that lives
// entirely in RAM (checkAndPauseForFlashWrite() is marked
// __not_in_flash_func for exactly this reason -- it must not fetch any
// instruction from flash while core0 is mid-erase/program, so it cannot
// call anything else that lives in flash, not even Serial.print). Core0
// waits for that confirmation, then calls the Pico SDK's
// flash_range_erase()/flash_range_program() directly (bypassing the
// EEPROM library's own commit() and its idleOtherCore() call entirely),
// then releases core1.
//
// Saves are triggered by markDirty() (called from every menu setter) plus
// saveIfDirty() (called once, when the menu closes) -- not a periodic
// debounce timer, so a flash write only ever happens right after the user
// finishes adjusting settings and steps away from the menu.
class PersistentSettings {
public:
    static PersistentSettings& instance();

    // Called once at boot (core0's setup(), before anything reads the
    // settings singletons) -- if the stored magic/version/checksum are
    // valid, applies each field to its owning singleton (DisplaySettings,
    // ColorTheme, AudioEngine/SpectrumPipeline); otherwise leaves current
    // (default) values untouched and logs why.
    void loadAndApply();

    // Called by every menu setter that changes a persisted setting.
    void markDirty() { m_dirty = true; }

    // Called when the menu closes (main.cpp's closeMenu()) -- commits to
    // flash via the interlocked raw-flash-write path if anything changed
    // since the last save; a no-op otherwise.
    void saveIfDirty();

    // Called from AudioEngine::runCore1()'s loop, once per iteration at a
    // safe checkpoint between chunk dispatches. MUST remain a true RAM-
    // resident leaf -- see this method's definition for the constraints
    // that follow from that.
    static void checkAndPauseForFlashWrite();

private:
    PersistentSettings() = default;
    void commitToFlash();

    bool m_dirty = false;
};