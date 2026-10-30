#pragma once
#include <Arduino.h>
#include "LDRVolume.hpp"
#include "ConsoleCommands.hpp"
#include "Board.hpp"

// ============================================================================
// SerialLink
//
// Owns one Stream (intended: Serial1, the real UART0 link to the controller
// board) and defaults to the strict, disciplined wire protocol from
// ldr-attenuator-protocol.md: MUTE ON/OFF, VOL UP/DOWN, VOL <n>,
// AMP_MUTE ON/OFF, CAL [R=<ohms>|R=AUTO], and STATUS -- the one and
// only getter, which carries every readable value. Replies are OK / ERR <reason>
// or the status line; a CAL command is answered with a status line showing BUSY.
// VOL/MUTE/CAL apply to both channels together, per the protocol doc's
// single-shared-volume architecture.
//
// Sending the exact line "DEBUG" switches this stream into the full
// console mode (the same ConsoleCommands used on USB) for the rest of the
// session -- useful for field service without physically re-wiring
// anything -- until "EXIT" switches back to protocol mode. A real controller's
// firmware should never send either of those; they're a human-technician
// escape hatch on the same physical link.
//
// CAL's verbose progress (DualCalibration::calibrate's `out` parameter) is
// redirected to a NullStream while in protocol mode --
// a real controller must never see that text. Completion is NOT announced: the
// controller polls STATUS, which reads BUSY until the calibration finishes.
//
// This class owns ALL byte-reading on its stream (including while in
// console mode) -- the ConsoleCommands passed in must NOT also have its own
// poll() called on the same stream, or both would race reading the same
// bytes. Call SerialLink::poll() only.
// ============================================================================
class SerialLink {
public:
  // console: a ConsoleCommands already constructed on the SAME stream
  // as this SerialLink -- used verbatim once console mode is entered.
  SerialLink(Stream &stream, Board &board, LDRVolume &left, LDRVolume &right, ConsoleCommands &console)
      : stream_(stream), board_(board), left_(left), right_(right), console_(console) {}

  void poll();

  // Lets this link keep answering while a calibration blocks the main loop:
  // STATUS gets "OK STATUS=BUSY ... CAL=PROCESSING", anything else "ERR busy",
  // instead of silence until the calibration ends. Call it for EVERY link that
  // carries the strict protocol (UART and USB), as early as possible in setup().
  // Every link is serviced, a link in console mode included: a line typed there while
  // a long bench operation runs is answered at once ("ERR busy", or the status line
  // for STATUS) instead of queueing silently until it finishes.
  void enableBusyService();

private:
  enum class Mode : uint8_t { PROTOCOL, CONSOLE };   // the words on the wire stay DEBUG / EXIT

  Stream &stream_;
  Board &board_;
  LDRVolume &left_;
  LDRVolume &right_;
  ConsoleCommands &console_;
  Mode mode_ = Mode::PROTOCOL;
  String lineBuf_;

  void handleProtocolLine(const String &line);

  // The strict protocol's command table (SerialLink.cpp): pattern, EXACT/PREFIX, flags
  // (ANYTIME = also answered during a calibration), handler, one-line description.
  struct ProtocolCommand {
    const char *pattern;
    uint8_t match;
    uint8_t flags;
    void (SerialLink::*fn)(const String &upper);
    const char *help;
  };
  static const ProtocolCommand kProtocol[];
  static const ProtocolCommand *findProtocol(const String &upper);
  bool inBusyService_ = false;           // STATUS answers BUSY while true
  bool refuseIfUncalibrated();
  void cmdEnv(const String &);    void cmdProgress(const String &); void cmdLearn(const String &);
  void cmdModel(const String &);  void cmdVersion(const String &);  void cmdBalanceQuery(const String &);
  void cmdStatus(const String &); void cmdAmpMute(const String &);  void cmdMuteOn(const String &);
  void cmdMuteOff(const String &); void cmdVolUp(const String &);   void cmdVolDown(const String &);
  void cmdVolSet(const String &); void cmdForget(const String &);   void cmdForgetUsage(const String &);
  void cmdSetModel(const String &); void cmdBalanceSet(const String &); void cmdSetAuto(const String &);
  void cmdComp(const String &);   void cmdCal(const String &);
  void cmdVerifyResult(const String &);

  // Answers with a BUSY status line, then runs the calibration (blocking, with
  // BusyHook keeping the link alive).
  void startCalibration(const CalRequest &req);



  // STATUS: one line with everything a controller needs after boot to sync
  // its display -- mute states, volume (and its valid range and dB),
  // calibration state, Rtotal, mode, attenuation depth. Format and field
  // meanings: ldr-attenuator-protocol.md. Read-only.
  // Why a status line is being sent, which decides STATUS=, CAL= and ERR=:
  //   IDLE          normal: STATUS=IDLE, CAL from the real state
  //   CALIBRATING   a calibration is running: STATUS=BUSY, CAL=PROCESSING
  //   CAL_REJECTED  a CAL request that could not start: STATUS=IDLE, CAL=ERR,
  //                 ERR=<text>; the real calibration is untouched and the next
  //                 STATUS shows the true state again
  enum class StatusKind : uint8_t { IDLE, CALIBRATING, CAL_REJECTED, VERIFYING };   // VERIFYING: BUSY, the table still OK
  void reportStatus(StatusKind kind = StatusKind::IDLE, const char *text = nullptr);
  void reportEnv();       // protocol 8: ENV -- temperatures, rail, compensation (cached, instant)
  void reportProgress();  // protocol 8: PROGRESS -- what a running calibration is doing
  void reportLearn();     // protocol 9: LEARN -- the thermal learning's progress and fit quality
  void reportModel();     // protocol 12: MODEL -- source/load in use and as solved


  // BusyHook listener: runs inside calibration's blocking waits.
  static void busyThunk(void *self);
  void serviceWhileBusy();
};
