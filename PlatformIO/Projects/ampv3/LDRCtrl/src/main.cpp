// ============================================================================
// Controller board firmware
//
// Board-level bring-up lives in Board. The 4-channel LED driver PWM lives
// in DriverChannels. Each LDR board's relay + calibration + volume control
// is grouped into one LDRVolume instance (leftChannel/rightChannel).
// SerialLink is the strict wire-protocol handler (the one a real controller
// board's firmware should speak) and is the default on BOTH USB and
// UART0; ConsoleCommands is console mode, reachable on either
// stream only after an explicit DEBUG command (EXIT returns to strict).
// main.cpp is just the wiring.
// ============================================================================

#include "RailComp.hpp"
#include "ThermalComp.hpp"
#include <Arduino.h>
#include <Wire.h>
#include "Pins.hpp"
#include "Config.hpp"
#include "AudioModel.hpp"
#include "AutoPref.hpp"
#include "Settings.hpp"
#include "VerifyHistory.hpp"
#include "Board.hpp"
#include "DriverChannels.hpp"
#include "LDRVolume.hpp"
#include "DualCalibration.hpp"
#include "ConsoleCommands.hpp"
#include "SerialLink.hpp"

static Board board;
static DriverChannels driver;

// Each LDRVolume owns its relay, its ADS1115 (via an internal LdrSensor),
// and its calibration/volume state. Left uses i2c0 (Wire), right uses
// i2c1 (Wire1). Rref = 10k 1% on both boards per the LDR board design.
static LDRVolume leftChannel(driver, board, COIL_CTRL_L, DriverChannels::SER_L, DriverChannels::SHUNT_L,
                              Wire, SDA_LEFT_PIN, SCL_LEFT_PIN, "/cal_left.bin",
                              RREF_LEFT_OHMS, LDRVolume::PLACEHOLDER_RTOTAL_OHMS);
static LDRVolume rightChannel(driver, board, COIL_CTRL_R, DriverChannels::SER_R, DriverChannels::SHUNT_R,
                               Wire1, SDA_RIGHT_PIN, SCL_RIGHT_PIN, "/cal_right.bin",
                               RREF_RIGHT_OHMS, LDRVolume::PLACEHOLDER_RTOTAL_OHMS);

// Both streams (USB and UART0) default to the strict wire protocol
// (ldr-attenuator-protocol.md) and both offer the identical
// DEBUG/EXIT escape hatch into console mode -- unified so the
// strict protocol itself can be tested directly over USB, without a
// second UART adapter, and so neither stream behaves differently from
// the other. Each SerialLink owns ALL byte-reading on its own stream
// (including while in console mode); the paired ConsoleCommands is only ever
// driven via processLine() once DEBUG is entered -- never call either
// ConsoleCommands's own poll(), that would race its SerialLink for bytes.
static ConsoleCommands consoleUsb(Serial, board, driver, leftChannel, rightChannel);
static ConsoleCommands consoleUart(Serial1, board, driver, leftChannel, rightChannel);
static SerialLink linkUsb(Serial, board, leftChannel, rightChannel, consoleUsb);
static SerialLink linkUart(Serial1, board, leftChannel, rightChannel, consoleUart);

void setup() {
  board.begin();        // Serial, mute/regulator pre-latch, clocks, boot info, LED
  Settings::begin();    // settings.txt (the first time: takes over the older settings files)
  VerifyHistory::begin();          // the last VERIFY runs (flash), the newest back into Results
  VerifyHistory::restoreResults();
  AudioModel::begin();  // source/load model (SET SRC / SET LOAD), before any calibration loads
  AutoPref::begin();    // AUTO preference (SET AUTO SNAPPY|BALANCED|DEEP)
  driver.begin();       // 4 PWM channels, all start at duty=0
  // The serial link comes up BEFORE anything slow, and answers "busy" while
  // a calibration blocks the main loop: a first-boot calibration takes
  // minutes, and a controller asking STATUS meanwhile should hear
  // STATUS=BUSY, not silence.
  Serial1.setTX(UART_TX_PIN);
  Serial1.setRX(UART_RX_PIN);
  Serial1.begin(UART_BAUD);
  linkUart.enableBusyService();
  linkUsb.enableBusyService(); // USB too, from the very start: STATUS over USB is immediate even during the boot calibration

  leftChannel.setSensorWiring(LDR_L_TOP_ON_A3, 'L');   // LDR board layout (auto-corrected)
  rightChannel.setSensorWiring(LDR_R_TOP_ON_A3, 'R');
  leftChannel.begin();  // relay de-energized (audio mode), i2c bus left inactive
  rightChannel.begin();
  VerifyHistory::noteCalibration(leftChannel.calMeta().fullCount, leftChannel.calMeta().resyncCount);
  // Power-up state: amp mute ON (latched in board.begin) and LDR mute ON, until the controller
  // changes them. The controller restores the volume (VOL n is silent while muted) and unmutes.
  leftChannel.mute();
  rightChannel.mute();
  RailComp::begin(driver); // 3.3 V rail compensation (TL431) against the stored reference
  ThermalComp::begin(driver); // driver-block temperature compensation, from the learned fit

  // begin() only LOADS a saved calibration; it no longer sweeps. If either
  // channel has no usable one (fresh flash, PWM resolution change, corrupt
  // file), calibrate both together here: interleaved, with one stereo-common
  // range, rather than two independent per-channel runs.
  // The single CAL's decision, at boot: nothing usable stored -> full characterization;
  // stored calibration -> a cell check (a few seconds, audio disconnected, amp muted):
  // the same cells -> use it as saved; changed cells -> full characterization.
  {
    CalRequest req;
    DualCalibration::calibrate(leftChannel, rightChannel, req, Serial, true);
  }

}

void loop() {
  board.update();        // non-blocking heartbeat LED toggle
  RailComp::tick();      // re-measure the rail every few s; re-scale the drive if it moved
  ThermalComp::tick();   // follow the block temperature; re-offset the drive if it moved
  linkUsb.poll();  // strict protocol by default on USB; DEBUG -> consoleUsb
  linkUart.poll(); // strict protocol by default on UART0; DEBUG -> consoleUart
}
