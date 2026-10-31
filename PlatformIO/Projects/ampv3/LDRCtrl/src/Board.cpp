#include "Board.hpp"
#include "Console.hpp"
#include "hardware/clocks.h"
#include "Pins.hpp"
#include "Config.hpp"
#include "DriverChannels.hpp"
#include "BoardSensors.hpp"
#include <LittleFS.h>
#ifdef LDR_BOARD_IS_PICO_W
#include <WiFi.h>
#include "pico/cyw43_arch.h"
#endif

void Board::bootInfo() {
  usb.blank();
  usb.line("--- HARDWARE & CLOCK VALIDATION ---");
  usb.line("Firmware: %s, built %s %s", FW_VERSION, __DATE__, __TIME__);
#if defined(PICO_RP2350) && PICO_RP2350
  usb.line("Compile Target: RP2350 Core (Pico 2)");
  usb.line("Silicon Revision: RP2350 A%u", (unsigned)((sysinfo_hw->chip_id >> 28) & 0xF));
  #if defined(__riscv) || defined(__riscv__)
  usb.line("Arch: NATIVE RISC-V (Hazard3 Core Active!)");
  #elif defined(__arm__)
  usb.line("Arch: NATIVE ARM (Cortex-M33 Core Active)");
  #else
  usb.line("Arch: Unknown Core Architecture");
  #endif
#else
  usb.line("Compile Target: RP2040 Core (Pico)");
#endif
  // BOARD_NAME comes from the Arduino-Pico build for the board PlatformIO targets.
#ifdef BOARD_NAME
  usb.line("Board: %s", BOARD_NAME);
#else
  usb.line("Board: (unknown -- BOARD_NAME not defined by this platform package)");
#endif
  // LDR_BOARD_IS_PICO_W is our own flag (platformio.ini): whether the wireless-chip code path
  // (WiFi.end() + the regulator fix, WL_GPIO pin mapping) is compiled into this build.
  usb.line("Driver board: %s  (calibration hardware rev %u)", DRIVER_BOARD_NAME, (unsigned)CAL_HW_REV);
#ifdef LDR_BOARD_IS_PICO_W
  usb.line("Wireless-chip code path active (LDR_BOARD_IS_PICO_W): YES");
#else
  usb.line("Wireless-chip code path active (LDR_BOARD_IS_PICO_W): NO");
#endif
  usb.line("Unique Flash ID: %s", rp2040.getChipID());
  usb.line("Active CPU Clock: %lu MHz", (unsigned long)(rp2040.f_cpu() / 1000000));
  usb.line("Peripheral Clock (clk_peri): %lu MHz", (unsigned long)(clock_get_hz(clk_peri) / 1000000));
  usb.line("ADC Clock (clk_adc): %lu MHz", (unsigned long)(clock_get_hz(clk_adc) / 1000000));
  usb.line("LED driver PWM frequency: %lu Hz", (unsigned long)DriverChannels::frequencyHz());
}

void Board::begin() {
  bootMillis_ = millis();
  ampMuted_ = true;

  // Pre-latch mute/regulator-mode output values BEFORE enabling the pin as
  // an output, so the driver never glitches through the opposite state for
  // even one instant when it's first enabled.
  digitalWrite(AMP_MUTE_PIN, (MUTE_ACTIVE_HIGH) ? HIGH : LOW);
  pinMode(AMP_MUTE_PIN, OUTPUT);

  // Analog sensor pins: configure them for the ADC NOW. Out of reset an RP2040 pin has its
  // pull-down on; on the NTC node that sits in parallel with the thermistor (and the 100 nF
  // there needs tens of ms to recover), so the first reading after boot came out ~60 C.
  // The first analogRead() switches the pad to analog (pulls off, digital input off).
  analogReadResolution(12);
  (void)analogRead(ADC_NTC_PIN);
  (void)analogRead(ADC_REF_PIN);

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
    usb.line("LittleFS mount failed -- formatting flash filesystem...");
    LittleFS.format();
    if (!LittleFS.begin()) {
      usb.line("LittleFS mount failed even after format -- saved calibrations will not work.");
    } else {
      usb.line("LittleFS formatted and mounted.");
    }
  } else {
    usb.line("LittleFS mounted.");
  }
  LittleFS.remove("/relay_hold.txt"); // left over from the withdrawn PWM coil hold (2026-10-15e..i)

}


void Board::writeAmpMute(bool muted) {
  bool driveHigh = muted ? MUTE_ACTIVE_HIGH : !MUTE_ACTIVE_HIGH;
  digitalWrite(AMP_MUTE_PIN, driveHigh ? HIGH : LOW);
  ampMuted_ = muted;
}

bool Board::setAmpMute(bool muted) {
  // The amp mute is latched ON at power-up and changes only on command (the controller decides
  // the power state). No timed boot hold any more.
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
  if (hold_.end(false)) writeAmpMute(false);
}

void Board::setRelayActive(bool active) {
  if (active) {
    relayActiveCount_++;
  } else if (relayActiveCount_ > 0) {
    relayActiveCount_--;
  }
}

void Board::update() {
  // (The timed boot-hold auto-unmute is gone: the amp stays muted until the controller says so.)
}

float Board::dieTempC() {
  const int N = 16; // the sensor's ADC is ~0.5 degC per LSB; averaging brings it to ~0.1
  float sum = 0.0f;
  for (int i = 0; i < N; i++) sum += analogReadTemp();
  return sum / N;
}

namespace {
// 256 averaged 12-bit reads (~3 ms): the RP2040 ADC's noise and its few uneven codes average out.
float adcAverage(uint8_t pin) {
  analogReadResolution(12);
  (void)analogRead(pin);   // selects the input; the first conversion after a switch is discarded
  delayMicroseconds(50);
  const int N = 256;
  uint32_t sum = 0;
  for (int i = 0; i < N; i++) sum += (uint32_t)analogRead(pin);
  return (float)sum / N;
}
} // namespace

float Board::blockTempC() { return BoardSensors::ntcCelsius(adcAverage(ADC_NTC_PIN)); }
BoardSensors::NtcState Board::ntcState() { return BoardSensors::ntcState(adcAverage(ADC_NTC_PIN)); }

namespace { bool gAmpCmdMuted = true; uint32_t gAmpOnMs = 0; }   // power-up: muted (standby)
void Board::noteAmpCommand(bool muted) {
  if (gAmpCmdMuted && !muted) gAmpOnMs = millis();
  gAmpCmdMuted = muted;
}
bool Board::ampCommandedMuted() { return gAmpCmdMuted; }
uint32_t Board::msSinceAmpOn() { return gAmpCmdMuted ? 0 : millis() - gAmpOnMs; }
float Board::railVolts()  { return BoardSensors::railVolts(adcAverage(ADC_REF_PIN)); }

uint32_t Board::uptimeSeconds() { return millis() / 1000UL; }

void Board::reportDieTemp(Stream &out, const __FlashStringHelper *label, uint32_t elapsedMs) {
  char hms[16];
  uint32_t up = uptimeSeconds();
  out.print(label);
  out.print(F(": "));
  out.print(dieTempC(), 1);
  out.print(F(" C (RP2040 die)  block "));
  const float tb = blockTempC(), vr = railVolts();
  if (isnan(tb)) { out.print(F("-- (NTC ")); out.print(BoardSensors::ntcStateName(ntcState())); out.print(')'); }
  else out.print(tb, 2);
  out.print(F(" C (NTC)  rail "));
  if (isnan(vr)) out.print(F("--")); else out.print(vr, 3);
  out.print(F(" V (TL431)  "));
  out.print(F("uptime "));
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
