#pragma once
#include <Arduino.h>
#include "LDRVolume.hpp"
#include "SerialConsole.hpp"
#include "Board.hpp"

// ============================================================================
// MasterLink
//
// Owns one Stream (intended: Serial1, the real UART0 link to the master
// board) and defaults to the strict, disciplined wire protocol from
// daughter-board-uart-protocol-v2.md: MUTE ON/OFF, VOL UP/DOWN, VOL <n>,
// AMP_MUTE ON/OFF, CAL [R=<ohms>|R=AUTO], and STATUS -- the one and
// only getter, which carries every readable value. Replies are OK / ERR <reason>
// or the status line; a CAL command is answered with a status line showing BUSY.
// VOL/MUTE/CAL apply to both channels together, per the protocol doc's
// single-shared-volume architecture.
//
// Sending the exact line "DEBUG" switches this stream into the full
// bench console (the same SerialConsole used on USB) for the rest of the
// session -- useful for field service without physically re-wiring
// anything -- until "EXIT" switches back to strict mode. A real master's
// firmware should never send either of those; they're a human-technician
// escape hatch on the same physical link.
//
// CAL's verbose progress (DualCalibration::calibrate's `out` parameter) is
// redirected to a NullStream while in strict mode --
// a real master must never see that text. Completion is NOT announced: the
// master polls STATUS, which reads BUSY until the calibration finishes.
//
// This class owns ALL byte-reading on its stream (including while in
// DEBUG mode) -- the SerialConsole passed in must NOT also have its own
// poll() called on the same stream, or both would race reading the same
// bytes. Call MasterLink::poll() only.
// ============================================================================
class MasterLink {
public:
  // debugConsole: a SerialConsole already constructed on the SAME stream
  // as this MasterLink -- used verbatim once DEBUG mode is entered.
  MasterLink(Stream &stream, Board &board, LDRVolume &left, LDRVolume &right, SerialConsole &debugConsole)
      : stream_(stream), board_(board), left_(left), right_(right), debugConsole_(debugConsole) {}

  void poll();

  // Lets this link keep answering while a calibration blocks the main loop:
  // STATUS gets "OK STATUS=BUSY ... CAL=PROCESSING", anything else "ERR busy",
  // instead of silence until the calibration ends. Call it for EVERY link that
  // carries the strict protocol (UART and USB), as early as possible in setup().
  // Every link is serviced, a link in DEBUG mode included: a line typed there while
  // a long bench operation runs is answered at once ("ERR busy", or the status line
  // for STATUS) instead of queueing silently until it finishes.
  void enableBusyService();

private:
  enum class Mode : uint8_t { STRICT, DEBUG };

  Stream &stream_;
  Board &board_;
  LDRVolume &left_;
  LDRVolume &right_;
  SerialConsole &debugConsole_;
  Mode mode_ = Mode::STRICT;
  String lineBuf_;

  void handleStrictLine(const String &line);

  // Answers with a BUSY status line, then runs the calibration (blocking, with
  // BusyHook keeping the link alive).
  void startCalibration(const CalRequest &req);



  // STATUS: one line with everything a master needs after boot to sync
  // its display -- mute states, volume (and its valid range and dB),
  // calibration state, Rtotal, mode, attenuation depth. Format and field
  // meanings: daughter-board-uart-protocol-v2.md. Read-only.
  // Why a status line is being sent, which decides STATUS=, CAL= and ERR=:
  //   IDLE          normal: STATUS=IDLE, CAL from the real state
  //   CALIBRATING   a calibration is running: STATUS=BUSY, CAL=PROCESSING
  //   BOOT_HOLD     inside the boot mute hold: STATUS=BUSY, CAL from the real state,
  //                 ERR=<text> ("boot mute hold active, <n>ms remaining") -- the reply
  //                 to EVERY strict command during the hold
  //   CAL_REJECTED  a CAL request that could not start: STATUS=IDLE, CAL=ERR,
  //                 ERR=<text>; the real calibration is untouched and the next
  //                 STATUS shows the true state again
  enum class StatusKind : uint8_t { IDLE, CALIBRATING, BOOT_HOLD, CAL_REJECTED };
  void reportStatus(StatusKind kind = StatusKind::IDLE, const char *text = nullptr);

  // The reply to any strict command while the boot mute hold is active.
  void replyBootHold();

  // BusyHook listener: runs inside calibration's blocking waits.
  static void busyThunk(void *self);
  void serviceWhileBusy();
};
