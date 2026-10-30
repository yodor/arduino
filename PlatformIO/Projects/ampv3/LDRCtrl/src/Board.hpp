#pragma once
#include "AmpHold.hpp"
#include <Arduino.h>
#include "BoardSensors.hpp"
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

  // Non-blocking. Call every loop() iteration -- handles the one-shot
  // boot-mute auto-release (see setAmpMute). The onboard LED is held off
  // (toggling that GPIO couples audible noise into the audio path).
  void update();

  // Direct control of the amp's own mute circuit (4N25 opto, AMP_MUTE_PIN)
  // -- entirely independent of any LDRVolume's own mute() -- either can
  // be engaged regardless of the other's state.
  //
  // Power-up: the amp mute is latched ON in begin() and changes only on command -- the
  // controller decides the power state (protocol 10; there is no timed boot hold any more).
  // Returns false only for an unmute asked while a calibration holds the amp muted.
  bool setAmpMute(bool muted);
  bool ampMuted() const { return ampMuted_; }
  // The amp state as COMMANDED by the controller or the console (AMP_MUTE), not the firmware's
  // own brief holds around calibrations: muted = standby. For the resync hint (warm-up).
  static void noteAmpCommand(bool muted);
  static bool ampCommandedMuted();
  static uint32_t msSinceAmpOn();          // since the last commanded unmute (0 while muted)

  // Calibration mute hold: while at least one CalHold is alive the amp is held MUTED,
  // whatever its state was. During a calibration the audio path is disconnected anyway, so
  // muting costs nothing and keeps the floating amp input, relay clicks and PWM activity out
  // of the speakers. When the last hold ends the amp goes back to what it was before (see
  // AmpHold.hpp): a calibration started from power-up (amp muted) leaves it muted. While a
  // hold is active setAmpMute(true) is a no-op that succeeds and setAmpMute(false) is refused.
  class CalHold {
  public:
    explicit CalHold(Board &b) : b_(b) { b_.calHoldBegin(); }
    ~CalHold() { b_.calHoldEnd(); }
    CalHold(const CalHold &) = delete;
    CalHold &operator=(const CalHold &) = delete;
  private:
    Board &b_;
  };



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
  // v1b driver board sensors (BoardSensors.hpp): the aluminium block's NTC in C, and the
  // 3.3 V rail via the TL431, in volts. NAN when the sensor is missing or implausible.
  static float blockTempC();
  static BoardSensors::NtcState ntcState();   // OK / OPEN / SHORT / BAD
  static float railVolts();

  // Whole seconds since power-up or reset (millis()/1000). There is no wall
  // clock on the attenuator, and for warm-up analysis time-since-power-on is the
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
  uint8_t relayActiveCount_ = 0; // how many channels currently have their relay energized
};
