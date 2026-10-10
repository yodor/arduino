#pragma once
#include <Arduino.h>
#include "Board.hpp"
#include "DriverChannels.hpp"
#include "LDRVolume.hpp"

// ============================================================================
// SerialConsole -- the bench console, entered with DEBUG (left with EXIT) on either
// link. Deliberately small:
//   CAL [R=<ohms>|R=AUTO]        the calibration, exactly as the master's CAL
//   FORGET                       delete both stored calibrations (next CAL/boot: full)
//   VOL [<n>|UP|DOWN]            volume (0-based); always reports each channel's step,
//                                dB, duties and target Rs/Rsh (+ live values if RELAY ON)
//   VOL INFO                     every step in one table, L and R side by side, with the
//                                L/R match from the last DIAG ALL verify
//   MUTE ON|OFF, AMP_MUTE ON|OFF the LDR mute and the amplifier mute
//   SET <channel> <duty>         raw PWM duty on one driver channel (meter work)
//   RELAY ON|OFF                 both relays; OFF restores the volume before reconnecting
//   READ                         one sensor reading per side (relay must be ON)
//   DIAG                         snapshot: firmware, state, stored calibration, capabilities
//   DIAG ALL                     snapshot + cell check + verify up/down + dark-end walk +
//                                creep -- the one block to send back (~8-9 min)
//   HELP
// ============================================================================
class SerialConsole {
public:
  SerialConsole(Stream &stream, Board &board, DriverChannels &driver,
                LDRVolume &leftChannel, LDRVolume &rightChannel)
      : stream_(stream), board_(board), driver_(driver), left_(leftChannel), right_(rightChannel) {}

  void begin();
  void poll();
  void processLine(const String &line) { handleLine(line); }

private:
  static constexpr int MAX_TOK = 4;

  Stream &stream_;
  Board &board_;
  DriverChannels &driver_;
  LDRVolume &left_;
  LDRVolume &right_;
  String lineBuf_;

  void handleLine(String line);
  void printHelp();

  // DIAG sections
  void diagSnapshot();
  void diagCellCheck();
  void diagVerify();
  void diagWalk();
  void diagCreep(uint16_t quietSeconds);
  void printState();
  void printStored(LDRVolume &v, const char *label);
  void printCurve(const char *label, const LdrCurve &curve);
  void printLut(const VolumeLut &lut);
  void printVolumeDetail(LDRVolume &v, const char *label);
  void printVolumeTable();

  // The last DIAG ALL verify (real dB per step, volume up), kept for VOL INFO's match
  // column; NAN where not measured. lastVerifyMs_ = 0: none since boot.
  float lastUpDb_[2][VolumeLut::MAX_STEPS];
  float lastUpSer_[2][VolumeLut::MAX_STEPS]; // live / target, series (NAN = not measured)
  float lastUpShu_[2][VolumeLut::MAX_STEPS]; // live / target, shunt  (NAN = not measured / open)
  unsigned long lastVerifyMs_ = 0;

  // Plain-language findings from the stored calibration and the last verify, each naming
  // the element (L/R series/shunt) and what to do next. End of VOL INFO and DIAG ALL.
  void printHealth();
};
