// ============================================================================
// Controller board firmware
//
// Board-level bring-up lives in Board. The 4-channel LED driver PWM lives
// in DriverChannels. Each LDR board's relay + calibration + volume control
// is grouped into one LDRVolume instance (leftChannel/rightChannel).
// MasterLink is the strict wire-protocol handler (the one a real master
// board's firmware should speak) and is the default on BOTH USB and
// UART0; SerialConsole is the full bench console, reachable on either
// stream only after an explicit DEBUG command (EXIT returns to strict).
// main.cpp is just the wiring.
// ============================================================================

#include <Arduino.h>
#include <Wire.h>
#include "Pins.hpp"
#include "Config.hpp"
#include "Board.hpp"
#include "DriverChannels.hpp"
#include "LDRVolume.hpp"
#include "DualCalibration.hpp"
#include "SerialConsole.hpp"
#include "MasterLink.hpp"

static Board board;
static DriverChannels driver;

// Each LDRVolume owns its relay, its ADS1115 (via an internal LdrSensor),
// and its calibration/volume state. Left uses i2c0 (Wire), right uses
// i2c1 (Wire1). Rref = 10k 1% on both boards per the LDR board design.
static LDRVolume leftChannel(driver, board, COIL_CTRL_L, DriverChannels::SER_L, DriverChannels::SHUNT_L,
                              Wire, SDA_LEFT_PIN, SCL_LEFT_PIN, "/cal_left.bin",
                              RREF_LEFT_OHMS, RTOTAL_LEFT_OHMS);
static LDRVolume rightChannel(driver, board, COIL_CTRL_R, DriverChannels::SER_R, DriverChannels::SHUNT_R,
                               Wire1, SDA_RIGHT_PIN, SCL_RIGHT_PIN, "/cal_right.bin",
                               RREF_RIGHT_OHMS, RTOTAL_RIGHT_OHMS);

// Both streams (USB and UART0) default to the strict wire protocol
// (daughter-board-uart-protocol-v2.md) and both offer the identical
// DEBUG/EXIT escape hatch into the full bench console -- unified so the
// strict protocol itself can be tested directly over USB, without a
// second UART adapter, and so neither stream behaves differently from
// the other. Each MasterLink owns ALL byte-reading on its own stream
// (including while in DEBUG mode); the paired SerialConsole is only ever
// driven via processLine() once DEBUG is entered -- never call either
// SerialConsole's own poll(), that would race its MasterLink for bytes.
static SerialConsole consoleUsb(Serial, board, driver, leftChannel, rightChannel);
static SerialConsole consoleUart(Serial1, board, driver, leftChannel, rightChannel);
static MasterLink masterLinkUsb(Serial, board, leftChannel, rightChannel, consoleUsb);
static MasterLink masterLinkUart(Serial1, board, leftChannel, rightChannel, consoleUart);

void setup() {
  board.begin();        // Serial, mute/regulator pre-latch, clocks, boot info, LED
  driver.begin();       // 4 PWM channels, all start at duty=0
  // The master link comes up BEFORE anything slow, and answers "busy" while
  // a calibration blocks the main loop: a first-boot calibration takes
  // minutes, and a master asking STATUS meanwhile should hear
  // STATUS=BUSY, not silence.
  Serial1.setTX(UART_TX_PIN);
  Serial1.setRX(UART_RX_PIN);
  Serial1.begin(UART_BAUD);
  masterLinkUart.enableBusyService();
  masterLinkUsb.enableBusyService(); // USB too, from the very start: STATUS over USB is immediate even during the boot calibration

  leftChannel.begin();  // relay de-energized (audio mode), i2c bus left inactive
  rightChannel.begin();

  // begin() only LOADS a saved calibration; it no longer sweeps. If either
  // channel has no usable one (fresh flash, PWM resolution change, corrupt
  // file), calibrate both together here: interleaved, with one stereo-common
  // range, rather than two independent per-channel runs.
  if (!leftChannel.hasUsableLut() || !rightChannel.hasUsableLut()) {
    Serial.println(F("Calibrating both channels (interleaved CALAUTO FULL)..."));
    DualCalibration::runBoth(leftChannel, rightChannel, LDRVolume::CalMode::FULL, Serial);
  }

  // Neither consoleUsb.begin() nor consoleUart.begin() is called here --
  // both streams start in strict mode; the channel-map/HELP banner only
  // prints once a DEBUG session is actually entered on one of them.
}

void loop() {
  board.update();        // non-blocking heartbeat LED toggle
  masterLinkUsb.poll();  // strict protocol by default on USB; DEBUG -> consoleUsb
  masterLinkUart.poll(); // strict protocol by default on UART0; DEBUG -> consoleUart
}