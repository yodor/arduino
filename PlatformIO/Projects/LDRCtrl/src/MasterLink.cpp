#include "MasterLink.hpp"
#include <string.h>
#include <stdio.h>
#include "Config.hpp"
#include "DualCalibration.hpp"
#include "VolumeRamp.hpp"
#include "BusyHook.hpp"
#include "Board.hpp"

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

// True if `ohms` is one of the values the master may request (CAL_R_CHOICES).
bool allowedR(long ohms) {
  for (uint8_t i = 0; i < CAL_R_CHOICE_COUNT; i++) {
    if (CAL_R_CHOICES[i] == ohms) return true;
  }
  return false;
}

// Parses what follows "CAL FULL" (already upper-cased): exactly
// MODE=<RTOTAL|FIXEDSERIES> and R=<allowed ohms>, in either order, nothing
// else. Returns false for anything incomplete, repeated, or unknown.
bool parseCalFullArgs(String rest, bool &fixed, long &ohms) {
  rest.trim();
  bool haveMode = false, haveR = false;
  while (rest.length() > 0) {
    int sp = rest.indexOf(' ');
    String tok = (sp < 0) ? rest : rest.substring(0, sp);
    rest = (sp < 0) ? String("") : rest.substring(sp + 1);
    rest.trim();
    if (tok.startsWith("MODE=") && !haveMode) {
      String v = tok.substring(5);
      if (v == "RTOTAL") fixed = false;
      else if (v == "FIXEDSERIES") fixed = true;
      else return false;
      haveMode = true;
    } else if (tok.startsWith("R=") && !haveR) {
      String v = tok.substring(2);
      if (v.length() == 0) return false;
      for (int i = 0; i < (int)v.length(); i++) {
        if (v[i] < '0' || v[i] > '9') return false;
      }
      ohms = (long)v.toInt();
      if (!allowedR(ohms)) return false;
      haveR = true;
    } else {
      return false;
    }
  }
  return haveMode && haveR;
}

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

void MasterLink::startCalibration(LDRVolume::CalMode mode) {
  reportStatus(StatusKind::CALIBRATING); // the reply to CAL FAST / CAL FULL: BUSY, CAL=PROCESSING
  NullStream nullOut;
  DualCalibration::runBoth(left_, right_, mode, nullOut);
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
        if (upper == "EXIT" || upper == "PROD") {
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
    } else {
      // Everything else must wait until STATUS reads IDLE -- but say so,
      // rather than leaving the master's command timing out.
      stream_.println(F("ERR busy"));
    }
  }
}

void MasterLink::replyBootHold() {
  char text[48];
  snprintf(text, sizeof(text), "boot mute hold active, %lums remaining",
           (unsigned long)board_.bootHoldRemainingMs());
  reportStatus(StatusKind::BOOT_HOLD, text);
}

void MasterLink::reportStatus(StatusKind kind, const char *text) {
  // STATUS=BUSY for a calibration and for the boot hold; CAL=PROCESSING only for
  // a calibration. A rejected CAL request is IDLE with CAL=ERR.
  const bool busy = (kind == StatusKind::CALIBRATING || kind == StatusKind::BOOT_HOLD);
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
  if (kind == StatusKind::BOOT_HOLD && text) {
    // The hold's reason (and how long is left) is what the master needs; it wins the
    // ERR= slot even if the calibration itself has a problem -- that reappears
    // once the hold ends.
    strncpy(desc, text, sizeof(desc) - 1);
    desc[sizeof(desc) - 1] = '\0';
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
  stream_.print(F(" MODE="));
  stream_.print(left_.mode() == LDRVolume::AttenuationMode::FIXED_SERIES ? F("FIXEDSERIES") : F("RTOTAL"));
  stream_.print(F(" R="));
  stream_.print((long)left_.rTotalOhms()); // Rtotal in RTOTAL mode, the fixed series resistance in FIXEDSERIES mode
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

  // MUTE_BOOT_HOLD_MS is a blanket window, not just an AMP_MUTE-specific
  // gate: refuse EVERY strict-protocol command (VOL, MUTE, CAL, STATUS,
  // AMP_MUTE included) until it expires, so nothing can disturb the
  // controlled startup sequence while it's still settling. The refusal is not
  // an "ERR ..." line: it is the regular status reply with STATUS=BUSY and
  // ERR=boot mute hold active, <n>ms remaining, so a master handles it the same
  // way as any other "busy". DEBUG is deliberately exempt (checked above,
  // already returned) -- a technician needs to be able to reach the bench
  // console at any time, hold or no hold.
  if (board_.bootHoldRemainingMs() > 0) {
    replyBootHold();
    return;
  }

  if (upper == "AMP_MUTE ON" || upper == "AMP_MUTE 1" ||
      upper == "AMP_MUTE OFF" || upper == "AMP_MUTE 0") {
    bool wantMuted = (upper == "AMP_MUTE ON" || upper == "AMP_MUTE 1");
    if (board_.setAmpMute(wantMuted)) {
      stream_.println(F("OK"));
    } else {
      replyBootHold(); // only possible inside the hold, which the gate above already catches
    }

  } else if (upper == "MUTE ON") {
    left_.mute();
    right_.mute();
    stream_.println(F("OK"));

  } else if (upper == "MUTE OFF") {
    left_.unmute();
    right_.unmute();
    stream_.println(F("OK"));

  } else if (upper == "VOL UP") {
    bool okL = left_.stepUp();
    bool okR = right_.stepUp();
    stream_.println((okL && okR) ? F("OK") : F("ERR step limit or no calibration loaded"));

  } else if (upper == "VOL DOWN") {
    bool okL = left_.stepDown();
    bool okR = right_.stepDown();
    stream_.println((okL && okR) ? F("OK") : F("ERR step limit or no calibration loaded"));

  } else if (upper.startsWith("VOL ")) {
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

  } else if (upper == "STATUS") {
    reportStatus();

  } else if (upper == "CAL FAST") {
    // Drift touch-up of the saved calibration: re-measure and trim, no sweep, no
    // parameters. The reply to a CAL command is ALWAYS a status line: BUSY means
    // it started; IDLE with CAL=ERR and ERR=<reason> means it did not (nothing
    // was changed). The master then polls STATUS until IDLE. There is no CAL
    // DONE / CAL FAIL.
    if (!left_.adsPresent() && !right_.adsPresent()) {
      reportStatus(StatusKind::CAL_REJECTED, "no ADS1115 detected on either channel");
    } else {
      startCalibration(LDRVolume::CalMode::FAST);
    }

  } else if (upper.startsWith("CAL FAST ")) {
    reportStatus(StatusKind::CAL_REJECTED, "CAL FAST takes no parameters");

  } else if (upper == "CAL FULL" || upper.startsWith("CAL FULL ")) {
    // Full characterization + solve + trim + save. Bare, it uses the mode and R
    // currently set; with parameters it takes BOTH, MODE=<RTOTAL|FIXEDSERIES> and
    // R=<one of CAL_R_CHOICES>, in either order.
    bool withParams = !(upper == "CAL FULL");
    bool fixed = false;
    long ohms = 0;
    if (withParams && !parseCalFullArgs(upper.substring(8), fixed, ohms)) {
      reportStatus(StatusKind::CAL_REJECTED, "CAL FULL args: MODE=RTOTAL|FIXEDSERIES R=5000|10000|25000|50000|100000");
    } else if (!left_.adsPresent() && !right_.adsPresent()) {
      reportStatus(StatusKind::CAL_REJECTED, "no ADS1115 detected on either channel");
    } else {
      if (withParams) {
        if (fixed) {
          left_.useFixedSeriesOhms((float)ohms);
          right_.useFixedSeriesOhms((float)ohms);
        } else {
          left_.setMode(LDRVolume::AttenuationMode::CONSTANT_RTOTAL);
          right_.setMode(LDRVolume::AttenuationMode::CONSTANT_RTOTAL);
          left_.setRTotalOhms((float)ohms);
          right_.setRTotalOhms((float)ohms);
        }
      } else if (left_.mode() == LDRVolume::AttenuationMode::FIXED_SERIES) {
        // As-is in FIXEDSERIES mode: re-derive the series duty from R against
        // the NEW sweep rather than reusing last time's duty.
        left_.useFixedSeriesOhms(left_.rTotalOhms());
        right_.useFixedSeriesOhms(right_.rTotalOhms());
      }
      startCalibration(LDRVolume::CalMode::FULL);
    }

  } else if (upper == "CAL" || upper.startsWith("CAL ")) {
    reportStatus(StatusKind::CAL_REJECTED, "unknown CAL command: use CAL FAST or CAL FULL [MODE=.. R=..]");

  } else {
    stream_.println(F("ERR unrecognized command"));
  }
}