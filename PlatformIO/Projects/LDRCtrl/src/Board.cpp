#include "Board.hpp"
#include "hardware/clocks.h"
#include "Pins.hpp"
#include "Config.hpp"
#include <LittleFS.h>

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
}

void Board::begin() {
  bootMillis_ = millis();
  ampMuted_ = true;

  // Pre-latch mute/regulator-mode output values BEFORE enabling the pin as
  // an output, so the driver never glitches through the opposite state for
  // even one instant when it's first enabled.
  digitalWrite(AMP_MUTE_PIN, (MUTE_ACTIVE_HIGH) ? HIGH : LOW);
  pinMode(AMP_MUTE_PIN, OUTPUT);

  // Force the local SMPS out of PFM (light-load, noisier) mode into
  // low-noise fixed-frequency PWM mode.
  digitalWrite(REGULATOR_MODE_PIN, HIGH);
  pinMode(REGULATOR_MODE_PIN, OUTPUT);

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

  pinMode(DIAGNOSTIC_LED_PIN, OUTPUT);
  digitalWrite(DIAGNOSTIC_LED_PIN, HIGH);
  ledOn_ = true;
  lastBlinkMs_ = millis();
}

unsigned long Board::bootHoldRemainingMs() const {
  unsigned long elapsed = millis() - bootMillis_;
  return (elapsed < MUTE_BOOT_HOLD_MS) ? (MUTE_BOOT_HOLD_MS - elapsed) : 0;
}

bool Board::setAmpMute(bool muted) {
  if (bootHoldRemainingMs() > 0) {
    return false; // boot hold active -- BOTH directions ignored, pin untouched
  }
  bool driveHigh = muted ? MUTE_ACTIVE_HIGH : !MUTE_ACTIVE_HIGH;
  digitalWrite(AMP_MUTE_PIN, driveHigh ? HIGH : LOW);
  ampMuted_ = muted;
  return true;
}

void Board::update() {
  unsigned long now = millis();
  if (now - lastBlinkMs_ >= DIAG_LED_BLINK_MS) {
    lastBlinkMs_ = now;
    ledOn_ = !ledOn_;
    digitalWrite(DIAGNOSTIC_LED_PIN, ledOn_ ? HIGH : LOW);
  }

  // One-shot: the instant the boot hold expires, auto-release the amp
  // mute with no command required. Written directly (not via
  // setAmpMute()) since that method's own gate would otherwise still
  // see 0ms remaining and work fine -- but being explicit here makes
  // the one-shot intent unambiguous rather than relying on that
  // coincidence.
  if (!bootHoldReleased_ && bootHoldRemainingMs() == 0) {
    bootHoldReleased_ = true;
    bool driveHigh = !MUTE_ACTIVE_HIGH; // the "unmuted" drive level
    digitalWrite(AMP_MUTE_PIN, driveHigh ? HIGH : LOW);
    ampMuted_ = false;
  }
}