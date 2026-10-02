#include "DriverChannels.hpp"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "Pins.hpp"

// Channel order must match the DriverChannels::Channel enum.
const uint8_t DriverChannels::PINS[DriverChannels::NUM_CHANNELS] = {
  PWM_SHUNT_L,
  PWM_SER_L,
  PWM_SHUNT_R,
  PWM_SER_R,
};

const char *const DriverChannels::NAMES[DriverChannels::NUM_CHANNELS] = {
  "SHUNT_L",
  "SER_L",
  "SHUNT_R",
  "SER_R",
};

void DriverChannels::begin() {
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    uint8_t pin = PINS[i];
    gpio_set_function(pin, GPIO_FUNC_PWM);

    // Deliberately slow the pad's own edge rate (dv/dt), independent of
    // PWM carrier frequency entirely -- testing whether the audible noise
    // is driven by edge SPEED (e.g. capacitive coupling through the
    // NSL-32SR3's own LED-to-photocell isolation barrier, which scales
    // with dv/dt regardless of switching rate) rather than edge RATE
    // (which the earlier 10-bit/146kHz change addressed instead, and
    // evidently didn't fully resolve). Confirmed by mute() -- which
    // halts switching entirely while current still flows -- eliminating
    // the noise completely, ruling out anything current/brightness-
    // driven and pointing specifically at the transitions themselves.
    gpio_set_slew_rate(pin, GPIO_SLEW_RATE_SLOW);
    gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_2MA);

    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_wrap(slice, WRAP);
    pwm_set_clkdiv(slice, CLKDIV);
    pwm_set_gpio_level(pin, 0); // start dark
    pwm_set_enabled(slice, true);
    duty_[i] = 0;
  }
}

void DriverChannels::setDuty(Channel ch, uint16_t duty) {
  if (ch >= NUM_CHANNELS) return;
  if (duty > WRAP) duty = WRAP;
  duty_[ch] = duty;
  pwm_set_gpio_level(PINS[ch], duty);
}

void DriverChannels::setDutyAll(uint16_t duty) {
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    setDuty(static_cast<Channel>(i), duty);
  }
}

uint16_t DriverChannels::getDuty(Channel ch) const {
  if (ch >= NUM_CHANNELS) return 0;
  return duty_[ch];
}

uint32_t DriverChannels::frequencyHz() {
  return (clock_get_hz(clk_sys) / static_cast<uint32_t>(CLKDIV)) / (WRAP + 1);
}

const char *DriverChannels::name(Channel ch) {
  if (ch >= NUM_CHANNELS) return "?";
  return NAMES[ch];
}

uint8_t DriverChannels::pinFor(Channel ch) {
  if (ch >= NUM_CHANNELS) return 255;
  return PINS[ch];
}

int DriverChannels::find(const String &token) {
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    if (token.equalsIgnoreCase(NAMES[i])) return i;
  }
  // also accept a plain channel index "0".."3"
  if (token.length() == 1 && isDigit(token[0])) {
    int idx = token.toInt();
    if (idx >= 0 && idx < NUM_CHANNELS) return idx;
  }
  return -1;
}