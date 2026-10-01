#pragma once
#include <Arduino.h>
#include <Wire.h>

// ============================================================================
// Ads1115
//
// Minimal register-level driver for one ADS1115 on a given TwoWire bus.
// Deliberately not wrapping a third-party library: the LDR board's
// measurement scheme needs mux code 0b010 (differential AIN1-AIN3), which
// most Arduino ADS1115 libraries only expose via a raw config write, not a
// named convenience function -- so raw register access is simpler here
// than fighting a wrapper for the one code path that matters most.
// ============================================================================
class Ads1115 {
public:
  enum Mux : uint16_t {
    MUX_AIN0_AIN1 = 0x0000, // differential AIN0-AIN1
    MUX_AIN0_AIN3 = 0x1000, // differential AIN0-AIN3
    MUX_AIN1_AIN3 = 0x2000, // differential AIN1-AIN3
    MUX_AIN2_AIN3 = 0x3000, // differential AIN2-AIN3
    MUX_AIN0_GND  = 0x4000, // single-ended AIN0
    MUX_AIN1_GND  = 0x5000, // single-ended AIN1
    MUX_AIN2_GND  = 0x6000, // single-ended AIN2
    MUX_AIN3_GND  = 0x7000, // single-ended AIN3
  };

  enum Gain : uint16_t {
    GAIN_TWOTHIRDS = 0x0000, // +/-6.144V
    GAIN_ONE       = 0x0200, // +/-4.096V
    GAIN_TWO       = 0x0400, // +/-2.048V
    GAIN_FOUR      = 0x0600, // +/-1.024V
    GAIN_EIGHT     = 0x0800, // +/-0.512V
    GAIN_SIXTEEN   = 0x0A00, // +/-0.256V
  };

  // Stores the bus/address; does not touch the bus itself (caller is
  // expected to have already called wire.begin(), e.g. via LdrSensor::begin()).
  bool begin(TwoWire &wire, uint8_t addr = 0x48);

  // Blocking single-shot conversion. Polls the config register's OS bit
  // for completion (should take ~1.2ms at the 860SPS rate used here)
  // rather than a fixed delay, with a 10ms timeout so a wiring fault
  // can't hang the caller.
  int16_t readRaw(Mux mux, Gain gain);

  // One conversion at the widest range to estimate magnitude, then a
  // second at the best-fitting gain -- two conversions' worth of latency
  // (a few ms), far better resolution than one fixed-gain read across a
  // range that can span more than two orders of magnitude.
  float readVoltageAutoRange(Mux mux);

  static float rangeForGain(Gain gain);
  static Gain gainForRange(float expectedVolts);

  // Cheap presence check: does addr_ ACK on the bus right now? No
  // conversion is started -- just the I2C address phase. Caller is
  // responsible for the bus already being active (wire.begin() called).
  bool isPresent();

  // Prints every address that ACKs on the given bus. Bring-up utility --
  // confirms the module is actually present/wired before trusting any
  // reading from it.
  static void scanBus(TwoWire &wire);

private:
  TwoWire *wire_ = nullptr;
  uint8_t addr_ = 0x48;

  bool writeConfig(uint16_t cfg);
  uint16_t readRegister16(uint8_t reg);
};