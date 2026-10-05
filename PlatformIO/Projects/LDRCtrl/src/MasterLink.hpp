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
// GET MUTE, GET VOL, CAL -- replying only OK/ERR <reason>/MUTE=.../
// VOL=.../CAL DONE/CAL FAIL <reason>. VOL/MUTE/CAL apply to both channels
// together, per the protocol doc's single-shared-volume architecture.
//
// Sending the exact line "DEBUG" switches this stream into the full
// bench console (the same SerialConsole used on USB) for the rest of the
// session -- useful for field service without physically re-wiring
// anything -- until "EXIT" switches back to strict mode. A real master's
// firmware should never send either of those; they're a human-technician
// escape hatch on the same physical link.
//
// CAL's existing verbose per-point progress (via runAutoCalibration's
// `out` parameter) is redirected to a NullStream while in strict mode --
// a real master must never see that text -- and the actual outcome is
// translated into CAL DONE / CAL FAIL by checking hasLut() afterward.
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

  // Lets this link keep answering while a calibration or trim blocks the main
  // loop: GET STATUS gets "OK STATUS=BUSY ...", anything else "ERR busy",
  // instead of silence until CAL DONE. Call it once, for the REAL master link
  // (UART) only: on a link a person may be using to abort a calibration with
  // a keypress, the listener would swallow that keypress. Inactive while that
  // link is in DEBUG mode.
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

  // After CAL RTOTAL / CAL MODE changes the relevant setting on both
  // channels: if a channel already has characterized curves, re-solves
  // (quietly) and saves immediately -- fast, no re-characterization
  // needed. If a channel has no curves yet, the new setting just takes
  // effect on its next CAL FULL/FAST.
  void resolveAndSaveIfCurvesExist();

  // After CAL INIT characterizes both channels: tries each
  // CAL_INIT_RTOTAL_CANDIDATES value against both channels'
  // computeMaxRangeDb(), reports the worst-of-both achievable range per
  // candidate (the range the unit would actually use). Changes no state --
  // it only reports candidates; CAL RTOTAL/CAL MODE commits to one.
  void reportRtotalSuggestions();

  // GET STATUS: one line with everything a master needs after boot to sync
  // its display -- mute states, volume (and its valid range and dB),
  // calibration state, Rtotal, mode, attenuation depth. Format and field
  // meanings: daughter-board-uart-protocol-v2.md. Read-only.
  void reportStatus(bool busy = false);

  // BusyHook listener: runs inside calibration's blocking waits.
  static void busyThunk(void *self);
  void serviceWhileBusy();
};