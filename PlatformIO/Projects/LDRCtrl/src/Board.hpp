#pragma once
#include <Arduino.h>

// ============================================================================
// Board
//
// Everything that's about bringing the controller board itself up, rather
// than about LED driving or calibration: mute pre-latch, forcing the local
// SMPS out of PFM mode, the CPU/peripheral clock reconfiguration, boot-time
// diagnostics, and the diagnostic LED heartbeat. Pulled out of main.cpp so
// setup()/loop() stay thin.
// ============================================================================
class Board {
public:
  // Starts Serial (with a bounded wait for USB CDC enumeration), does the
  // mute/regulator pre-latch, applies the clock configuration from
  // Config.hpp, and prints the boot diagnostics block. Call once from setup().
  void begin();

  // Non-blocking. Call every loop() iteration. Toggles the diagnostic LED
  // roughly every DIAG_LED_BLINK_MS -- a simple "firmware is alive and
  // loop() isn't stuck" heartbeat.
  void update();

  // Direct control of the amp's own mute circuit (4N25 opto, AMP_MUTE_PIN)
  // -- entirely independent of any LDRVolume's own mute() -- either can
  // be engaged regardless of the other's state.
  //
  // Enforces Config.hpp's documented MUTE_BOOT_HOLD_MS as a true gate:
  // during that window, BOTH directions (mute and unmute) are ignored --
  // the pin never moves no matter what's requested, returning false so
  // the caller can reply accordingly (the daughter should still reply
  // to the master -- just reporting "ignored," not silently dropping
  // the reply itself, which would just trade a stuck pin for a stuck
  // protocol timeout). Once the hold expires, the pin auto-releases to
  // unmuted on its own (see update()) -- no command required -- and
  // every explicit call works normally from then on.
  bool setAmpMute(bool muted);
  bool ampMuted() const { return ampMuted_; }

  // Milliseconds remaining in the boot hold, 0 once it's expired. Useful
  // for an ERR message's "<n>ms remaining" detail.
  unsigned long bootHoldRemainingMs() const;

private:
  void bootInfo();

  unsigned long lastBlinkMs_ = 0;
  bool ledOn_ = false;
  unsigned long bootMillis_ = 0;
  bool ampMuted_ = true;        // matches the boot pre-latch -- starts muted
  bool bootHoldReleased_ = false; // one-shot: has the auto-unmute already fired?
};