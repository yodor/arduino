#include "DriverChannels.hpp"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "Pins.hpp"
#include "Dither.hpp"

// ---------------------------------------------------------------------------
// Dither engine. One PWM slice's wrap interrupt (all slices run at the same rate)
// updates all four channels' compare levels. The compare registers are double-
// buffered and latch at each slice's own wrap, so slices need not be phase-aligned.
// Everything the handler touches lives in RAM and the handler itself is placed in
// RAM, so it keeps running correctly while flash is busy (LittleFS saves).
// ---------------------------------------------------------------------------
namespace {
volatile uint32_t gFine[DriverChannels::NUM_CHANNELS];   // written by setDutyFine, read by the ISR
DitherChannel<PWM_DITHER_LEVELS> gDither[DriverChannels::NUM_CHANNELS];
uint16_t gLastLevel[DriverChannels::NUM_CHANNELS];
uint8_t gPin[DriverChannels::NUM_CHANNELS];
uint gIrqSlice = 0;

void __not_in_flash_func(ditherIsr)() {
  pwm_clear_irq(gIrqSlice);
  for (uint8_t i = 0; i < DriverChannels::NUM_CHANNELS; i++) {
    const uint16_t level = gDither[i].next(gFine[i], DriverChannels::WRAP);
    if (level != gLastLevel[i]) {
      pwm_set_gpio_level(gPin[i], level);
      gLastLevel[i] = level;
    }
  }
}
} // namespace

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
    fine_[i] = 0;
    gFine[i] = 0;
    gLastLevel[i] = 0;
    gPin[i] = pin;
  }
  if (PWM_DITHER_LEVELS > 1) {
    gIrqSlice = pwm_gpio_to_slice_num(PINS[0]);
    pwm_clear_irq(gIrqSlice);
    pwm_set_irq_enabled(gIrqSlice, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, ditherIsr);
    irq_set_enabled(PWM_IRQ_WRAP, true);
  }
}

void DriverChannels::setDuty(Channel ch, uint16_t duty) {
  if (duty > WRAP) duty = WRAP;
  setDutyFine(ch, (uint32_t)duty * FINE_PER_COUNT);
}

void DriverChannels::setDutyFine(Channel ch, uint32_t fine) {
  if (ch >= NUM_CHANNELS) return;
  if (fine > FINE_MAX) fine = FINE_MAX;
  fine_[ch] = fine;
  gFine[ch] = fine; // a single aligned 32-bit store: the ISR never sees a torn value
  if (PWM_DITHER_LEVELS == 1) {
    pwm_set_gpio_level(PINS[ch], (uint16_t)fine); // no engine running: write it directly
  }
}

uint32_t DriverChannels::getDutyFine(Channel ch) const {
  if (ch >= NUM_CHANNELS) return 0;
  return fine_[ch];
}

uint32_t DriverChannels::frequencyHz() {
  return (clock_get_hz(clk_sys) / static_cast<uint32_t>(CLKDIV)) / (WRAP + 1);
}

const char *DriverChannels::name(Channel ch) {
  if (ch >= NUM_CHANNELS) return "?";
  return NAMES[ch];
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
