#include "MasterLink.hpp"
#include "Config.hpp"

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
  if (!left_.seriesCurve().empty() && !left_.shuntCurve().empty()) {
    left_.calSolve(NUM_VOLUME_STEPS_DEFAULT, left_.rangeDb(), left_.rTotalOhms(), nullOut);
    left_.calSave();
  }
  if (!right_.seriesCurve().empty() && !right_.shuntCurve().empty()) {
    right_.calSolve(NUM_VOLUME_STEPS_DEFAULT, right_.rangeDb(), right_.rTotalOhms(), nullOut);
    right_.calSave();
  }
}

void MasterLink::reportRtotalSuggestions() {
  float savedLeftRtotal = left_.rTotalOhms();
  float savedRightRtotal = right_.rTotalOhms();

  stream_.print(F("CAL INIT SUGGEST"));
  for (uint8_t i = 0; i < CAL_INIT_RTOTAL_CANDIDATE_COUNT; i++) {
    float candidate = CAL_INIT_RTOTAL_CANDIDATES[i];
    left_.setRTotalOhms(candidate);
    right_.setRTotalOhms(candidate);
    float rangeL = left_.computeMaxRangeDb(RANGE_SAFETY_MARGIN_DB);
    float rangeR = right_.computeMaxRangeDb(RANGE_SAFETY_MARGIN_DB);
    float worst = (rangeL < rangeR) ? rangeL : rangeR;
    stream_.print(F(" RTOTAL="));
    stream_.print((long)candidate);
    stream_.print(F(":RANGE="));
    stream_.print(worst, 1);
  }
  stream_.println();

  left_.setRTotalOhms(savedLeftRtotal);
  right_.setRTotalOhms(savedRightRtotal);
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
      bool okL = left_.setStep((uint8_t)(n - 1));
      bool okR = right_.setStep((uint8_t)(n - 1));
      stream_.println((okL && okR) ? F("OK") : F("ERR step invalid or no calibration loaded"));
    }

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
    left_.runAutoCalibration(LDRVolume::CalMode::FULL, nullOut);
    right_.runAutoCalibration(LDRVolume::CalMode::FULL, nullOut);

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
    left_.runAutoCalibration(mode, nullOut);
    right_.runAutoCalibration(mode, nullOut);
    if (left_.hasLut() && right_.hasLut()) {
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