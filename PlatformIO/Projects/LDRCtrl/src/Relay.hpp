#pragma once
#include <Arduino.h>

// ============================================================================
// Relay
//
// One LDR board's calibration relay (K1), driven low-side through an
// N-channel FET gated from a GPIO (COIL_CTRL_L / COIL_CTRL_R in Pins.hpp).
//
// Polarity, confirmed against the actual driver netlist (FET drain->GNDD,
// source->coil node): GPIO HIGH turns the FET on, pulling the coil's low
// side to GND and energizing the relay -> CALIBRATION mode (network
// disconnected from AUDIO_IN/OUT, connected to ADS_VDD/Rref instead).
// GPIO LOW de-energizes -> normal AUDIO (fail-safe) mode.
// ============================================================================
class Relay {
public:
  explicit Relay(uint8_t pin) : pin_(pin) {}

  // Configures the pin and leaves the relay de-energized (audio mode) --
  // the safe default on every boot/reset.
  void begin();

  // true = energize (calibration mode), false = de-energize (audio mode).
  void energize(bool on);
  bool isEnergized() const { return energized_; }

private:
  uint8_t pin_;
  bool energized_ = false;
};