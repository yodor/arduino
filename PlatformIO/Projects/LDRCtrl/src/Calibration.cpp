#include "Calibration.hpp"
#include <math.h>

// ---------------------------------------------------------------------------
// LdrCurve
// ---------------------------------------------------------------------------
bool LdrCurve::addPoint(uint16_t duty, float ohms) {
  if (count_ >= MAX_POINTS) return false;
  if (count_ > 0 && duty <= points_[count_ - 1].duty) return false; // duty must be strictly increasing
  // Resistance should be non-increasing as duty rises, but allow a small
  // tolerance: near the resistance floor, ordinary measurement noise can
  // make consecutive readings tick up a few percent even though the true
  // physical trend is still flat/decreasing. What this must still catch
  // is the order-of-magnitude noise spikes that slip past LdrSensor's
  // Vref floor near the DARK end -- those overshoot by 100s of percent,
  // nowhere near this tolerance.
  static constexpr float MAX_RISE_TOLERANCE = 1.10f; // allow up to +10%
  if (count_ > 0 && ohms > points_[count_ - 1].ohms * MAX_RISE_TOLERANCE) return false;
  points_[count_].duty = duty;
  points_[count_].ohms = ohms;
  count_++;
  return true;
}

bool LdrCurve::dutyForResistance(float targetOhms, uint16_t &dutyOut) const {
  if (count_ < 2) return false; // need at least two points to interpolate

  if (targetOhms > points_[0].ohms) return false;
  if (targetOhms < points_[count_ - 1].ohms) return false;

  for (uint8_t i = 0; i < count_ - 1; i++) {
    float hi = points_[i].ohms;
    float lo = points_[i + 1].ohms;
    if (targetOhms <= hi && targetOhms >= lo) {
      if (hi <= 0.0f || lo <= 0.0f) return false;
      if (hi == lo) { dutyOut = points_[i].duty; return true; }
      float logHi = logf(hi);
      float logLo = logf(lo);
      float logT  = logf(targetOhms);
      float frac = (logHi - logT) / (logHi - logLo);
      int32_t d = (int32_t)points_[i].duty +
                  (int32_t)lroundf(frac * (float)((int32_t)points_[i + 1].duty - (int32_t)points_[i].duty));
      dutyOut = (uint16_t)d;
      return true;
    }
  }
  return false;
}

bool LdrCurve::resistanceForDuty(uint16_t targetDuty, float &ohmsOut) const {
  if (count_ < 2) return false;
  if (targetDuty < points_[0].duty) return false;
  if (targetDuty > points_[count_ - 1].duty) return false;

  for (uint8_t i = 0; i < count_ - 1; i++) {
    uint16_t d0 = points_[i].duty, d1 = points_[i + 1].duty;
    if (targetDuty >= d0 && targetDuty <= d1) {
      float hi = points_[i].ohms, lo = points_[i + 1].ohms;
      if (d1 == d0) { ohmsOut = hi; return true; }
      if (hi <= 0.0f || lo <= 0.0f) return false;
      float frac = (float)(targetDuty - d0) / (float)(d1 - d0);
      float logR = logf(hi) + frac * (logf(lo) - logf(hi));
      ohmsOut = expf(logR);
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// VolumeLut
// ---------------------------------------------------------------------------
uint8_t VolumeLut::solve(const LdrCurve &seriesCurve, const LdrCurve &shuntCurve,
                          uint8_t numSteps, float rangeDb, float rTotalOhms) {
  if (numSteps > MAX_STEPS) numSteps = MAX_STEPS;
  numSteps_ = numSteps;
  uint8_t validCount = 0;

  for (uint8_t i = 0; i < numSteps; i++) {
    float attenDb = (numSteps <= 1)
                        ? 0.0f
                        : -rangeDb * (float)(numSteps - 1 - i) / (float)(numSteps - 1);
    float k   = powf(10.0f, attenDb / 20.0f);
    float rsh = k * rTotalOhms;
    float rs  = (1.0f - k) * rTotalOhms;

    Entry &e = entries_[i];
    e.targetDb  = attenDb;
    e.targetRsh = rsh;
    e.targetRs  = rs;

    uint16_t sDuty = 0, shDuty = 0;
    bool okS  = seriesCurve.dutyForResistance(rs, sDuty);
    bool okSh = shuntCurve.dutyForResistance(rsh, shDuty);
    e.seriesDuty = sDuty;
    e.shuntDuty  = shDuty;
    e.valid = okS && okSh;
    if (e.valid) validCount++;
  }
  return validCount;
}

uint8_t VolumeLut::solveFixedSeries(const LdrCurve &shuntCurve, uint8_t numSteps, float rangeDb,
                                     float rSeriesFixedOhms, uint16_t seriesFixedDuty) {
  if (numSteps > MAX_STEPS) numSteps = MAX_STEPS;
  numSteps_ = numSteps;
  uint8_t validCount = 0;

  for (uint8_t i = 0; i < numSteps; i++) {
    float attenDb = (numSteps <= 1)
                        ? 0.0f
                        : -rangeDb * (float)(numSteps - 1 - i) / (float)(numSteps - 1);
    float k = powf(10.0f, attenDb / 20.0f);

    // K = Rsh/(Rs+Rsh)  =>  Rsh = K*Rs/(1-K). Guard the K->1 singularity
    // (true 0dB needs Rsh->infinity with any nonzero fixed series) with a
    // large sentinel -- it will naturally fail the shunt curve's own
    // range check below rather than produce Inf/NaN.
    float rsh = (k >= 0.9999f) ? 1.0e9f : (k * rSeriesFixedOhms / (1.0f - k));

    Entry &e = entries_[i];
    e.targetDb = attenDb;
    e.targetRs = rSeriesFixedOhms;
    e.targetRsh = rsh;
    e.seriesDuty = seriesFixedDuty; // constant every step -- series never moves

    uint16_t shDuty = 0;
    bool ok = shuntCurve.dutyForResistance(rsh, shDuty);
    e.shuntDuty = shDuty;
    e.valid = ok;
    if (ok) validCount++;
  }
  return validCount;
}

// ---------------------------------------------------------------------------
// CalibrationSession
// ---------------------------------------------------------------------------
void CalibrationSession::beginCharacterization() {
  seriesCurve_.clear();
  shuntCurve_.clear();
  lut_ = VolumeLut();
  state_ = State::CHARACTERIZING;
}

void CalibrationSession::resetCurves() {
  seriesCurve_.clear();
  shuntCurve_.clear();
  lut_ = VolumeLut();
  state_ = State::IDLE;
}

uint8_t CalibrationSession::solveLut(uint8_t numSteps, float rangeDb, float rTotalOhms) {
  uint8_t valid = lut_.solve(seriesCurve_, shuntCurve_, numSteps, rangeDb, rTotalOhms);
  state_ = State::SOLVED;
  return valid;
}