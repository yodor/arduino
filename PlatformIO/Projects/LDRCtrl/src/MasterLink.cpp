#include "MasterLink.hpp"
#include "Config.hpp"
#include "DualCalibration.hpp"
#include "VolumeRamp.hpp"
#include "BusyHook.hpp"

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

} // namespace

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

void MasterLink::resolveAndSaveIfCurvesExist() {
  NullStream nullOut;
  bool haveL = !left_.seriesCurve().empty() && !left_.shuntCurve().empty();
  bool haveR = !right_.seriesCurve().empty() && !right_.shuntCurve().empty();
  if (haveL && haveR) {
    // Both channels together, so AUTO uses ONE common range -- solving
    // them independently left R 3 dB deeper than L at the quiet end.
    uint8_t vl, vr;
    DualCalibration::solveBoth(left_, right_, nullOut, vl, vr);
    left_.calSave();
    right_.calSave();
    left_.setStep(left_.currentStep());   // keep the current volume, with the new LUT
    right_.setStep(right_.currentStep());
    return;
  }
  if (haveL) {
    left_.calSolve(NUM_VOLUME_STEPS_DEFAULT, nullOut);
    left_.calSave();
  }
  if (haveR) {
    right_.calSolve(NUM_VOLUME_STEPS_DEFAULT, nullOut);
    right_.calSave();
  }
}

void MasterLink::reportRtotalSuggestions() {
  // Pure computation: evaluates each candidate against the measured curves
  // without changing either channel's Rtotal (so the solved LUT stays put).
  stream_.print(F("CAL INIT SUGGEST"));
  for (uint8_t i = 0; i < CAL_INIT_RTOTAL_CANDIDATE_COUNT; i++) {
    float candidate = CAL_INIT_RTOTAL_CANDIDATES[i];
    float rangeL = left_.computeMaxRangeDb(RANGE_SAFETY_MARGIN_DB, candidate);
    float rangeR = right_.computeMaxRangeDb(RANGE_SAFETY_MARGIN_DB, candidate);
    float worst = (rangeL < rangeR) ? rangeL : rangeR;
    stream_.print(F(" RTOTAL="));
    stream_.print((long)candidate);
    stream_.print(F(":RANGE="));
    stream_.print(worst, 1);
  }
  stream_.println();
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
  if (mode_ != Mode::STRICT) return; // a person is on this link -- leave their bytes alone
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
    if (line == "GET STATUS" || line == "STATUS") {
      reportStatus(true);
    } else {
      // Everything else must wait for CAL DONE / CAL FAIL -- but say so,
      // rather than leaving the master's command timing out.
      stream_.println(F("ERR busy"));
    }
  }
}

void MasterLink::reportStatus(bool busy) {
  // One line: "OK" then key=value pairs separated by single spaces, always
  // every key in this order, so a master can parse it with a fixed-format
  // scanf or a generic tokenizer. STATUS=BUSY means a calibration/trim is
  // running (answered from BusyHook; every other field is then transient --
  // the LUT may be mid-rebuild -- and should be ignored). Otherwise values
  // other than CAL/RTOTAL/MODE are only meaningful when CAL=OK (they read 0
  // otherwise).
  //
  // VOLMIN/VOLMAX: the lowest/highest VOL value that is valid on BOTH
  // channels (the unreachable top step is excluded, so VOLMAX is normally
  // one below the LUT size). VOL=<n> and `VOL <n>` use the same 1-based
  // numbering.
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
  bool calOk = (usableL && usableR && lo >= 0);

  float db = 0.0f;
  if (calOk && left_.currentStep() < left_.numSteps()) {
    db = left_.lut().step(left_.currentStep()).targetDb;
  }

  stream_.print(F("OK STATUS="));
  stream_.print(busy ? F("BUSY") : F("IDLE"));
  stream_.print(F(" PROTO="));
  stream_.print(PROTO_VERSION);
  stream_.print(F(" MUTE="));
  stream_.print(left_.isMuted() ? '1' : '0');
  stream_.print(F(" AMP_MUTE="));
  stream_.print(board_.ampMuted() ? '1' : '0');
  stream_.print(F(" VOL="));
  stream_.print(calOk ? left_.currentStep() + 1 : 0);
  stream_.print(F(" VOLMIN="));
  stream_.print(calOk ? lo + 1 : 0);
  stream_.print(F(" VOLMAX="));
  stream_.print(calOk ? hi + 1 : 0);
  stream_.print(F(" DB="));
  stream_.print(db, 1);
  stream_.print(F(" RANGE="));
  stream_.print(calOk ? left_.rangeDb() : 0.0f, 1);
  stream_.print(F(" CAL="));
  stream_.print(calOk ? F("OK") : ((usableL || usableR) ? F("PARTIAL") : F("NONE")));
  stream_.print(F(" RTOTAL="));
  stream_.print((long)left_.rTotalOhms());
  stream_.print(F(" MODE="));
  stream_.println(left_.mode() == LDRVolume::AttenuationMode::FIXED_SERIES ? F("FIXEDSERIES") : F("RTOTAL"));
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
    debugConsole_.begin(); // prints channel map + full help, now that a human is here
    return;
  }

  // MUTE_BOOT_HOLD_MS is a blanket window, not just an AMP_MUTE-specific
  // gate: refuse EVERY strict-protocol command (VOL, MUTE, CAL, GET*,
  // AMP_MUTE included) until it expires, so nothing can disturb the
  // controlled startup sequence while it's still settling. DEBUG is
  // deliberately exempt (checked above, already returned) -- a
  // technician needs to be able to reach the bench console at any time,
  // hold or no hold.
  if (board_.bootHoldRemainingMs() > 0) {
    stream_.print(F("ERR boot mute hold active, "));
    stream_.print(board_.bootHoldRemainingMs());
    stream_.println(F("ms remaining"));
    return;
  }

  if (upper == "AMP_MUTE ON" || upper == "AMP_MUTE 1" ||
      upper == "AMP_MUTE OFF" || upper == "AMP_MUTE 0") {
    bool wantMuted = (upper == "AMP_MUTE ON" || upper == "AMP_MUTE 1");
    if (board_.setAmpMute(wantMuted)) {
      stream_.println(F("OK"));
    } else {
      stream_.print(F("ERR boot mute hold active, "));
      stream_.print(board_.bootHoldRemainingMs());
      stream_.println(F("ms remaining"));
    }

  } else if (upper == "GET AMP_MUTE") {
    stream_.print(F("AMP_MUTE="));
    stream_.println(board_.ampMuted() ? '1' : '0');

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
    int n = upper.substring(4).toInt();
    if (n < 1 || n > 32) {
      stream_.println(F("ERR VOL out of range (1-32)"));
    } else {
      LDRVolume *both[2] = {&left_, &right_};
      bool ok[2];
      VolumeRamp::rampTo(both, 2, (uint8_t)(n - 1), ok);
      stream_.println((ok[0] && ok[1]) ? F("OK") : F("ERR step invalid or no calibration loaded"));
    }

  } else if (upper == "GET STATUS" || upper == "STATUS") {
    reportStatus(false);

  } else if (upper == "GET MUTE") {
    stream_.print(F("MUTE="));
    stream_.println(left_.isMuted() ? '1' : '0');

  } else if (upper == "GET VOL") {
    stream_.print(F("VOL="));
    stream_.println(left_.currentStep() + 1);

  } else if (upper == "CAL INIT") {
    // Full characterization under whatever RTOTAL/mode is currently set
    // (leaves a working, if not yet optimal, calibration in place
    // immediately), then reports RTOTAL candidates computed from the
    // curves just measured. Does NOT itself commit to a candidate --
    // follow up with CAL RTOTAL/CAL FIXEDSERIES/CAL MODE once a choice
    // is made (cheap: re-solves against these same curves, no re-sweep).
    stream_.println(F("OK"));
    NullStream nullOut;
    DualCalibration::runBoth(left_, right_, LDRVolume::CalMode::FULL, nullOut);

    bool haveCurvesL = !left_.seriesCurve().empty() && !left_.shuntCurve().empty();
    bool haveCurvesR = !right_.seriesCurve().empty() && !right_.shuntCurve().empty();
    if (!haveCurvesL || !haveCurvesR) {
      stream_.println(F("CAL INIT FAIL no ADS1115 detected or no valid points characterized on one or both channels"));
    } else {
      reportRtotalSuggestions();
      stream_.println(F("CAL INIT DONE -- choose with CAL RTOTAL <ohms>, CAL FIXEDSERIES <ohms>, or CAL MODE <RTOTAL|FIXEDSERIES>"));
    }

  } else if (upper.startsWith("CAL FIXEDSERIES ")) {
    float ohms = upper.substring(17).toFloat();
    if (ohms <= 0.0f) {
      stream_.println(F("ERR CAL FIXEDSERIES value must be positive"));
    } else {
      uint16_t dutyL, dutyR;
      bool okL = left_.seriesCurve().dutyForResistance(ohms, dutyL);
      bool okR = right_.seriesCurve().dutyForResistance(ohms, dutyR);
      if (!okL || !okR) {
        stream_.println(F("ERR requested resistance outside one or both channels' characterized series range -- run CAL INIT/FULL first"));
      } else {
        left_.setFixedSeriesDuty(dutyL);
        right_.setFixedSeriesDuty(dutyR);
        left_.setMode(LDRVolume::AttenuationMode::FIXED_SERIES);
        right_.setMode(LDRVolume::AttenuationMode::FIXED_SERIES);
        resolveAndSaveIfCurvesExist();
        stream_.println(F("OK"));
      }
    }

  } else if (upper == "CAL" || upper == "CAL FULL" || upper == "CAL FAST") {
    // Bare CAL defaults to FULL (matches v1 protocol intent: a thorough
    // characterization unless FAST is explicitly requested).
    LDRVolume::CalMode mode = (upper == "CAL FAST") ? LDRVolume::CalMode::FAST : LDRVolume::CalMode::FULL;
    stream_.println(F("OK"));
    NullStream nullOut;
    DualCalibration::runBoth(left_, right_, mode, nullOut);
    // hasLut() alone isn't enough here -- a degenerate calibration (no
    // ADC data during the sweep) can still produce a full-size LUT with
    // every entry marked OUT OF RANGE, which hasLut() can't distinguish
    // from a real success. hasUsableLut() checks for at least one
    // genuinely valid step on each channel.
    if (left_.hasUsableLut() && right_.hasUsableLut()) {
      stream_.println(F("CAL DONE"));
    } else {
      stream_.println(F("CAL FAIL insufficient valid steps or no ADS1115 detected on one or both channels"));
    }

  } else if (upper.startsWith("CAL RTOTAL ")) {
    float ohms = upper.substring(11).toFloat();
    if (ohms <= 0.0f) {
      stream_.println(F("ERR CAL RTOTAL value must be positive"));
    } else {
      left_.setRTotalOhms(ohms);
      right_.setRTotalOhms(ohms);
      resolveAndSaveIfCurvesExist();
      stream_.println(F("OK"));
    }

  } else if (upper == "CAL MODE FIXEDSERIES" || upper == "CAL MODE RTOTAL") {
    LDRVolume::AttenuationMode m = (upper == "CAL MODE FIXEDSERIES")
                                        ? LDRVolume::AttenuationMode::FIXED_SERIES
                                        : LDRVolume::AttenuationMode::CONSTANT_RTOTAL;
    left_.setMode(m);
    right_.setMode(m);
    resolveAndSaveIfCurvesExist();
    stream_.println(F("OK"));

  } else {
    stream_.println(F("ERR unrecognized command"));
  }
}