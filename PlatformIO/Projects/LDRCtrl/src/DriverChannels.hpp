#pragma once
#include <Arduino.h>

// ============================================================================
// DriverChannels
//
// Direct PWM control for the four LED driver channels feeding the two LDR
// boards' series/shunt vactrol LEDs (SHUNT_L, SER_L on the left board's
// i2c0 bus; SHUNT_R, SER_R on the right board's i2c1 bus). Pulled out of
// main.cpp so the driver-stage logic (proven on the breadboard against the
// real NSL-32SR3 driver topology) is reusable once calibration and the
// UART protocol land on top of it, instead of being tangled into the
// bring-up test's command parser.
//
// Uses the Pico SDK's hardware/pwm.h directly rather than analogWrite(),
// for explicit control of wrap value and clock divider -- this class
// itself hardcodes no GPIO numbers; all four pins come from Pins.hpp, so
// a future pin remap only ever touches that one file.
// ============================================================================
class DriverChannels {
public:
  enum Channel : uint8_t {
    SHUNT_L = 0,
    SER_L,
    SHUNT_R,
    SER_R,
    NUM_CHANNELS
  };

  static constexpr uint16_t WRAP = 4095;  // 12-bit PWM
  static constexpr float CLKDIV = 1.0f;   // fastest achievable divider at WRAP=4095

  // Configures all four channels' GPIOs for PWM output and starts each at
  // duty=0 (dark). Call once from setup().
  void begin();

  // Raw duty, 0..WRAP. Values above WRAP are clamped rather than wrapping.
  void setDuty(Channel ch, uint16_t duty);
  void setDutyAll(uint16_t duty);
  uint16_t getDuty(Channel ch) const;

  // Actual PWM frequency, computed from the live system clock rather than
  // a comment -- stays correct across CPU_SPEED_KHZ changes in Config.hpp.
  // NOTE: assumes CLKDIV is a whole number (currently 1.0); revisit this
  // if CLKDIV ever needs a fractional value.
  static uint32_t frequencyHz();

  // Name <-> Channel lookup, for parsing serial commands.
  static const char *name(Channel ch);
  static int find(const String &token); // channel name, or plain index "0".."3"; -1 if no match
  static uint8_t pinFor(Channel ch);

private:
  uint16_t duty_[NUM_CHANNELS] = {0, 0, 0, 0};

  static const uint8_t PINS[NUM_CHANNELS];
  static const char *const NAMES[NUM_CHANNELS];
};