#include "SerialLink.hpp"
#include "NullStream.hpp"
#include "Commands.hpp"
#include "ConsoleCommands.hpp"
#include "Settings.hpp"
#include "VerifyAnalysis.hpp"
#include "VerifyHistory.hpp"
#include "VersionInfo.hpp"
#include "AutoPref.hpp"
#include "Balance.hpp"
#include "BoardSensors.hpp"
#include "ThermalLog.hpp"
#include "ThermalFit.hpp"
#include "BusyHook.hpp"
#include "RailComp.hpp"
#include "ThermalComp.hpp"
#include "CalProgress.hpp"
#include <string.h>
#include <stdio.h>
#include "Config.hpp"
#include "AudioModel.hpp"
#include "DualCalibration.hpp"
#include "VolumeRamp.hpp"
#include "BusyHook.hpp"
#include "Board.hpp"
#include "CalArgs.hpp"

namespace {

// Discards everything written to it. Used to run the existing (chatty)
// calibration code unchanged while in protocol mode, without its per-point
// progress text ever reaching the real controller.


// Everything deriveCal() needs to know, so it can be a pure function.
struct CalFacts {
  bool usableL, usableR;   // hasUsableLut() per channel
  bool hasLutL, hasLutR;   // a LUT exists at all (even if every step is invalid)
  int8_t adsL, adsR;       // last ADS1115 probe: 1 present, 0 absent, -1 never probed
};

// The CAL= value for a status line, and for ERR the reason (into desc):
//   PROCESSING a calibration is running (busy)
//   OK   both channels usable
//   NONE nothing calibrated and nothing wrong (a fresh unit, or a calibration
//        that has just discarded the old one)
//   ERR  anything else: a rejected CAL request (calError), or a channel that
//        cannot run -- no ADS1115, no valid steps, or not calibrated
// There is deliberately no "PARTIAL": one working channel out of two cannot do
// stereo, so it is an error and the description says which channel and why.
const char *deriveCal(bool busy, bool calOk, const CalFacts &f, const char *calError, char *desc, size_t n) {
  desc[0] = '\0';
  if (busy) return "PROCESSING"; // a calibration is running: nothing else about CAL is meaningful
  if (calError) {
    strncpy(desc, calError, n - 1);
    desc[n - 1] = '\0';
    return "ERR";
  }
  if (calOk) return "OK";
  auto problem = [](bool usable, bool hasLut, int8_t ads) -> const char * {
    if (usable) return nullptr;
    if (ads == 0) return "no ADS1115";
    if (hasLut) return "no valid steps";
    return nullptr; // simply not calibrated
  };
  const char *pl = problem(f.usableL, f.hasLutL, f.adsL);
  const char *pr = problem(f.usableR, f.hasLutR, f.adsR);
  if (pl && pr) {
    snprintf(desc, n, "L: %s; R: %s", pl, pr);
    return "ERR";
  }
  if (pl || pr) {
    snprintf(desc, n, "%s: %s", pl ? "L" : "R", pl ? pl : pr);
    return "ERR";
  }
  if (!f.hasLutL && !f.hasLutR) return "NONE";
  snprintf(desc, n, "%s: not calibrated", f.usableL ? "R" : "L"); // one calibrated, the other not
  return "ERR";
}

} // namespace

// Command-table flags (the table itself is further down, with the handlers).
namespace {
enum : uint8_t { EXACT = 0, PREFIX = 1 };   // match the whole line / its start
enum : uint8_t { ANYTIME = 1 };             // also answered during a calibration
} // namespace

void SerialLink::startCalibration(const CalRequest &req) {
  reportStatus(StatusKind::CALIBRATING); // the reply to CAL: BUSY, CAL=PROCESSING
  NullStream nullOut;
  DualCalibration::calibrate(left_, right_, req, nullOut, false);
  // Nothing more is sent. The controller polls STATUS: IDLE means it is over, and
  // CAL=OK / PARTIAL / NONE says how it went.
}

void SerialLink::poll() {
  while (stream_.available()) {
    char c = stream_.read();
    if (c == '\r' || c == '\n') {
      if (lineBuf_.length() == 0) continue;
      String line = lineBuf_;
      lineBuf_ = "";

      if (mode_ == Mode::CONSOLE) {
        String upper = line;
        upper.trim();
        upper.toUpperCase();
        if (upper == "EXIT") {
          console_.cancelImport();          // an unfinished IMPORT writes nothing
          mode_ = Mode::PROTOCOL;
          stream_.println(F("OK -- back to strict protocol mode."));
        } else {
          if (!console_.importing()) stream_.println(); // a blank line after the echoed command
          console_.processLine(line);
        }
      } else {
        handleProtocolLine(line);
      }
    } else if ((c == '\b' || c == 0x7F) && mode_ == Mode::CONSOLE && !console_.importing()) {
      // Console mode: Backspace / Delete (terminals send either) removes the last character,
      // and "\b \b" erases it on the screen too. Protocol mode and IMPORT take bytes as they are.
      if (lineBuf_.length() > 0) {
        lineBuf_.remove(lineBuf_.length() - 1);
        stream_.print(F("\b \b"));
      }
    } else {
      if (mode_ == Mode::CONSOLE && !console_.importing()) stream_.write(c); // echo (not while a backup is pasted)
      lineBuf_ += c;
    }
  }
}

void SerialLink::enableBusyService() {
  BusyHook::add(&SerialLink::busyThunk, this);
}

void SerialLink::busyThunk(void *self) {
  static_cast<SerialLink *>(self)->serviceWhileBusy();
}

void SerialLink::serviceWhileBusy() {
  // Runs from inside a calibration's blocking waits, so a controller gets an
  // answer in ~10 ms instead of a timeout. The outer handler that started
  // the calibration already copied its own line out of lineBuf_ and cleared
  // it, so lineBuf_ is free for the new bytes (and a half-received line
  // simply carries over into poll() afterwards).
  while (stream_.available()) {
    char c = stream_.read();
    if (c != '\r' && c != '\n') {
      lineBuf_ += c;
      continue;
    }
    if (lineBuf_.length() == 0) continue;
    String line = lineBuf_;
    lineBuf_ = "";
    line.trim();
    line.toUpperCase();
    // ANYTIME commands are answered (STATUS then reports BUSY); everything else must wait
    // until STATUS reads IDLE -- but say so, rather than leaving the controller timing out.
    const ProtocolCommand *cmd = findProtocol(line);
    if (cmd && (cmd->flags & ANYTIME)) {
      inBusyService_ = true;
      (this->*(cmd->fn))(line);
      inBusyService_ = false;
    } else {
      stream_.println(F("ERR busy"));
    }
  }
}


// Protocol 8. One short line each (well under 120 characters), fixed key order, so a controller
// with a small receive buffer or a fixed-format parser handles them like STATUS. Values that
// are unknown or not applicable read "--".
namespace {
void kv(Print &o, const __FlashStringHelper *key, float v, uint8_t dec, bool sign = false) {
  o.print(' '); o.print(key); o.print('=');
  if (!isfinite(v)) { o.print(F("--")); return; }
  if (sign && v >= 0) o.print('+');
  o.print(v, dec);
}
} // namespace

void SerialLink::reportEnv() {
  // OK ENV BLOCK= RAIL= COMP= OFFL= OFFR= KL= KR= DBLOCK= MATCH=   (protocol 11: no cells)
  const bool aL = ThermalComp::sideActive(0), aR = ThermalComp::sideActive(1);
  stream_.print(F("OK ENV"));
  kv(stream_, F("BLOCK"), ThermalComp::blockAverage(), 2);
  kv(stream_, F("RAIL"), RailComp::averageVolts(), 4);
  // COMP: ON (both sides compensated), PART (one side), LEARNING (on, fit not trusted yet), OFF
  stream_.print(F(" COMP="));
  stream_.print(!ThermalComp::enabled() ? F("OFF") : !ThermalComp::ntcOk() ? F("NONTC")
               : (aL && aR) ? F("ON") : (aL || aR) ? F("PART") : F("LEARNING"));
  kv(stream_, F("OFFL"), aL ? ThermalComp::sideOffsetCounts(0) : NAN, 2, true);
  kv(stream_, F("OFFR"), aR ? ThermalComp::sideOffsetCounts(1) : NAN, 2, true);
  kv(stream_, F("KL"), ThermalComp::sideSlope(0), 2, true);
  kv(stream_, F("KR"), ThermalComp::sideSlope(1), 2, true);
  const float b = ThermalComp::blockAverage(), b0 = ThermalComp::blockAtCalibration();
  kv(stream_, F("DBLOCK"), (isnan(b) || isnan(b0)) ? NAN : b - b0, 2, true);
  kv(stream_, F("MATCH"), DualCalibration::lastQuietMatchDb(), 2, true);
  stream_.print(F(" NTC=")); stream_.print(BoardSensors::ntcStateName(Board::ntcState()));   // protocol 13
  const ThermalComp::Hint h = ThermalComp::hint();                                           // protocol 14
  stream_.print(F(" RESYNC=")); stream_.print(ThermalComp::hintRecommends(h) ? '1' : '0');   // WARMING: 0
  stream_.print(F(" WHY=")); stream_.print(ThermalComp::hintName(h));
  stream_.println();
}

void SerialLink::reportLearn() {
  // OK LEARN N= SPREAD= COMP= KL= R2L= KR= R2R= MINN= MINSPREAD= MINR2=
  // N resyncs in the log; SPREAD the RMS change of the block temperature across them (C);
  // the fit per side; and the bar it must clear before the compensation trusts it.
  uint8_t n; float spread;
  ThermalLog::fitCounts(n, spread);
  float k[2], r2[2]; bool usable[2];
  ThermalLog::blockSlopes(k, r2, usable);
  const bool aL = ThermalComp::sideActive(0), aR = ThermalComp::sideActive(1);
  stream_.print(F("OK LEARN N=")); stream_.print(n);
  kv(stream_, F("SPREAD"), n ? spread : NAN, 2);
  stream_.print(F(" COMP="));
  stream_.print(!ThermalComp::enabled() ? F("OFF") : (aL && aR) ? F("ON") : (aL || aR) ? F("PART") : F("LEARNING"));
  kv(stream_, F("KL"), k[0], 2, true);
  kv(stream_, F("R2L"), r2[0], 2);
  kv(stream_, F("KR"), k[1], 2, true);
  kv(stream_, F("R2R"), r2[1], 2);
  stream_.print(F(" MINN=3"));
  kv(stream_, F("MINSPREAD"), 0.5f, 2);
  kv(stream_, F("MINR2"), ThermalComp::MIN_R2, 2);
  stream_.println();
}

void SerialLink::reportModel() {
  // OK MODEL SRC= LOAD= SOLVED_SRC= SOLVED_LOAD=   (LOAD 0 = unloaded; SOLVED_* "--" if unknown)
  float s = NAN, l = NAN;
  DualCalibration::solvedModel(s, l);
  stream_.print(F("OK MODEL"));
  kv(stream_, F("SRC"), AudioModel::sourceOhms(), 0);
  kv(stream_, F("LOAD"), AudioModel::loadOhms(), 0);
  kv(stream_, F("SOLVED_SRC"), s, 0);
  kv(stream_, F("SOLVED_LOAD"), l, 0);
  stream_.print(F(" AUTO=")); stream_.print(AutoPref::name(AutoPref::current()));          // protocol 16
  kv(stream_, F("REACH"), DualCalibration::walkReachOhms(left_, right_), 0);
  stream_.println();
}

void SerialLink::reportProgress() {
  // OK PROGRESS KIND= PHASE= [SIDE=] [STEP=i/n] PCT= ETA=
  stream_.print(F("OK PROGRESS"));
  CalProgress::report(stream_);
  stream_.println();
}

void SerialLink::reportStatus(StatusKind kind, const char *text) {
  // STATUS=BUSY with CAL=PROCESSING for a calibration (the power-up cell check included).
  // A rejected CAL request is IDLE with CAL=ERR.
  const bool busy = (kind == StatusKind::CALIBRATING || kind == StatusKind::VERIFYING);
  const char *calError = (kind == StatusKind::CAL_REJECTED) ? text : nullptr;
  // One line: "OK" then key=value pairs separated by single spaces, always
  // every key in this order, so a controller can parse it with a fixed-format
  // scanf or a generic tokenizer. STATUS=BUSY means a calibration/trim is
  // running (answered from BusyHook; every other field is then transient --
  // the LUT may be mid-rebuild -- and should be ignored). Otherwise values
  // other than CAL/MODE/R are only meaningful when CAL=OK (they read 0
  // otherwise).
  //
  // VOLMIN/VOLMAX: the lowest/highest VOL value that is valid on BOTH
  // channels (the unreachable top step is excluded, so VOLMAX is normally
  // one below the LUT size). VOL=<n>, `VOL <n>` and console mode's step
  // numbers are all the same 0-based numbering (0 = quietest). Because 0 is
  // now a real volume, "no calibration" is reported as -1, never 0.
  int lo = -1, hi = -1;
  if (left_.hasLut() && right_.hasLut()) {
    uint8_t n = left_.numSteps() < right_.numSteps() ? left_.numSteps() : right_.numSteps();
    for (uint8_t i = 0; i < n; i++) {
      if (left_.lut().step(i).valid && right_.lut().step(i).valid) {
        if (lo < 0) lo = i;
        hi = i;
      }
    }
  }
  bool usableL = left_.hasUsableLut();
  bool usableR = right_.hasUsableLut();
  // The volume fields below follow REAL usability, not the CAL value: a rejected
  // CAL request reports CAL=ERR while the existing calibration still works.
  bool calOk = (usableL && usableR && lo >= 0);

  // CAL value and, when it is ERR, the reason. Built from state alone -- it never
  // touches the I2C bus (adsLastSeen() is cached; probing from here would shut
  // the bus under a running calibration, and would toggle it during playback).
  //   OK   both channels usable
  //   NONE nothing calibrated and nothing wrong (fresh unit, or a calibration
  //        that has just discarded the old one)
  //   ERR  anything else, with a description: a rejected CAL request, or a
  //        channel that cannot run (no ADS1115, no valid steps, not calibrated)
  char desc[80];
  CalFacts facts = {usableL, usableR, left_.hasLut(), right_.hasLut(),
                    left_.adsLastSeen(), right_.adsLastSeen()};
  const char *calStr = deriveCal(kind == StatusKind::CALIBRATING, calOk, facts, calError, desc, sizeof(desc));
  // Protocol 10: a pending need (set by the power-up check) is what the controller must act on --
  // NONE / CHANGED / STALE -- even when old tables are loaded. PROCESSING and ERR keep priority.
  if (strcmp(calStr, "PROCESSING") != 0 && strcmp(calStr, "ERR") != 0 &&
      DualCalibration::calNeed() != DualCalibration::CalNeed::NONE) {
    calStr = DualCalibration::calNeedName();
    desc[0] = '\0';
  }
  float db = 0.0f;
  if (calOk && left_.currentStep() < left_.numSteps()) {
    db = left_.lut().step(left_.currentStep()).targetDb;
  }

  // Order matters for a controller that shows the raw line on a narrow display
  // or has a short receive buffer (one clipped at ~93 chars did lose the old
  // tail): the fields people actually look at come first, and the constants
  // (RANGE/MODE/R/PROTO) last.
  stream_.print(F("OK STATUS="));
  stream_.print(busy ? F("BUSY") : F("IDLE"));
  stream_.print(F(" TEMP="));
  stream_.print(Board::dieTempC(), 1);
  stream_.print(F(" UP="));
  stream_.print((unsigned long)Board::uptimeSeconds());
  stream_.print(F(" VOL="));
  stream_.print(calOk ? (int)left_.currentStep() : -1);
  stream_.print(F(" DB="));
  stream_.print(db, 1);
  stream_.print(F(" MUTE="));
  stream_.print(left_.isMuted() ? '1' : '0');
  stream_.print(F(" AMP_MUTE="));
  stream_.print((board_.ampMuted() || kind == StatusKind::CALIBRATING) ? '1' : '0'); // a calibration holds the amp muted, even in the reply that announces it
  stream_.print(F(" CAL="));
  stream_.print(calStr);
  stream_.print(F(" VOLMIN="));
  stream_.print(calOk ? lo : -1);
  stream_.print(F(" VOLMAX="));
  stream_.print(calOk ? hi : -1);
  stream_.print(F(" RANGE="));
  stream_.print(calOk ? left_.rangeDb() : 0.0f, 1);
  stream_.print(F(" R="));
  stream_.print((long)left_.rTotalOhms()); // Rs+Rsh, the series+shunt total the LUT is solved for
  // The range the last full calibration measured (stereo-common), 0/0 if never measured,
  // and whether R was chosen by the calibration (1) or by the controller (0).
  {
    const bool known = left_.rangeKnown() && right_.rangeKnown();
    const float mn = known ? (left_.rangeMinOhms() > right_.rangeMinOhms() ? left_.rangeMinOhms() : right_.rangeMinOhms()) : 0.0f;
    const float mx = known ? (left_.rangeMaxOhms() < right_.rangeMaxOhms() ? left_.rangeMaxOhms() : right_.rangeMaxOhms()) : 0.0f;
    stream_.print(F(" RMIN="));
    stream_.print((long)mn);
    stream_.print(F(" RMAX="));
    stream_.print((long)mx);
    stream_.print(F(" RAUTO="));
    stream_.print((left_.rAuto() && right_.rAuto()) ? '1' : '0');
  }
  // What the last CAL actually did (both channels are always calibrated together).
  {
    const uint8_t k = left_.calMeta().lastKind;
    stream_.print(F(" LASTCAL="));
    stream_.print(k == (uint8_t)CalKind::FULL ? F("FULL") : k == (uint8_t)CalKind::RESYNC ? F("RESYNC")
                : k == (uint8_t)CalKind::RESOLVE ? F("RESOLVE") : F("NONE"));
  }
  stream_.print(F(" PROTO="));
  stream_.print(PROTO_VERSION);
  if (desc[0] != '\0') {
    // Always the LAST key, and its value runs to the end of the line (it contains
    // spaces). Present only when CAL=ERR.
    stream_.print(F(" ERR="));
    stream_.print(desc);
  }
  stream_.println();
}

// ============================================================================
// The strict protocol's commands: ONE table, in matching order (an exact entry before the
// prefix entry it would otherwise fall into: "VOL UP" before "VOL ", "FORGET CAL" before
// "FORGET"). EXACT = the whole (trimmed, upper-cased) line; PREFIX = the line starts with it.
// ANYTIME = also answered during a calibration (everything else gets "ERR busy" then).
// ============================================================================

const SerialLink::ProtocolCommand SerialLink::kProtocol[] = {
  // read-only queries (protocol 8-18): answered at any time
  {"ENV",          EXACT,  ANYTIME, &SerialLink::cmdEnv,      "temperatures, rail, compensation, resync hint"},
  {"PROGRESS",     EXACT,  ANYTIME, &SerialLink::cmdProgress, "what a running calibration is doing"},
  {"LEARN",        EXACT,  ANYTIME, &SerialLink::cmdLearn,    "the thermal learning's progress and fit"},
  {"MODEL",        EXACT,  ANYTIME, &SerialLink::cmdModel,    "source/load in use and as solved; AUTO preference"},
  {"VER",          EXACT,  ANYTIME, &SerialLink::cmdVersion,  "firmware, build and chip information"},
  {"FW",           EXACT,  ANYTIME, &SerialLink::cmdVersion,  "same as VER"},
  {"BAL",          EXACT,  ANYTIME, &SerialLink::cmdBalanceQuery, "the balance setting"},
  {"STATUS",       EXACT,  ANYTIME, &SerialLink::cmdStatus,   "state, volume, mutes, calibration"},
  {"VERIFY RESULT", EXACT, ANYTIME, &SerialLink::cmdVerifyResult, "the newest VERIFY run in one line"},
  // setters
  {"AMP_MUTE ON",  EXACT,  0, &SerialLink::cmdAmpMute,    "amplifier mute on (standby)"},
  {"AMP_MUTE 1",   EXACT,  0, &SerialLink::cmdAmpMute,    "same as AMP_MUTE ON"},
  {"AMP_MUTE OFF", EXACT,  0, &SerialLink::cmdAmpMute,    "amplifier mute off"},
  {"AMP_MUTE 0",   EXACT,  0, &SerialLink::cmdAmpMute,    "same as AMP_MUTE OFF"},
  {"MUTE ON",      EXACT,  0, &SerialLink::cmdMuteOn,     "LDR mute on"},
  {"MUTE OFF",     EXACT,  0, &SerialLink::cmdMuteOff,    "LDR mute off (needs CAL=OK)"},
  {"VOL UP",       EXACT,  0, &SerialLink::cmdVolUp,      "one step louder"},
  {"VOL DOWN",     EXACT,  0, &SerialLink::cmdVolDown,    "one step quieter"},
  {"VOL ",         PREFIX, 0, &SerialLink::cmdVolSet,     "VOL <0-31>: set the step"},
  {"FORGET CAL",   EXACT,  0, &SerialLink::cmdForget,     "forget both calibrations"},
  {"FORGET COMP",  EXACT,  0, &SerialLink::cmdForget,     "forget the thermal learning"},
  {"FORGET ALL",   EXACT,  0, &SerialLink::cmdForget,     "forget both"},
  {"FORGET",       PREFIX, 0, &SerialLink::cmdForgetUsage, "(usage)"},
  {"SET SRC ",     PREFIX, 0, &SerialLink::cmdSetModel,   "SET SRC <ohms>: source impedance"},
  {"SET LOAD ",    PREFIX, 0, &SerialLink::cmdSetModel,   "SET LOAD <ohms>: load impedance (0 = none)"},
  {"BAL ",         PREFIX, 0, &SerialLink::cmdBalanceSet, "BAL <-16..16>: balance, 0.25 dB units"},
  {"SET AUTO ",    PREFIX, 0, &SerialLink::cmdSetAuto,    "SET AUTO SNAPPY|BALANCED|DEEP"},
  {"COMP ON",      EXACT,  0, &SerialLink::cmdComp,       "thermal compensation on"},
  {"COMP OFF",     EXACT,  0, &SerialLink::cmdComp,       "thermal compensation off"},
  {"CAL",          EXACT,  0, &SerialLink::cmdCal,        "calibrate (the attenuator decides how much)"},
  {"CAL ",         PREFIX, 0, &SerialLink::cmdCal,        "CAL R=<ohms> | R=AUTO | FULL | VERIFY"},
};

const SerialLink::ProtocolCommand *SerialLink::findProtocol(const String &upper) {
  for (const ProtocolCommand &c : kProtocol) {
    if (c.match == EXACT ? upper == c.pattern : upper.startsWith(c.pattern)) return &c;
  }
  return nullptr;
}

void SerialLink::handleProtocolLine(const String &lineIn) {
  String line = lineIn;
  line.trim();
  if (line.length() == 0) return;

  String upper = line;
  upper.toUpperCase();

  if (upper == "DEBUG") {
    mode_ = Mode::CONSOLE;
    stream_.println(F("OK -- entering DEBUG mode. Send EXIT to return to the strict protocol."));
    return; // no banner: HELP is there if a human wants it
  }
  const ProtocolCommand *c = findProtocol(upper);
  if (!c) { stream_.println(F("ERR unrecognized command")); return; }
  (this->*(c->fn))(upper);
}

// Protocol 10: no boot hold. Volume is refused until the attenuator is calibrated for these
// cells (CAL=OK): the LDR mute stays on, so nothing plays uncalibrated. True = refused (replied).
bool SerialLink::refuseIfUncalibrated() {
  if (DualCalibration::calNeed() == DualCalibration::CalNeed::NONE &&
      left_.hasUsableLut() && right_.hasUsableLut()) return false;
  stream_.print(F("ERR calibration needed (CAL="));
  stream_.print(DualCalibration::calNeed() == DualCalibration::CalNeed::NONE ? "ERR" : DualCalibration::calNeedName());
  stream_.println(')');
  return true;
}

// ---- read-only queries ----
void SerialLink::cmdEnv(const String &) { reportEnv(); }
void SerialLink::cmdProgress(const String &) { reportProgress(); }
void SerialLink::cmdLearn(const String &) { reportLearn(); }
void SerialLink::cmdModel(const String &) { reportModel(); }
void SerialLink::cmdVersion(const String &) { VersionInfo::printLine(stream_); }   // protocol 15
void SerialLink::cmdBalanceQuery(const String &) { stream_.print(F("OK BAL=")); stream_.println(Balance::get()); }   // protocol 18
void SerialLink::cmdStatus(const String &) {
  reportStatus(!inBusyService_ ? StatusKind::IDLE
               : ConsoleCommands::verifyRunning() ? StatusKind::VERIFYING : StatusKind::CALIBRATING);
}


// Protocol 20: the newest VERIFY run in one line (also after restarts: the runs are stored).
void SerialLink::cmdVerifyResult(const String &) {
  const VerifyHistory::Record *r = VerifyHistory::latest();
  if (!r) { stream_.println(F("OK VERIFY SEQ=0 VERDICT=NONE")); return; }
  const VerifyHistory::Summary s = VerifyHistory::summarize(*r);
  const uint16_t boot = Settings::bootNumber();
  stream_.print(F("OK VERIFY SEQ=")); stream_.print(r->seq);
  stream_.print(F(" RESTARTS=")); stream_.print((unsigned)(boot - r->boot));
  stream_.print(F(" AGE_MIN="));
  if (r->boot == boot) stream_.print((millis() / 1000 - r->upS) / 60); else stream_.print('-');
  kv(stream_, F("BLOCK"), r->blockC, 2);
  kv(stream_, F("RES_L"), s.residual[0], 1);  kv(stream_, F("RES_R"), s.residual[1], 1);
  kv(stream_, F("MOVED_L"), r->moved[0], 1);  kv(stream_, F("MOVED_R"), r->moved[1], 1);
  kv(stream_, F("SPREAD_L"), s.spread[0], 1); kv(stream_, F("SPREAD_R"), s.spread[1], 1);
  kv(stream_, F("MID_L"), s.middleDb[0], 2);  kv(stream_, F("MID_R"), s.middleDb[1], 2);
  kv(stream_, F("RMS_L"), s.rms[0], 2);       kv(stream_, F("RMS_R"), s.rms[1], 2);
  kv(stream_, F("LR"), s.worstLR, 2);
  stream_.print(F(" ADOPTED=")); stream_.print(Commands::walkAdopted(*r, left_, right_) ? '1' : '0');
  stream_.print(F(" VERDICT=")); stream_.println(VerifyAnalysis::verdictName((VerifyAnalysis::Verdict)s.verdict));
}

// ---- setters ----
void SerialLink::cmdAmpMute(const String &upper) {
  Commands::ampMute(board_, upper == "AMP_MUTE ON" || upper == "AMP_MUTE 1");
  stream_.println(F("OK"));
}

void SerialLink::cmdMuteOn(const String &) {
  left_.mute();
  right_.mute();
  stream_.println(F("OK"));
}

void SerialLink::cmdMuteOff(const String &) {
  if (refuseIfUncalibrated()) return;
  left_.unmute();
  right_.unmute();
  stream_.println(F("OK"));
}

void SerialLink::cmdVolUp(const String &) {
  if (refuseIfUncalibrated()) return;
  bool okL = left_.stepUp();
  bool okR = right_.stepUp();
  stream_.println((okL && okR) ? F("OK") : F("ERR step limit or no calibration loaded"));
}

void SerialLink::cmdVolDown(const String &) {
  if (refuseIfUncalibrated()) return;
  bool okL = left_.stepDown();
  bool okR = right_.stepDown();
  stream_.println((okL && okR) ? F("OK") : F("ERR step limit or no calibration loaded"));
}

void SerialLink::cmdVolSet(const String &upper) {
  if (refuseIfUncalibrated()) return;
  // 0-based, identical to console mode's step numbers: VOL 0 is the
  // quietest step. toInt() turns garbage into 0, which is now a VALID
  // volume, so insist the argument is all digits before trusting it.
  String arg = upper.substring(4);
  arg.trim();
  bool digits = arg.length() > 0;
  for (int i = 0; i < (int)arg.length(); i++) {
    if (arg[i] < '0' || arg[i] > '9') digits = false;
  }
  int n = digits ? (int)arg.toInt() : -1;
  if (n < 0 || n >= NUM_VOLUME_STEPS_DEFAULT) {
    stream_.println(F("ERR VOL out of range (0-31)"));
  } else {
    LDRVolume *both[2] = {&left_, &right_};
    bool ok[2];
    VolumeRamp::rampTo(both, 2, (uint8_t)n, ok);
    stream_.println((ok[0] && ok[1]) ? F("OK") : F("ERR step invalid or no calibration loaded"));
  }
}

void SerialLink::cmdForget(const String &upper) {
  // Reply at once (the release to the LDR mute takes ~3 s, longer than the controller's
  // timeout), then do it inside a busy scope: STATUS reads BUSY meanwhile.
  stream_.println(F("OK"));
  stream_.flush();
  BusyHook::Scope busy;
  if (upper != "FORGET COMP") DualCalibration::forgetCalibration(left_, right_);
  if (upper != "FORGET CAL")  DualCalibration::forgetCompensation();
}

void SerialLink::cmdForgetUsage(const String &) { stream_.println(F("ERR usage: FORGET CAL|COMP|ALL")); }

void SerialLink::cmdSetModel(const String &upper) {
  // Protocol 12: the audio model at runtime. A calibration solved for other values turns
  // STALE (LDR mute on, volume refused) until CAL re-solves.
  const bool src = upper.startsWith("SET SRC ");
  const String arg = upper.substring(src ? 8 : 9);
  char *end;
  const float ohms = (float)strtod(arg.c_str(), &end);
  if (end == arg.c_str()) { stream_.println(F("ERR usage: SET SRC <ohms> | SET LOAD <ohms>")); return; }
  if (!Commands::setModel(src, ohms, left_, right_)) {
    stream_.println(src ? F("ERR SRC range 0..100000") : F("ERR LOAD range 0 or 1000..100000000"));
    return;
  }
  stream_.println(F("OK"));
}

void SerialLink::cmdBalanceSet(const String &upper) {
  // Protocol 18: balance -16..+16 in 0.25 dB units; negative lowers the right channel.
  const String arg = upper.substring(4);
  char *end;
  const long v = strtol(arg.c_str(), &end, 10);
  if (end == arg.c_str() || v < -Balance::MAX_UNITS || v > Balance::MAX_UNITS) {
    stream_.println(F("ERR usage: BAL <-16..16>"));
    return;
  }
  Balance::set((int8_t)v, left_, right_);
  stream_.println(F("OK"));
}

void SerialLink::cmdSetAuto(const String &upper) {
  // Protocol 16: AUTO's depth vs settling preference. If AUTO would now pick another
  // Rtotal, the stored solution turns STALE (LDR mute on, volume refused) until CAL.
  AutoPref::Pref pr;
  if (!AutoPref::parse(upper.substring(9).c_str(), pr)) { stream_.println(F("ERR usage: SET AUTO SNAPPY|BALANCED|DEEP")); return; }
  Commands::setAutoPref(pr, left_, right_);
  stream_.println(F("OK"));
}

void SerialLink::cmdComp(const String &upper) {
  ThermalComp::setEnabled(upper == "COMP ON");   // not stored: boots ON
  stream_.println(F("OK"));
}

void SerialLink::cmdCal(const String &upper) {
  // The single CAL (protocol 7). The attenuator decides how much work it needs: a cell
  // check against the stored calibration, then a full characterization (nothing stored,
  // or the cells changed), a resync (same cells), or a re-solve + resync (same cells,
  // new R). R=<ohms> must lie in RMIN..RMAX as measured by the last calibration;
  // R=AUTO returns to the automatic choice. The reply is ALWAYS a status line: BUSY
  // if it started, IDLE + CAL=ERR + ERR=<reason> if not (nothing changed).
  const String rest = upper.length() > 3 ? upper.substring(4) : String("");
  CalRequest req;
  const char *why = Commands::validateCal(rest.c_str(), left_, right_, req);
  if (why) reportStatus(StatusKind::CAL_REJECTED, why);
  else if (req.verify) {                     // protocol 20: VERIFY -- STATUS=BUSY with the table still OK
    reportStatus(StatusKind::VERIFYING);
    stream_.flush();
    console_.runVerifyQuiet();               // its text goes nowhere; VERIFY RESULT gives the summary
  } else startCalibration(req);
}
