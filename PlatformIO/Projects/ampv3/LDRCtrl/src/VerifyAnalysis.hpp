#pragma once
// What changed between two VERIFY runs -- pure, host-tested.
//
// The walk is measured with the temperature compensation ACTIVE, so a point's effective drive is
// its commanded duty plus that run's compensation offset. The shift between two walks is
// therefore taken at equal EFFECTIVE drive: for each deep point of the newer walk (resistance R
// at effective duty d + offNow), the effective duty at which the older walk had the same R.
//   mean   how far the cells moved (counts; negative = they now need less drive for the same R)
//   spread max - min over the depths: small = a uniform shift (temperature-like), large = the
//          shape changed (some depths moved more than others)
// Divided by the block temperature change, a uniform shift is an independent measurement of
// counts per C -- comparable with the learned slope the compensation uses.
#include <math.h>
#include "DarkProfile.hpp"

namespace VerifyAnalysis {

struct Shift { float mean = NAN, min = NAN, max = NAN; uint8_t n = 0; };

inline Shift walkShift(const DarkProfile &ref, float refOff, const DarkProfile &now, float nowOff,
                       float rMin = 40000.0f, float rMax = 4000000.0f) {
  Shift s;
  if (ref.okCount() < 2) return s;
  const float lo = ref.shallowestOkOhms(), hi = ref.deepestOkOhms();
  double sum = 0;
  for (uint8_t i = 0; i < now.okCount(); i++) {
    const float r = now.p[i].ohms;
    if (!(r >= rMin && r <= rMax) || r < lo || r > hi) continue;    // only where both walks measured
    const float dRef = DarkProfileMath::dutyAt(ref, r);
    if (isnan(dRef)) continue;
    const float d = ((float)now.p[i].duty + nowOff) - (dRef + refOff);
    sum += d;
    if (s.n == 0 || d < s.min) s.min = d;
    if (s.n == 0 || d > s.max) s.max = d;
    s.n++;
  }
  if (s.n) s.mean = (float)(sum / s.n);
  return s;
}

// Step regions: 0 = quiet (the series at the quiet point), 1 = middle (deep, from the walk's
// curve), 2 = loud. Mean level error per region over the measured steps (NAN if none).
enum : uint8_t { QUIET = 0, MIDDLE = 1, LOUD = 2 };
inline uint8_t regionOf(float targetRs, float rTotal) {
  if (targetRs >= 0.9f * rTotal) return QUIET;
  return targetRs >= 20000.0f ? MIDDLE : LOUD;
}
inline void regionMeans(const float *meas, const float *target, const uint8_t *region, uint8_t n, float out[3]) {
  double sum[3] = {0, 0, 0}; uint8_t cnt[3] = {0, 0, 0};
  for (uint8_t i = 0; i < n; i++) {
    if (isnan(meas[i]) || isnan(target[i]) || region[i] > 2) continue;
    sum[region[i]] += meas[i] - target[i]; cnt[region[i]]++;
  }
  for (uint8_t r = 0; r < 3; r++) out[r] = cnt[r] ? (float)(sum[r] / cnt[r]) : NAN;
}

// Plain-language classification of a channel pair's walk shifts.
//   uniform: both spreads within `uniformCounts`; common: the two means within `commonCounts`.
inline bool uniform(const Shift &a, float uniformCounts = 2.5f) { return a.n >= 2 && (a.max - a.min) <= uniformCounts; }
inline bool common(const Shift &l, const Shift &r, float commonCounts = 2.0f) {
  return l.n && r.n && fabsf(l.mean - r.mean) <= commonCounts;
}

// A run's verdict, for the controller (VERIFY RESULT) and the resync hint:
//   CHECK   L and R are off differently (residuals more than 3 counts apart), or a channel's
//           cells changed shape (spread over 2.5 counts): worth a look on the console
//   RESYNC  the table is off -- a residual beyond 3 counts, or the middle steps' mean error
//           beyond 1 dB -- and a resync fixes that
//   OK      otherwise
// NAN inputs (not measured) do not count.
enum class Verdict : uint8_t { OK, RESYNC, CHECK };
inline const char *verdictName(Verdict v) { return v == Verdict::CHECK ? "CHECK" : v == Verdict::RESYNC ? "RESYNC" : "OK"; }
inline Verdict verdict(const float residual[2], const float spread[2], const float middleDb[2]) {
  if (!isnan(residual[0]) && !isnan(residual[1]) && fabsf(residual[0] - residual[1]) > 3.0f) return Verdict::CHECK;
  for (uint8_t c = 0; c < 2; c++) if (!isnan(spread[c]) && spread[c] > 2.5f) return Verdict::CHECK;
  for (uint8_t c = 0; c < 2; c++) {
    if (!isnan(residual[c]) && fabsf(residual[c]) > 3.0f) return Verdict::RESYNC;
    if (!isnan(middleDb[c]) && fabsf(middleDb[c]) > 1.0f) return Verdict::RESYNC;
  }
  return Verdict::OK;
}

} // namespace VerifyAnalysis
