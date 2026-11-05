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

// A run's verdict, for the controller (VERIFY RESULT) and the resync hint. It rests on the
// SETTLED quiet point only (the quiet step held until the cells settle): the quick step readings
// (3 s after arriving) and the walk depend on what played minutes before -- CdS light history --
// by about +-2 counts, and a resync cannot change that, so they are reported but do not decide.
//   CHECK   the two channels' settled residuals more than 3 counts apart, or a channel's cells
//           changed shape (walk spread over 2.5 counts): worth a look on the console
//   RESYNC  a settled residual beyond 3 counts: the table is off, a resync fixes it. The cells
//           themselves wander about +-2 counts with what played in the last half hour (CdS light
//           history, a slow tail of tens of minutes, the same on both channels) -- 3 counts keeps
//           clear of that, so only offsets a resync can actually fix raise the hint.
//   OK      otherwise -- also when nothing settled was measured (a run from older firmware)
enum class Verdict : uint8_t { OK, RESYNC, CHECK };
constexpr float WANDER_RESYNC = 3.0f;   // counts: above the cells' history-dependent wander
// A temperature step only says something about the compensation when the shift it should cause
// clearly exceeds that wander (+-2 counts): at least 4 counts.
constexpr float WANDER_JUDGE = 4.0f;
inline const char *verdictName(Verdict v) { return v == Verdict::CHECK ? "CHECK" : v == Verdict::RESYNC ? "RESYNC" : "OK"; }
inline Verdict verdict(const float settled[2], const float spread[2]) {
  if (!isnan(settled[0]) && !isnan(settled[1]) && fabsf(settled[0] - settled[1]) > 3.0f) return Verdict::CHECK;
  for (uint8_t c = 0; c < 2; c++) if (!isnan(spread[c]) && spread[c] > 2.5f) return Verdict::CHECK;
  for (uint8_t c = 0; c < 2; c++) if (!isnan(settled[c]) && fabsf(settled[c]) > WANDER_RESYNC) return Verdict::RESYNC;
  return Verdict::OK;
}

} // namespace VerifyAnalysis
