#pragma once
#include <math.h>
#include <stdint.h>

// ============================================================================
// TrimSearch -- the per-step trim's secant search, in FINE duty units (PWM count x
// dither levels), as a pure state machine: the caller drives the PWM to current(),
// measures, and feed()s the reading until feed() returns false; best() is the answer.
//
// Same strategy as the original whole-count trimDuty(): a first step sized from an
// assumed slope, then secant steps on the MEASURED ln(R)-vs-duty slope, step caps that
// are tight on the steep knee and generous on the flat bright end, and an unreadable
// (too dark) walk-in that is not charged against the probe budget. Two differences:
//   * it can land BETWEEN counts (fine units) -- on the 1M-bleed knee a whole count
//     moves the cell ~25-40%, so whole counts alone cannot meet a 3% tolerance;
//   * the first step uses the curve's own local slope when the caller has one,
//     instead of a fixed 3%/count that badly overshoots a 30%/count knee.
// Tested on the host against simulated knees from 3% to 45% per count.
// ============================================================================
class TrimSearch {
public:
  void begin(float targetOhms, uint32_t guessFine, uint8_t maxProbes, float tolFrac,
             uint32_t finePerCount, uint32_t fineMax, float initialSlopePerCount) {
    target_ = targetOhms;
    lnTarget_ = logf(targetOhms);
    fine_ = bestFine_ = (guessFine > fineMax) ? fineMax : guessFine;
    maxProbes_ = maxProbes;
    tol_ = tolFrac;
    L_ = finePerCount ? finePerCount : 1;
    fineMax_ = fineMax;
    slope0_ = (initialSlopePerCount > 1e-4f) ? initialSlopePerCount : ASSUMED_SLOPE;
    bestErr_ = 1e9f;
    havePrev_ = false;
    probesUsed_ = darkSteps_ = 0;
  }

  uint32_t current() const { return fine_; }
  uint32_t best() const { return bestFine_; }

  // The reading taken at current() (<= 0: unreadable). Returns false when finished.
  bool feed(float measured) {
    if (measured <= 0.0f) {
      if (bestErr_ < 1e8f) return false;          // lost a reading we had: stop, keep the best
      if (++darkSteps_ > MAX_DARK_STEPS) return false;
      const uint32_t brighter = fine_ + DARK_WALK_COUNTS * L_;
      if (brighter > fineMax_) return false;
      fine_ = brighter;
      havePrev_ = false;
      return true;                                // walk-in: not charged to the probe budget
    }
    probesUsed_++;
    const float relErr = (measured - target_) / target_;
    const float absErr = fabsf(relErr);
    if (absErr < bestErr_) { bestErr_ = absErr; bestFine_ = fine_; }
    if (absErr <= tol_) return false;

    const float lnM = logf(measured);
    float stepCounts;
    float maxStep = MAX_STEP_STEEP;
    if (havePrev_ && fine_ != prevFine_) {
      const float dCounts = ((float)fine_ - (float)prevFine_) / (float)L_;
      const float slope = (lnM - prevLn_) / dCounts;      // ln(ohms) per count; negative when sane
      if (slope < -1e-4f) {
        if (-slope < FLAT_SLOPE) maxStep = MAX_STEP_FLAT;
        stepCounts = (lnTarget_ - lnM) / slope;
      } else {
        stepCounts = (measured > target_) ? 4.0f : -4.0f; // noise or lag: modest step the right way
      }
    } else {
      stepCounts = (lnM - lnTarget_) / slope0_;
      if (stepCounts > FIRST_STEP_MAX) stepCounts = FIRST_STEP_MAX;
      if (stepCounts < -FIRST_STEP_MAX) stepCounts = -FIRST_STEP_MAX;
    }
    if (stepCounts > maxStep) stepCounts = maxStep;
    if (stepCounts < -maxStep) stepCounts = -maxStep;
    int32_t stepFine = (int32_t)lroundf(stepCounts * (float)L_);
    if (stepFine == 0) stepFine = (measured > target_) ? 1 : -1;  // always move at least one fine unit

    int64_t next = (int64_t)fine_ + stepFine;
    if (next < 0) next = 0;
    if (next > (int64_t)fineMax_) next = fineMax_;
    if ((uint32_t)next == fine_) return false;
    if (probesUsed_ >= maxProbes_) return false;  // budget spent: keep the best seen
    prevFine_ = fine_;
    prevLn_ = lnM;
    havePrev_ = true;
    fine_ = (uint32_t)next;
    return true;
  }

private:
  static constexpr float MAX_STEP_STEEP = 40.0f;  // counts per probe while the measured slope is steep
  static constexpr float MAX_STEP_FLAT = 150.0f;  // ...and while it is flat (bright end)
  static constexpr float FLAT_SLOPE = 3e-3f;      // |d ln(ohms)/d count| below this counts as flat
  static constexpr float ASSUMED_SLOPE = 0.03f;   // first-probe slope when the caller has none
  static constexpr float FIRST_STEP_MAX = 12.0f;  // counts
  static constexpr uint32_t DARK_WALK_COUNTS = 12;
  static constexpr uint8_t MAX_DARK_STEPS = 12;

  float target_ = 1, lnTarget_ = 0, tol_ = 0.03f, slope0_ = ASSUMED_SLOPE;
  uint32_t fine_ = 0, bestFine_ = 0, prevFine_ = 0, L_ = 1, fineMax_ = 0;
  float bestErr_ = 1e9f, prevLn_ = 0;
  bool havePrev_ = false;
  uint8_t maxProbes_ = 6, probesUsed_ = 0, darkSteps_ = 0;
};
