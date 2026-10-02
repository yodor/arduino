#include "DualCalibration.hpp"
#include "DriverChannels.hpp"
#include "Config.hpp"
#include <math.h>

namespace DualCalibration {

namespace {

// Same point lists as LDRVolume::runAutoCalibration() -- duplicated here
// deliberately (they're small, local statics there, not worth a shared
// header just to avoid repeating 18 numbers).
constexpr uint16_t FULL_PCT[] = {15, 17, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                                  31, 32, 33, 34, 35, 36, 38, 40, 45, 50, 60, 70, 80, 90, 100};
constexpr uint16_t FAST_PCT[] = {18, 22, 26, 32, 40, 55, 75, 100};

// Per-channel convergence state for one duty point -- caller (sweepPhase)
// owns one of these per channel so the two can progress independently.
struct ConvState {
  float prev = -1.0f;
  float lastGood = -1.0f;
  unsigned long start = 0;
  bool done = false;
};

// One non-blocking poll attempt. Same convergence rule as LDRVolume's own
// convergeRead() (two consecutive readings within 1%, or timeout).
void pollOne(LDRVolume &v, bool wantSeries, unsigned long timeoutMs, ConvState &st) {
  if (st.done) return;
  float vRef, rs, rsh;
  bool ok = v.adcRead(vRef, rs, rsh);
  float cur = wantSeries ? rs : rsh;
  if (ok) {
    st.lastGood = cur;
    if (st.prev > 0.0f && fabsf(cur - st.prev) / st.prev < 0.01f) {
      st.done = true;
      return;
    }
    st.prev = cur;
  }
  if (millis() - st.start >= timeoutMs) {
    st.done = true;
  }
}

bool sweepPhase(LDRVolume &left, LDRVolume &right, bool wantSeries,
                 const uint16_t *pts, uint8_t n, unsigned long timeoutMs, Stream &out) {
  if (wantSeries) {
    left.calDriveShunt(DriverChannels::WRAP);
    right.calDriveShunt(DriverChannels::WRAP);
  } else {
    left.calDriveSeries(DriverChannels::WRAP);
    right.calDriveSeries(DriverChannels::WRAP);
  }

  for (uint8_t i = 0; i < n; i++) {
    uint16_t duty = (uint16_t)(((uint32_t)pts[i] * DriverChannels::WRAP) / 100);
    if (wantSeries) {
      left.calDriveSeries(duty);
      right.calDriveSeries(duty);
    } else {
      left.calDriveShunt(duty);
      right.calDriveShunt(duty);
    }

    ConvState stL, stR;
    stL.start = millis();
    stR.start = millis();
    while (!stL.done || !stR.done) {
      pollOne(left, wantSeries, timeoutMs, stL);
      pollOne(right, wantSeries, timeoutMs, stR);
      delay(15);
    }
    unsigned long elapsedL = millis() - stL.start;
    unsigned long elapsedR = millis() - stR.start;

    bool addedL = (stL.lastGood > 0.0f) &&
                  (wantSeries ? left.calFeedSeriesPoint(duty, stL.lastGood)
                              : left.calFeedShuntPoint(duty, stL.lastGood));
    bool addedR = (stR.lastGood > 0.0f) &&
                  (wantSeries ? right.calFeedSeriesPoint(duty, stR.lastGood)
                              : right.calFeedShuntPoint(duty, stR.lastGood));

    out.print(F("  duty="));
    out.print(duty);
    out.print(F("  L="));
    if (stL.lastGood > 0.0f) out.print(stL.lastGood, 1); else out.print(F("ERR"));
    out.print(addedL ? F("") : F("*"));
    out.print(F("("));
    out.print(elapsedL);
    out.print(F("ms)  R="));
    if (stR.lastGood > 0.0f) out.print(stR.lastGood, 1); else out.print(F("ERR"));
    out.print(addedR ? F("") : F("*"));
    out.print(F("("));
    out.print(elapsedR);
    out.println(F("ms)"));

    if (out.available()) {
      char c = out.read();
      if (c != '\r' && c != '\n') {
        out.println(F("DualCal aborted."));
        return false;
      }
    }
  }
  return true;
}

} // namespace

void runBoth(LDRVolume &left, LDRVolume &right, LDRVolume::CalMode mode, Stream &out) {
  bool presentL = left.adsPresent();
  bool presentR = right.adsPresent();

  if (!presentL && !presentR) {
    out.println(F("DualCal: no ADS1115 detected on either channel -- skipping"));
    return;
  }
  if (!presentL) {
    out.println(F("DualCal: left not present -- running right alone"));
    right.runAutoCalibration(mode, out);
    return;
  }
  if (!presentR) {
    out.println(F("DualCal: right not present -- running left alone"));
    left.runAutoCalibration(mode, out);
    return;
  }

  const uint16_t *pts = (mode == LDRVolume::CalMode::FULL) ? FULL_PCT : FAST_PCT;
  uint8_t n = (mode == LDRVolume::CalMode::FULL) ? (sizeof(FULL_PCT) / sizeof(FULL_PCT[0]))
                                                  : (sizeof(FAST_PCT) / sizeof(FAST_PCT[0]));
  unsigned long timeoutMs = (mode == LDRVolume::CalMode::FULL) ? 500 : 150;

  left.calBegin();
  right.calBegin();

  out.println(F("--- DualCal: series sweep (interleaved, shunt held bright) ---"));
  if (!sweepPhase(left, right, true, pts, n, timeoutMs, out)) {
    left.relayEnergize(false);
    right.relayEnergize(false);
    // A partial sweep still drove raw duty directly before aborting --
    // restore hardware to match whatever LUT (old, still-intact one,
    // since calSolve()/calSave() never ran this attempt) is actually
    // current, rather than leaving it at the abort point's leftover duty.
    left.applyDefinedStartupState();
    right.applyDefinedStartupState();
    return;
  }

  out.println(F("--- DualCal: shunt sweep (interleaved, series held bright) ---"));
  if (!sweepPhase(left, right, false, pts, n, timeoutMs, out)) {
    left.relayEnergize(false);
    right.relayEnergize(false);
    left.applyDefinedStartupState();
    right.applyDefinedStartupState();
    return;
  }

  uint8_t validL = left.calSolve(NUM_VOLUME_STEPS_DEFAULT, left.rangeDb(), left.rTotalOhms(), out);
  uint8_t validR = right.calSolve(NUM_VOLUME_STEPS_DEFAULT, right.rangeDb(), right.rTotalOhms(), out);
  out.print(F("--- DualCal done: L solved "));
  out.print(validL);
  out.print('/');
  out.print(NUM_VOLUME_STEPS_DEFAULT);
  out.print(F(", R solved "));
  out.print(validR);
  out.print('/');
  out.print(NUM_VOLUME_STEPS_DEFAULT);
  out.println(F(" ---"));

  bool savedL = left.calSave();
  bool savedR = right.calSave();
  out.print(F("Saved: L="));
  out.print(savedL ? F("OK") : F("FAIL"));
  out.print(F(" R="));
  out.println(savedR ? F("OK") : F("FAIL"));

  left.relayEnergize(false);
  right.relayEnergize(false);

  // Same re-sync LDRVolume::begin()/runAutoCalibration() do on their own
  // -- without this, hardware stays at the sweep's raw leftover duty
  // until the next VOL command happens to differ from the stale
  // currentStep_ by coincidence (exactly what produced the stale-749
  // duty seen on real hardware after a manual CALAUTO FULL).
  left.applyDefinedStartupState();
  right.applyDefinedStartupState();
}

} // namespace DualCalibration