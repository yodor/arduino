#include "Board.hpp"
#include "hardware/clocks.h"
#include "Pins.hpp"
#include "Config.hpp"
#include "DriverChannels.hpp"
#include <LittleFS.h>
#ifdef LDR_BOARD_IS_PICO_W
#include <WiFi.h>
#include "pico/cyw43_arch.h"
#endif

void Board::bootInfo() {
  Serial.println("\n--- HARDWARE & CLOCK VALIDATION ---");

  #if defined(PICO_RP2350) && PICO_RP2350
    Serial.println("Compile Target: RP2350 Core (Pico 2)");
    uint8_t chip_rev = (sysinfo_hw->chip_id >> 28) & 0xF;
    Serial.print("Silicon Revision: RP2350 A");
    Serial.println(chip_rev);
    #if defined(__riscv) || defined(__riscv__)
      Serial.println("Arch: NATIVE RISC-V (Hazard3 Core Active!)");
    #elif defined(__arm__)
      Serial.println("Arch: NATIVE ARM (Cortex-M33 Core Active)");
    #else
      Serial.println("Arch: Unknown Core Architecture");
    #endif
  #else
    Serial.println("Compile Target: RP2040 Core (Pico)");
  #endif

  // BOARD_NAME is defined automatically by the Arduino-Pico build system
  // for whichever specific board PlatformIO is targeting (pico, rpipicow,
  // rpipico2, rpipico2w, ...) -- this stays correct on its own for any
  // future board swap, no per-variant list to maintain here.
  Serial.print("Board: ");
  #ifdef BOARD_NAME
    Serial.println(BOARD_NAME);
  #else
    Serial.println("(unknown -- BOARD_NAME not defined by this platform package)");
  #endif

  // LDR_BOARD_IS_PICO_W is OUR OWN flag (set in platformio.ini, not a
  // core-internal one) confirming whether the wireless-chip-specific code
  // path (WiFi.end() + cyw43_arch_gpio_put regulator fix, WL_GPIO0/1 pin
  // mapping) is actually compiled into THIS build -- the one thing that
  // directly affects this firmware's own behavior, as distinct from just
  // knowing which physical chip is on the board.
  Serial.print("Wireless-chip code path active (LDR_BOARD_IS_PICO_W): ");
  #ifdef LDR_BOARD_IS_PICO_W
    Serial.println("YES");
  #else
    Serial.println("NO");
  #endif

  Serial.print("Unique Flash ID: ");
  Serial.println(rp2040.getChipID());

  Serial.print("Active CPU Clock: ");
  Serial.print(rp2040.f_cpu() / 1000000);
  Serial.println(" MHz");

  Serial.print("Peripheral Clock (clk_peri): ");
  Serial.print(clock_get_hz(clk_peri) / 1000000);
  Serial.println(" MHz");

  Serial.print("ADC Clock (clk_adc): ");
  Serial.print(clock_get_hz(clk_adc) / 1000000);
  Serial.println(" MHz");

  Serial.print("LED driver PWM frequency: ");
  Serial.print(DriverChannels::frequencyHz());
  Serial.println(" Hz");
}

void Board::begin() {
  bootMillis_ = millis();
  ampMuted_ = true;

  // Pre-latch mute/regulator-mode output values BEFORE enabling the pin as
  // an output, so the driver never glitches through the opposite state for
  // even one instant when it's first enabled.
  digitalWrite(AMP_MUTE_PIN, (MUTE_ACTIVE_HIGH) ? HIGH : LOW);
  pinMode(AMP_MUTE_PIN, OUTPUT);

#ifdef LDR_BOARD_IS_PICO_W
  // Confirmed on real hardware: a bare digitalWrite() on the WL_GPIO1
  // pseudo-pin alone made no audible difference, and -- also confirmed by
  // testing -- neither did adding WiFi.mode(WIFI_OFF)/WiFi.end() ahead of
  // that same wrapper-based call. What actually fixed the white noise is
  // this exact combination: WiFi.end() first to park the radio in a
  // clean, deliberate "off" state, THEN the raw SDK call
  // (cyw43_arch_gpio_put) directly -- not the digitalWrite(33, ...)
  // wrapper around it. The two are supposed to be equivalent per the
  // core's own wrapper source, but empirically, on this hardware, they
  // are not -- trust this result over that reasoning.
  WiFi.mode(WIFI_OFF);
  WiFi.end();

  //regulator fix
  cyw43_arch_gpio_put(1, 1);
#else
  // Force the local SMPS out of PFM (light-load, noisier) mode into
  // low-noise fixed-frequency PWM mode -- REGULATOR_MODE_PIN (23) is a
  // real GPIO on this board.
  digitalWrite(REGULATOR_MODE_PIN, HIGH);
  pinMode(REGULATOR_MODE_PIN, OUTPUT);
#endif

  // DIAGNOSTIC_LED_PIN is 25 (real GPIO) on the original Pico, or 32
  // (WL_GPIO0, routed through the wireless chip) on Pico W -- see
  // Pins.hpp. Default OFF state.
  digitalWrite(DIAGNOSTIC_LED_PIN, LOW);
  pinMode(DIAGNOSTIC_LED_PIN, OUTPUT);

  if (CUSTOM_CLOCKS_ENABLED) {
    set_sys_clock_khz(CPU_SPEED_KHZ, true);

    // Re-bind clk_peri to clk_sys explicitly so it scales with the new CPU
    // speed rather than staying at the fixed 48MHz default.
    uint32_t freq = clock_get_hz(clk_sys);
    clock_configure(
        clk_peri,
        0,
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
        freq,
        freq
    );
  }

  Serial.begin(115200);
  uint32_t timeout = millis();
  while (!Serial && (millis() - timeout < 3000)) {
    delay(10);
  }

  bootInfo();

  // Mount the flash filesystem used for saved calibrations. On a genuinely
  // fresh board (no LittleFS partition written yet), begin() fails once;
  // format and retry -- standard pattern for this core's LittleFS wrapper.
  if (!LittleFS.begin()) {
    Serial.println(F("LittleFS mount failed -- formatting flash filesystem..."));
    LittleFS.format();
    if (!LittleFS.begin()) {
      Serial.println(F("LittleFS mount failed even after format -- saved calibrations will not work."));
    } else {
      Serial.println(F("LittleFS formatted and mounted."));
    }
  } else {
    Serial.println(F("LittleFS mounted."));
  }

}

unsigned long Board::bootHoldRemainingMs() const {
  unsigned long elapsed = millis() - bootMillis_;
  return (elapsed < MUTE_BOOT_HOLD_MS) ? (MUTE_BOOT_HOLD_MS - elapsed) : 0;
}

void Board::writeAmpMute(bool muted) {
  bool driveHigh = muted ? MUTE_ACTIVE_HIGH : !MUTE_ACTIVE_HIGH;
  digitalWrite(AMP_MUTE_PIN, driveHigh ? HIGH : LOW);
  ampMuted_ = muted;
}

bool Board::setAmpMute(bool muted) {
  if (bootHoldRemainingMs() > 0) {
    return false; // boot hold active -- BOTH directions ignored, pin untouched
  }
  if (hold_.active()) {
    // A calibration is holding the amp muted. Asking for mute is already true;
    // asking to open it must wait for the hold to end. The remembered
    // pre-calibration state is deliberately NOT touched here -- relayEnergize()
    // restores "whatever it was" on every transition and would otherwise
    // overwrite it with the forced mute.
    return muted;
  }
  writeAmpMute(muted);
  return true;
}

void Board::calHoldBegin() {
  hold_.begin(ampMuted_);
  writeAmpMute(true);
}

void Board::calHoldEnd() {
  if (hold_.end(bootHoldRemainingMs() > 0)) writeAmpMute(false);
}

void Board::setRelayActive(bool active) {
  if (active) {
    relayActiveCount_++;
  } else if (relayActiveCount_ > 0) {
    relayActiveCount_--;
  }
}

void Board::setDiagLedEnabled(bool enabled) {
  diagLedEnabled_ = enabled;
  digitalWrite(DIAGNOSTIC_LED_PIN, enabled ? HIGH : LOW);
}

void Board::update() {
  // One-shot: the instant the boot hold expires, auto-release the amp
  // mute with no command required. Written directly (not via
  // setAmpMute()) since that method's own gate would otherwise still
  // see 0ms remaining and work fine -- but being explicit here makes
  // the one-shot intent unambiguous rather than relying on that
  // coincidence.
  if (!bootHoldReleased_ && bootHoldRemainingMs() == 0 && !anyRelayActive() && !hold_.active()) {
    bootHoldReleased_ = true;
    bool driveHigh = !MUTE_ACTIVE_HIGH; // the "unmuted" drive level
    digitalWrite(AMP_MUTE_PIN, driveHigh ? HIGH : LOW);
    ampMuted_ = false;
  }
}

float Board::dieTempC() {
  const int N = 16; // the sensor's ADC is ~0.5 degC per LSB; averaging brings it to ~0.1
  float sum = 0.0f;
  for (int i = 0; i < N; i++) sum += analogReadTemp();
  return sum / N;
}

uint32_t Board::uptimeSeconds() { return millis() / 1000UL; }

void Board::reportDieTemp(Stream &out, const __FlashStringHelper *label, uint32_t elapsedMs) {
  char hms[16];
  uint32_t up = uptimeSeconds();
  out.print(label);
  out.print(F(": "));
  out.print(dieTempC(), 1);
  out.print(F(" C (RP2040 die)  uptime "));
  out.print(formatHms(up, hms, sizeof(hms)));
  out.print(F(" ("));
  out.print((unsigned long)up);
  out.print(F(" s)"));
  if (elapsedMs > 0) {
    out.print(F("  took "));
    out.print(elapsedMs / 1000.0f, 1);
    out.print(F(" s"));
  }
  out.println();
}