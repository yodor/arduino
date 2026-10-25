#include "MasterLink.hpp"
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
// calibration code unchanged while in strict mode, without its per-point
// progress text ever reaching the real master.
class NullStream : public Stream {
public:
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t *buffer, size_t size) override { return size; }
};

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

void MasterLink::startCalibration(const CalRequest &req) {
  reportStatus(StatusKind::CALIBRATING); // the reply to CAL: BUSY, CAL=PROCESSING
  NullStream nullOut;
  DualCalibration::calibrate(left_, right_, req, nullOut, false);
  // Nothing more is sent. The master polls STATUS: IDLE means it is over, and
  // CAL=OK / PARTIAL / NONE says how it went.
}

void MasterLink::poll() {
  while (stream_.available()) {
    char c = stream_.read();
    if (c == '\r' || c == '\n') {
      if (lineBuf_.length() == 0) continue;
      String line = lineBuf_;
      lineBuf_ = "";

      if (mode_ == Mode::DEBUG) {
        String upper = line;
        upper.trim();
        upper.toUpperCase();
        if (upper == "EXIT") {
          mode_ = Mode::STRICT;
          stream_.println(F("OK -- back to strict protocol mode."));
        } else {
          stream_.println(); // match SerialConsole's own poll() UX
          debugConsole_.processLine(line);
        }
      } else {
        handleStrictLine(line);
      }
    } else {
      if (mode_ == Mode::DEBUG) stream_.write(c); // echo, matching SerialConsole's own UX
      lineBuf_ += c;
    }
  }
}

void MasterLink::enableBusyService() {
  BusyHook::add(&MasterLink::busyThunk, this);
}

void MasterLink::busyThunk(void *self) {
  static_cast<MasterLink *>(self)->serviceWhileBusy();
}

void MasterLink::serviceWhileBusy() {
  // Runs from inside a calibration's blocking waits, so a master gets an
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
    if (line == "STATUS") {
      reportStatus(StatusKind::CALIBRATING);
    } else if (line == "PROGRESS") {
      reportProgress();
    } else if (line == "ENV") {
      reportEnv();
    } else if (line == "LEARN") {
      reportLearn();
    } else if (line == "MODEL") {
      reportModel();
    } else {
      // Everything else must wait until STATUS reads IDLE -- but say so,
      // rather than leaving the master's command timing out.
      stream_.println(F("ERR busy"));
    }
  }
}


// Protocol 8. One short line each (well under 120 characters), fixed key order, so a master
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

void MasterLink::reportEnv() {
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
  stream_.print(F(" RESYNC=")); stream_.print(h == ThermalComp::Hint::NONE ? '0' : '1');
  stream_.print(F(" WHY=")); stream_.print(ThermalComp::hintName(h));
  stream_.println();
}

void MasterLink::reportLearn() {
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

void MasterLink::reportModel() {
  // OK MODEL SRC= LOAD= SOLVED_SRC= SOLVED_LOAD=   (LOAD 0 = unloaded; SOLVED_* "--" if unknown)
  float s = NAN, l = NAN;
  DualCalibration::solvedModel(s, l);
  stream_.print(F("OK MODEL"));
  kv(stream_, F("SRC"), AudioModel::sourceOhms(), 0);
  kv(stream_, F("LOAD"), AudioModel::loadOhms(), 0);
  kv(stream_, F("SOLVED_SRC"), s, 0);
  kv(stream_, F("SOLVED_LOAD"), l, 0);
  stream_.println();
}

void MasterLink::reportProgress() {
  // OK PROGRESS KIND= PHASE= [SIDE=] [STEP=i/n] PCT= ETA=
  stream_.print(F("OK PROGRESS"));
  CalProgress::report(stream_);
  stream_.println();
}

void MasterLink::reportStatus(StatusKind kind, const char *text) {
  // STATUS=BUSY with CAL=PROCESSING for a calibration (the power-up cell check included).
  // A rejected CAL request is IDLE with CAL=ERR.
  const bool busy = (kind == StatusKind::CALIBRATING);
  const char *calError = (kind == StatusKind::CAL_REJECTED) ? text : nullptr;
  // One line: "OK" then key=value pairs separated by single spaces, always
  // every key in this order, so a master can parse it with a fixed-format
  // scanf or a generic tokenizer. STATUS=BUSY means a calibration/trim is
  // running (answered from BusyHook; every other field is then transient --
  // the LUT may be mid-rebuild -- and should be ignored). Otherwise values
  // other than CAL/MODE/R are only meaningful when CAL=OK (they read 0
  // otherwise).
  //
  // VOLMIN/VOLMAX: the lowest/highest VOL value that is valid on BOTH
  // channels (the unreachable top step is excluded, so VOLMAX is normally
  // one below the LUT size). VOL=<n>, `VOL <n>` and the bench console's step
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
  // Protocol 10: a pending need (set by the power-up check) is what the master must act on --
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

  // Order matters for a master that shows the raw line on a narrow display
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
  // and whether R was chosen by the calibration (1) or by the master (0).
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

void MasterLink::handleStrictLine(const String &lineIn) {
  String line = lineIn;
  line.trim();
  if (line.length() == 0) return;

  String upper = line;
  upper.toUpperCase();

  if (upper == "DEBUG") {
    mode_ = Mode::DEBUG;
    stream_.println(F("OK -- entering DEBUG mode. Send EXIT to return to the strict protocol."));
    return; // no banner: HELP is there if a human wants it
  }

  // Read-only queries.
  if (upper == "ENV") { reportEnv(); return; }
  if (upper == "PROGRESS") { reportProgress(); return; }
  if (upper == "LEARN") { reportLearn(); return; }
  if (upper == "MODEL") { reportModel(); return; }
  // Protocol 10: no boot hold. Volume is refused until the attenuator is calibrated for
  // these cells (CAL=OK): the LDR mute stays on, so nothing plays uncalibrated.
  auto needCal = [&]() -> bool {
    if (DualCalibration::calNeed() == DualCalibration::CalNeed::NONE &&
        left_.hasUsableLut() && right_.hasUsableLut()) return false;
    stream_.print(F("ERR calibration needed (CAL="));
    stream_.print(DualCalibration::calNeed() == DualCalibration::CalNeed::NONE ? "ERR" : DualCalibration::calNeedName());
    stream_.println(')');
    return true;
  };

  if (upper == "AMP_MUTE ON" || upper == "AMP_MUTE 1" ||
      upper == "AMP_MUTE OFF" || upper == "AMP_MUTE 0") {
    bool wantMuted = (upper == "AMP_MUTE ON" || upper == "AMP_MUTE 1");
    board_.setAmpMute(wantMuted);   // during a calibration's hold an unmute waits for its end
    stream_.println(F("OK"));

  } else if (upper == "MUTE ON") {
    left_.mute();
    right_.mute();
    stream_.println(F("OK"));

  } else if (upper == "MUTE OFF") {
    if (needCal()) return;
    left_.unmute();
    right_.unmute();
    stream_.println(F("OK"));

  } else if (upper == "VOL UP") {
    if (needCal()) return;
    bool okL = left_.stepUp();
    bool okR = right_.stepUp();
    stream_.println((okL && okR) ? F("OK") : F("ERR step limit or no calibration loaded"));

  } else if (upper == "VOL DOWN") {
    if (needCal()) return;
    bool okL = left_.stepDown();
    bool okR = right_.stepDown();
    stream_.println((okL && okR) ? F("OK") : F("ERR step limit or no calibration loaded"));

  } else if (upper.startsWith("VOL ")) {
    if (needCal()) return;
    // 0-based, identical to the bench console's step numbers: VOL 0 is the
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

  } else if (upper == "FORGET CAL" || upper == "FORGET COMP" || upper == "FORGET ALL") {
    // Reply at once (the release to the LDR mute takes ~3 s, longer than the master's
    // timeout), then do it inside a busy scope: STATUS reads BUSY meanwhile.
    stream_.println(F("OK"));
    stream_.flush();
    BusyHook::Scope busy;
    if (upper != "FORGET COMP") DualCalibration::forgetCalibration(left_, right_);
    if (upper != "FORGET CAL")  DualCalibration::forgetCompensation();

  } else if (upper.startsWith("FORGET")) {
    stream_.println(F("ERR usage: FORGET CAL|COMP|ALL"));

  } else if (upper.startsWith("SET SRC ") || upper.startsWith("SET LOAD ")) {
    // Protocol 12: the audio model at runtime. A calibration solved for other values turns
    // STALE (LDR mute on, volume refused) until CAL re-solves.
    const bool src = upper.startsWith("SET SRC ");
    const String arg = upper.substring(src ? 8 : 9);
    char *end;
    const float ohms = (float)strtod(arg.c_str(), &end);
    if (end == arg.c_str()) { stream_.println(F("ERR usage: SET SRC <ohms> | SET LOAD <ohms>")); return; }
    if (!(src ? AudioModel::setSource(ohms) : AudioModel::setLoad(ohms))) {
      stream_.println(src ? F("ERR SRC range 0..100000") : F("ERR LOAD range 0 or 1000..100000000"));
      return;
    }
    DualCalibration::onModelChanged(left_, right_);
    stream_.println(F("OK"));

  } else if (upper == "COMP ON" || upper == "COMP OFF") {
    ThermalComp::setEnabled(upper == "COMP ON");   // not stored: boots ON
    stream_.println(F("OK"));

  } else if (upper == "STATUS") {
    reportStatus();

  } else if (upper == "CAL" || upper.startsWith("CAL ")) {
    // The single CAL (protocol 7). The daughter decides how much work it needs: a cell
    // check against the stored calibration, then a full characterization (nothing stored,
    // or the cells changed), a resync (same cells), or a re-solve + resync (same cells,
    // new R). R=<ohms> must lie in RMIN..RMAX as measured by the last calibration;
    // R=AUTO returns to the automatic choice. The reply is ALWAYS a status line: BUSY
    // if it started, IDLE + CAL=ERR + ERR=<reason> if not (nothing changed).
    bool hasR = false, rAuto = false;
    long ohms = 0;
    const String rest = upper.length() > 3 ? upper.substring(4) : String("");
    if (!CalArgs::parseCal(rest.c_str(), hasR, rAuto, ohms)) {
      reportStatus(StatusKind::CAL_REJECTED, "CAL args: nothing, R=<ohms> within RMIN..RMAX, or R=AUTO");
    } else if (!left_.adsPresent() && !right_.adsPresent()) {
      reportStatus(StatusKind::CAL_REJECTED, "no ADS1115 detected on either channel");
    } else if (hasR && !(left_.rangeKnown() && right_.rangeKnown())) {
      reportStatus(StatusKind::CAL_REJECTED, "R range unknown -- send CAL first");
    } else {
      const float mn = left_.rangeMinOhms() > right_.rangeMinOhms() ? left_.rangeMinOhms() : right_.rangeMinOhms();
      const float mx = left_.rangeMaxOhms() < right_.rangeMaxOhms() ? left_.rangeMaxOhms() : right_.rangeMaxOhms();
      if (hasR && ((float)ohms < mn || (float)ohms > mx)) {
        char why[96];
        snprintf(why, sizeof why, "R=%ld outside the achievable range %ld..%ld", ohms, (long)mn, (long)mx);
        reportStatus(StatusKind::CAL_REJECTED, why);
      } else {
        CalRequest req;
        req.hasR = hasR; req.rAuto = rAuto; req.ohms = ohms;
        startCalibration(req);
      }
    }

  } else {
    stream_.println(F("ERR unrecognized command"));
  }
}
