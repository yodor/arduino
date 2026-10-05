#pragma once
#include "AmpHold.hpp"
#include <Arduino.h>
#include <stdio.h>

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

  // Non-blocking. Call every loop() iteration -- currently only handles
  // the one-shot boot-mute auto-release (see setAmpMute). The diagnostic
  // LED no longer blinks here (see setDiagLedEnabled).
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

  // Calibration mute hold: while at least one CalHold is alive the amp is held
  // MUTED, whatever its state was and whatever the boot hold is doing. During a
  // calibration the audio path is disconnected anyway, so muting costs nothing
  // and keeps the floating amp input, relay clicks and PWM activity out of the
  // speakers. When the last hold ends the amp goes back to what it was (see
  // AmpHold.hpp); a boot-time calibration leaves it muted and Board::update()'s
  // one-shot auto-release opens it once the hold has expired and nothing is
  // running. While a hold is active setAmpMute(true) is a no-op that succeeds
  // and setAmpMute(false) is refused.
  class CalHold {
  public:
    explicit CalHold(Board &b) : b_(b) { b_.calHoldBegin(); }
    ~CalHold() { b_.calHoldEnd(); }
    CalHold(const CalHold &) = delete;
    CalHold &operator=(const CalHold &) = delete;
  private:
    Board &b_;
  };
  bool calHoldActive() const { return hold_.active(); }

  // Milliseconds remaining in the boot hold, 0 once it's expired. Useful
  // for an ERR message's "<n>ms remaining" detail.
  unsigned long bootHoldRemainingMs() const;

  // Bench diagnostic LED: steady on/off, no blinking -- confirmed on real
  // hardware that the GPIO toggling itself couples audible noise into the
  // audio path, so there's no heartbeat behavior to preserve; this is
  // just a plain indicator you explicitly turn on when you want it.
  void setDiagLedEnabled(bool enabled);
  bool diagLedEnabled() const { return diagLedEnabled_; }

  // Called by LDRVolume::relayEnergize() on every transition (true =
  // energized, false = de-energized) so Board knows whether a
  // calibration/relay operation is currently in flight on either
  // channel. The boot-hold auto-release (see update()) waits for this
  // to clear in addition to the timer -- otherwise a fresh (uncalibrated)
  // board whose first characterization sweep runs past the hold window
  // would have the amp auto-unmute straight into an active sweep.
  void setRelayActive(bool active);
  bool anyRelayActive() const { return relayActiveCount_ > 0; }

  // --- MCU facts: temperature and uptime ----------------------------------------
  // Static: they describe the chip, not any one Board instance, so any code
  // (calibration, the protocol, the console) can call them without plumbing.
  //
  // The RP2040's on-die temperature, a PROXY for the board temperature. Why it is
  // here: the LED driver stages (NPN Vbe, PNP V_EB, a 100k bleed) drift with
  // temperature -- measured cold-start vs warm, the duty needed for a given
  // resistance moved ~11-14 counts at the quiet end -- so the temperature is
  // shown next to every calibration/trim and in STATUS, letting duty-vs-degC be
  // fitted. It is the DIE temperature: a few degrees above the board, it self-
  // heats a little under CPU load (read it at idle -- the calibration code does),
  // and its absolute accuracy is only ~+/-2 degC. Look at CHANGES, not the level.
  // Average of 16 conversions (~0.1 degC resolution), degrees Celsius.
  static float dieTempC();

  // Whole seconds since power-up or reset (millis()/1000). There is no wall
  // clock on the daughter, and for warm-up analysis time-since-power-on is the
  // number that matters. Wraps after ~49.7 days of continuous running.
  static uint32_t uptimeSeconds();

  // "HH:MM:SS" into buf (hours may exceed 24, and 99). Returns buf.
  static char *formatHms(uint32_t seconds, char *buf, size_t len) {
    snprintf(buf, len, "%02lu:%02lu:%02lu", (unsigned long)(seconds / 3600UL),
             (unsigned long)((seconds / 60UL) % 60UL), (unsigned long)(seconds % 60UL));
    return buf;
  }

  // Prints "<label>: 31.4 C (RP2040 die)  uptime 01:02:05 (3725 s)" on its own
  // line: the two numbers needed to log a warm-up curve, always together. If
  // elapsedMs is non-zero, "  took 39.4 s" is appended -- the end-of-calibration
  // lines use it so temperature, uptime and total duration read as one record.
  static void reportDieTemp(Stream &out, const __FlashStringHelper *label, uint32_t elapsedMs = 0);

private:
  void bootInfo();
  void calHoldBegin();
  void calHoldEnd();
  void writeAmpMute(bool muted); // pin + ampMuted_, no gating
  AmpHold hold_;

  unsigned long bootMillis_ = 0;
  bool ampMuted_ = true;        // matches the boot pre-latch -- starts muted
  bool diagLedEnabled_ = false; // off by default -- DIAGLED ON to enable for bring-up/debug
  uint8_t relayActiveCount_ = 0; // how many channels currently have their relay energized
  bool bootHoldReleased_ = false; // one-shot: has the auto-unmute already fired?
};