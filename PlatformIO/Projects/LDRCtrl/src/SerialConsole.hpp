#pragma once
#include <Arduino.h>
#include "DriverChannels.hpp"
#include "LDRVolume.hpp"
#include "Board.hpp"

// ============================================================================
// SerialConsole
//
// All command parsing -- raw driver bring-up commands
// (SET/PCT/ALL/ALLPCT/SWEEP/STATUS/OFF/HELP), relay control, calibration,
// and volume control -- talking to the two LDRVolume instances
// (leftChannel/rightChannel). Parameterized by Stream& rather than
// hardcoding the USB Serial object, so the identical parser can run on
// either USB (Serial) or the real UART0 link to the master board
// (Serial1) -- two independent instances, same command set, one shared
// implementation. Both are intended to run simultaneously (see project
// notes on why USB presence should never silently disable the UART
// link) -- main.cpp owns both instances and polls each every loop().
// ============================================================================
class SerialConsole {
public:
  SerialConsole(Stream &stream, Board &board, DriverChannels &driver,
                LDRVolume &leftChannel, LDRVolume &rightChannel)
      : stream_(stream), board_(board), driver_(driver), left_(leftChannel), right_(rightChannel) {}

  // Prints the boot banner (channel map + help). Call once from setup().
  void begin();

  // Non-blocking. Call every loop() iteration to service incoming bytes.
  void poll();

  // Processes one already-assembled line as if it arrived via poll() --
  // for a caller (MasterLink) that owns byte-reading on this stream
  // itself and wants to hand off a complete line, e.g. once a DEBUG
  // session has switched this stream into verbose/bench mode.
  void processLine(const String &line) { handleLine(line); }

private:
  static constexpr int MAX_TOK = 6;

  Stream &stream_;
  Board &board_;
  DriverChannels &driver_;
  LDRVolume &left_;
  LDRVolume &right_;
  String lineBuf_;

  void handleLine(String line);
  void printHelp();
  void printStatus();
  void doSweep(DriverChannels::Channel ch, int startPct, int endPct, int stepPct, unsigned long dwellMs);

  LDRVolume *resolveSide(const String &token); // nullptr + ERR printed on bad token
  void printCurve(const char *label, const LdrCurve &curve);
  void printLut(const VolumeLut &lut);
};