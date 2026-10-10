#pragma once
#include "DarkProfile.hpp"
#include <Arduino.h>

// ============================================================================
// LdrCurve
//
// One LDR's characterized duty->resistance curve -- the reusable,
// slow-to-acquire part of calibration. Once populated it doesn't need
// re-measuring; only the LUT solve step (fast, pure math) needs to re-run
// if the target step count / range / RTOTAL changes.
//
// Points must be added in increasing-duty order (matches the "always
// approach from the same direction" rule the NSL-32SR3's light-history
// behavior needs). Resistance is assumed monotonically non-increasing with
// duty, matching every real sweep taken so far.
// ============================================================================
class LdrCurve {
public:
  // 96, up from 64 (and 40 before that): the sweep refines
  // adaptively across the steep knee and, when the knee sits below the
  // coarse list's first point, extends the list downward (see
  // DualCalibration) -- on top of the ~29-point coarse list. Equal to
  // DualCalibration's MAX_SWEEP_POINTS so a full sweep can never be refused
  // at the bright end, where the floor points are fed last. The saved-file
  // layout is unchanged (it stores count x sizeof(Point)), so existing
  // files still load.
  static constexpr uint8_t MAX_POINTS = 96;

  struct Point {
    uint16_t duty;
    float ohms;
  };

  void clear() { count_ = 0; }
  bool empty() const { return count_ == 0; }
  uint8_t count() const { return count_; }
  const Point &point(uint8_t i) const { return points_[i]; }

  // Raw access for persistence (CalStorage). Not for general use --
  // rawLoad() replaces this curve's contents wholesale, bypassing the
  // strictly-increasing/monotonic checks addPoint() enforces (the data
  // being loaded already passed those checks when it was saved).
  const Point *rawPoints() const { return points_; }
  void rawLoad(const Point *points, uint8_t count) {
    if (count > MAX_POINTS) count = MAX_POINTS;
    for (uint8_t i = 0; i < count; i++) points_[i] = points[i];
    count_ = count;
  }

  // Appends one characterization point. Returns false (point not added)
  // if the table is full, or duty isn't strictly increasing relative to
  // the last point added.
  bool addPoint(uint16_t duty, float ohms);

  // Log-linear interpolation between the two bracketing characterized
  // points (the curve is close to a straight line in log(R) vs duty, per
  // the measured sweeps). Returns false if targetOhms falls outside the
  // characterized range.
  bool dutyForResistance(float targetOhms, uint16_t &dutyOut) const;
  // Same interpolation, but returns FINE units (count x PWM_DITHER_LEVELS): the
  // fractional duty the curve implies, for the dithered LUT.
  bool dutyForResistanceFine(float targetOhms, uint32_t &fineOut) const;


private:
  Point points_[MAX_POINTS];
  uint8_t count_ = 0;
};

// ============================================================================
// VolumeLut
//
// Solves a set of volume steps (log/geometric in dB, holding Rs+Rsh at a
// fixed target) against two already-characterized LdrCurves. Pure math --
// no hardware access, so re-solving with a different step count or dB
// range costs nothing once the curves exist.
// ============================================================================
class VolumeLut {
public:
  static constexpr uint8_t MAX_STEPS = 64;

  // Duties are in FINE units: PWM count x PWM_DITHER_LEVELS (see Config.hpp), so a
  // step can sit BETWEEN counts and the dither engine produces the fraction.
  struct Entry {
    uint32_t seriesFine;
    uint32_t shuntFine;
    float targetDb;
    float targetRsh;
    float targetRs;
    bool valid; // false if either duty lookup fell outside the curve's range
  };

  // CONSTANT_RTOTAL mode: holds Rs+Rsh fixed at rTotalOhms every step -- except
  // the top step, which is the TRANSPARENT state (setTransparentTop()).
  uint8_t solve(const LdrCurve &seriesCurve, const LdrCurve &shuntCurve,
                uint8_t numSteps, float rangeDb, float rTotalOhms);

  // The top step is not solved: 0 dB is out of any divider's reach. It is the
  // transparent state instead -- series at TRANSPARENT_SERIES_OHMS on both channels
  // (stereo-matched), shunt LED OFF (open) -- with its real loaded gain as targetDb
  // (about -0.47 dB). Derived from the series curve alone, so it is refreshed when a
  // calibration loads. Marked by shuntFine == 0 and targetRsh == 0 ("open"); the trim
  // never touches it.
  void setTransparentTop(const LdrCurve &seriesCurve);

  // Apply a settled seek of the quiet-end series point: the steps sharing it (series duty
  // == oldFine) get newFine, and every other step whose series target lies on the SETTLED
  // part of the curve (targetRs >= settledFromOhms) moves by the same number of counts.
  // The knee's position drifts by fractions of a count to a couple of counts between
  // sessions (driver Vbe, the 3.3 V rail behind the PWM); its shape does not -- so the
  // live seek says where the knee is now, the stored curve keeps the shape. Steps below
  // settledFromOhms are trimmed live and are left alone; so is the transparent step.
  // Returns how many non-shared steps were shifted.
  uint8_t applyQuietSeek(uint32_t oldFine, uint32_t newFine, float settledFromOhms);
  bool isTransparent(uint8_t i) const { return i < numSteps_ && entries_[i].valid && entries_[i].shuntFine == 0; }

  uint8_t numSteps() const { return numSteps_; }
  const Entry &step(uint8_t i) const { return entries_[i]; }

  // Used by the trim pass to update one step's duties after verifying
  // against a live measurement. Does not touch targetDb/targetRs/
  // targetRsh/valid -- those remain the original solve's targets/verdict.
  void setStepDuty(uint8_t i, uint32_t seriesFine, uint32_t shuntFine) {
    if (i >= numSteps_) return;
    entries_[i].seriesFine = seriesFine;
    entries_[i].shuntFine = shuntFine;
  }

  // Raw access for persistence (CalStorage) -- see LdrCurve's rawLoad note.
  const Entry *rawEntries() const { return entries_; }
  void rawLoad(const Entry *entries, uint8_t numSteps) {
    if (numSteps > MAX_STEPS) numSteps = MAX_STEPS;
    for (uint8_t i = 0; i < numSteps; i++) entries_[i] = entries[i];
    numSteps_ = numSteps;
  }

private:
  Entry entries_[MAX_STEPS];
  uint8_t numSteps_ = 0;
};

// ============================================================================
// CalibrationSession
//
// Pure curve/LUT bookkeeping for one LDR board -- no driver, relay, or I2C
// knowledge at all (LDRVolume owns those and drives this session's
// feed*Point methods directly). Kept deliberately dumb: this class doesn't
// know or care how a characterization point was obtained -- manually via
// bench commands, or later via an automatic ADS1115-driven sweep -- both
// end up calling the same feedSeriesPoint()/feedShuntPoint().
// ============================================================================
class CalibrationSession {
public:
  enum class State : uint8_t {
    IDLE,           // no curves yet (or stale)
    CHARACTERIZING, // curves being built
    SOLVED,         // curves exist and a LUT has been solved from them
  };

  void beginCharacterization(); // clears curves, state -> CHARACTERIZING
  void resetCurves();           // clears curves + LUT, state -> IDLE

  // Invalidates just the solved LUT (e.g. RTOTAL/mode/range changed since
  // it was solved) -- curves are untouched, since none of those depend
  // on them. hasLut()-gated callers (VOL etc.) correctly refuse until the
  // next solve.
  void invalidateLut() {
    lut_ = VolumeLut();
    if (state_ == State::SOLVED) {
      state_ = (seriesCurve_.empty() && shuntCurve_.empty()) ? State::IDLE : State::CHARACTERIZING;
    }
  }

  // Passthrough for the trim pass -- see VolumeLut::setStepDuty.
  void setLutStepDuty(uint8_t i, uint32_t seriesFine, uint32_t shuntFine) {
    lut_.setStepDuty(i, seriesFine, shuntFine);
  }

  bool feedSeriesPoint(uint16_t duty, float ohms) { return seriesCurve_.addPoint(duty, ohms); }
  bool feedShuntPoint(uint16_t duty, float ohms) { return shuntCurve_.addPoint(duty, ohms); }

  // Runs VolumeLut::solve() against the two curves as they currently
  // stand -- the "fast recalibration reusing known LDR characteristics" path.
  uint8_t solveLut(uint8_t numSteps, float rangeDb, float rTotalOhms);

  State state() const { return state_; }
  const LdrCurve &seriesCurve() const { return seriesCurve_; }

  // Replace the sweep's dark end of the series curve with the settled points of a
  // dark-end walk: the walk's ok points, then every sweep point BRIGHTER than the
  // walk's brightest duty (addPoint() drops any that would make the curve rise).
  // The LUT is invalidated; solve after. False (curve untouched) if unusable.
  // Re-derive the transparent top step from the series curve (after a load).
  void refreshTransparentTop() { lut_.setTransparentTop(seriesCurve_); }
  uint8_t applyQuietSeek(uint32_t oldFine, uint32_t newFine, float settledFromOhms) {
    return lut_.applyQuietSeek(oldFine, newFine, settledFromOhms);
  }

  bool replaceSeriesDarkEnd(const DarkProfile &prof) {
    const uint8_t k = prof.okCount();
    if (k == 0) return false;
    LdrCurve m;
    for (int i = (int)k - 1; i >= 0; i--) m.addPoint(prof.p[i].duty, prof.p[i].ohms);
    const uint16_t top = prof.p[0].duty;
    for (uint8_t i = 0; i < seriesCurve_.count(); i++) {
      const LdrCurve::Point &q = seriesCurve_.point(i);
      if (q.duty > top) m.addPoint(q.duty, q.ohms);
    }
    if (m.count() < 2) return false;
    seriesCurve_ = m;
    invalidateLut();
    return true;
  }
  const LdrCurve &shuntCurve() const { return shuntCurve_; }
  const VolumeLut &lut() const { return lut_; }

  // Used by CalStorage when loading a saved calibration: replaces the
  // curves/LUT wholesale and marks state SOLVED directly, skipping the
  // normal characterize->solve flow since this data was already solved
  // when it was saved.
  void adoptLoaded(const LdrCurve &series, const LdrCurve &shunt, const VolumeLut &lut) {
    seriesCurve_ = series;
    shuntCurve_ = shunt;
    lut_ = lut;
    state_ = State::SOLVED;
  }

private:
  LdrCurve seriesCurve_;
  LdrCurve shuntCurve_;
  VolumeLut lut_;
  State state_ = State::IDLE;
};
