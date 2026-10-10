#include "Calibration.hpp"
#include "Config.hpp"
#include "DividerMath.hpp"
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
  // Above this, readings are dominated by the cell's slow dark-relaxation
  // lag rather than its settled resistance -- and the LUT never needs
  // resistances anywhere near this high (Rs/Rsh targets top out at
  // Rtotal, a few tens of kOhm).
  static constexpr float DARK_UNRELIABLE_OHMS = 150000.0f;
  if (count_ > 0 && ohms > points_[count_ - 1].ohms * MAX_RISE_TOLERANCE) {
    // A later, brighter point reading HIGHER than an earlier dark-end one
    // means the earlier one was still catching up from a brighter state
    // (lag), not that the later one is the outlier. Real hardware showed
    // exactly this: R's shunt sweep started at ~560k (lagging), so the
    // next real point (895k at duty 1023) was rejected -- leaving a
    // 450-count hole in the curve right where the knee is, and the trim
    // then had to guess across it. Drop trailing dark-end points that
    // this contradicts, then accept the new one. Anything below the dark
    // threshold is still rejected as before: a mid-range spike must not
    // be allowed to erase good earlier points.
    while (count_ > 0 && ohms > points_[count_ - 1].ohms * MAX_RISE_TOLERANCE &&
           points_[count_ - 1].ohms > DARK_UNRELIABLE_OHMS) {
      count_--;
    }
    if (count_ > 0 && ohms > points_[count_ - 1].ohms * MAX_RISE_TOLERANCE) return false;
  }
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

bool LdrCurve::dutyForResistanceFine(float targetOhms, uint32_t &fineOut) const {
  if (count_ < 2) return false;
  if (targetOhms > points_[0].ohms) return false;
  if (targetOhms < points_[count_ - 1].ohms) return false;
  for (uint8_t i = 0; i < count_ - 1; i++) {
    float hi = points_[i].ohms;
    float lo = points_[i + 1].ohms;
    if (targetOhms <= hi && targetOhms >= lo) {
      if (hi <= 0.0f || lo <= 0.0f) return false;
      if (hi == lo) { fineOut = (uint32_t)points_[i].duty * PWM_DITHER_LEVELS; return true; }
      float frac = (logf(hi) - logf(targetOhms)) / (logf(hi) - logf(lo));
      float d = (float)points_[i].duty + frac * (float)((int32_t)points_[i + 1].duty - (int32_t)points_[i].duty);
      long f = lroundf(d * (float)PWM_DITHER_LEVELS);
      fineOut = (uint32_t)(f < 0 ? 0 : f);
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
  const DividerMath::Load kLoad{AUDIO_SOURCE_OHMS, AMP_INPUT_LOAD_OHMS};

  for (uint8_t i = 0; i < numSteps; i++) {
    float attenDb = (numSteps <= 1)
                        ? 0.0f
                        : -rangeDb * (float)(numSteps - 1 - i) / (float)(numSteps - 1);
    float k   = powf(10.0f, attenDb / 20.0f);
    // Solved against the LOADED divider (amp input + buffer output impedance),
    // so attenDb is the real gain. Rsh >= rTotal means k is beyond what Rs -> 0
    // can reach: Rs = 0 then fails the series curve's range check (OUT OF RANGE).
    float rsh = DividerMath::rshForRtotal(k, rTotalOhms, kLoad);
    if (rsh > rTotalOhms) rsh = rTotalOhms;
    float rs  = rTotalOhms - rsh;

    Entry &e = entries_[i];
    e.targetDb  = attenDb;
    e.targetRsh = rsh;
    e.targetRs  = rs;

    uint32_t sFine = 0, shFine = 0;   // fractional duties: the dither engine resolves them
    bool okS  = seriesCurve.dutyForResistanceFine(rs, sFine);
    bool okSh = shuntCurve.dutyForResistanceFine(rsh, shFine);
    e.seriesFine = sFine;
    e.shuntFine  = shFine;
    e.valid = okS && okSh;
  }
  setTransparentTop(seriesCurve);
  for (uint8_t i = 0; i < numSteps_; i++) if (entries_[i].valid) validCount++;
  return validCount;
}

uint8_t VolumeLut::applyQuietSeek(uint32_t oldFine, uint32_t newFine, float settledFromOhms) {
  const int32_t delta = (int32_t)newFine - (int32_t)oldFine;
  uint8_t shifted = 0;
  for (uint8_t i = 0; i < numSteps_; i++) {
    Entry &e = entries_[i];
    if (!e.valid || isTransparent(i)) continue;
    if (e.seriesFine == oldFine) {
      e.seriesFine = newFine;
    } else if (delta != 0 && e.targetRs >= settledFromOhms) {
      const int32_t f = (int32_t)e.seriesFine + delta;
      e.seriesFine = f > (int32_t)PWM_DITHER_LEVELS ? (uint32_t)f : (uint32_t)PWM_DITHER_LEVELS;
      shifted++;
    }
  }
  return shifted;
}

void VolumeLut::setTransparentTop(const LdrCurve &seriesCurve) {
  if (numSteps_ == 0 || seriesCurve.empty()) return;
  // The same series resistance on both channels (TRANSPARENT_SERIES_OHMS); a cell whose
  // floor is above it gets its brightest measured point instead.
  uint32_t fine = 0;
  float rs = TRANSPARENT_SERIES_OHMS;
  if (!seriesCurve.dutyForResistanceFine(rs, fine)) {
    uint8_t lo = 0;
    for (uint8_t k = 1; k < seriesCurve.count(); k++) if (seriesCurve.point(k).ohms < seriesCurve.point(lo).ohms) lo = k;
    rs = seriesCurve.point(lo).ohms;
    fine = (uint32_t)seriesCurve.point(lo).duty * PWM_DITHER_LEVELS;
  }
  const DividerMath::Load kLoad{AUDIO_SOURCE_OHMS, AMP_INPUT_LOAD_OHMS};
  Entry &e = entries_[numSteps_ - 1];
  e.seriesFine = fine;
  e.shuntFine = 0;                  // shunt LED off
  e.targetRs = rs;
  e.targetRsh = 0.0f;               // open
  e.targetDb = 20.0f * log10f(DividerMath::gain(rs, 1.0e9f, kLoad));
  e.valid = true;
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
