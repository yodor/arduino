#pragma once
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
  static constexpr uint8_t MAX_POINTS = 40;

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

  // Forward lookup (duty -> resistance), same log-linear interpolation
  // model as dutyForResistance. Used by FIXED_SERIES mode to find out
  // what resistance a chosen fixed duty actually produces. Returns false
  // if targetDuty falls outside the characterized range.
  bool resistanceForDuty(uint16_t targetDuty, float &ohmsOut) const;

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

  struct Entry {
    uint16_t seriesDuty;
    uint16_t shuntDuty;
    float targetDb;
    float targetRsh;
    float targetRs;
    bool valid; // false if either duty lookup fell outside the curve's range
  };

  // CONSTANT_RTOTAL mode: holds Rs+Rsh fixed at rTotalOhms every step.
  uint8_t solve(const LdrCurve &seriesCurve, const LdrCurve &shuntCurve,
                uint8_t numSteps, float rangeDb, float rTotalOhms);

  // FIXED_SERIES mode: series is held at seriesFixedDuty for every step
  // (rSeriesFixedOhms is whatever resistance that duty actually produces,
  // from LdrCurve::resistanceForDuty) -- only the shunt curve is looked
  // up per step. K=1 (true 0dB) is unreachable with any nonzero fixed
  // series resistor (would need Rsh->infinity); steps that would require
  // it are correctly reported invalid rather than producing a bogus duty.
  uint8_t solveFixedSeries(const LdrCurve &shuntCurve, uint8_t numSteps, float rangeDb,
                            float rSeriesFixedOhms, uint16_t seriesFixedDuty);

  uint8_t numSteps() const { return numSteps_; }
  const Entry &step(uint8_t i) const { return entries_[i]; }

  // Used by the trim pass to update one step's duties after verifying
  // against a live measurement. Does not touch targetDb/targetRs/
  // targetRsh/valid -- those remain the original solve's targets/verdict.
  void setStepDuty(uint8_t i, uint16_t seriesDuty, uint16_t shuntDuty) {
    if (i >= numSteps_) return;
    entries_[i].seriesDuty = seriesDuty;
    entries_[i].shuntDuty = shuntDuty;
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
  void setLutStepDuty(uint8_t i, uint16_t seriesDuty, uint16_t shuntDuty) {
    lut_.setStepDuty(i, seriesDuty, shuntDuty);
  }

  uint8_t solveLutFixedSeries(uint8_t numSteps, float rangeDb, float rSeriesFixedOhms, uint16_t seriesFixedDuty) {
    uint8_t valid = lut_.solveFixedSeries(shuntCurve_, numSteps, rangeDb, rSeriesFixedOhms, seriesFixedDuty);
    state_ = State::SOLVED;
    return valid;
  }

  bool feedSeriesPoint(uint16_t duty, float ohms) { return seriesCurve_.addPoint(duty, ohms); }
  bool feedShuntPoint(uint16_t duty, float ohms) { return shuntCurve_.addPoint(duty, ohms); }

  // Runs VolumeLut::solve() against the two curves as they currently
  // stand -- the "fast recalibration reusing known LDR characteristics" path.
  uint8_t solveLut(uint8_t numSteps, float rangeDb, float rTotalOhms);

  State state() const { return state_; }
  const LdrCurve &seriesCurve() const { return seriesCurve_; }
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