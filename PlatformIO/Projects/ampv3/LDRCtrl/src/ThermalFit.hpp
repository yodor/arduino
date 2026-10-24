#pragma once
// Learning how temperature moves the knee: pure maths, host-testable.
//
// Every resync measures each channel's knee shift (PWM counts the quiet point had to move
// since the previous calibration) and ThermalLog records it next to how much the driver
// block and the 3.3 V rail changed over the same interval. Fitting
// shift = k * dT through the origin (no change -> no shift) gives counts per degree; R2
// says how much of the shift that one variable explains. Stage 2 applies the winner.
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

namespace ThermalFit {

struct Entry {
  float shift[2];   // knee shift, counts (L, R); NAN = not measured
  float dBlock;     // driver block, C
  float dRailPct;   // 3.3 V rail, percent
};

struct Slope {
  uint8_t n = 0;    // entries used
  float k = NAN;    // counts per unit (C or %)
  float r2 = NAN;   // fraction of the shift explained (through-origin fit)
  float spread = 0; // RMS of the variable's change -- a fit needs it to have moved
};

// shift[c] against x through the origin. xSel: 0 = block, 1 = rail.
// A resync whose quiet-point seek failed recorded no real shift: either "unknown" (current
// firmware) or, before 2026-10-25b, exactly 0.00 on both channels although the block had
// moved. The resync AFTER such a failure is contaminated too: its shift also contains the
// correction the failed one missed. Both are left out of every fit.
inline bool seekFailed(const Entry &e) {
  if (!isfinite(e.shift[0]) || !isfinite(e.shift[1])) return true;
  return e.shift[0] == 0.0f && e.shift[1] == 0.0f && isfinite(e.dBlock) && fabsf(e.dBlock) >= 0.5f;
}

inline Slope fit(const Entry *e, uint8_t n, uint8_t c, uint8_t xSel) {
  Slope s;
  double sxy = 0, sxx = 0, syy = 0;
  bool prevFailed = false;
  for (uint8_t i = 0; i < n; i++) {
    const bool failedNow = seekFailed(e[i]);
    const bool skip = failedNow || prevFailed;
    prevFailed = failedNow;
    if (skip) continue;
    const float y = e[i].shift[c];
    const float x = xSel == 0 ? e[i].dBlock : e[i].dRailPct;
    if (!isfinite(x) || !isfinite(y)) continue;   // NaN, or inf from NaN arithmetic on the RP2040
    sxy += (double)x * y; sxx += (double)x * x; syy += (double)y * y;
    s.n++;
  }
  if (s.n == 0) return s;
  s.spread = (float)sqrt(sxx / s.n);
  if (sxx <= 0) return s;
  s.k = (float)(sxy / sxx);
  // residual sum of squares of y - k x, against the total (through-origin R2)
  const double rss = syy - sxy * sxy / sxx;
  s.r2 = syy > 0 ? (float)(1.0 - rss / syy) : NAN;
  return s;
}

// Enough to trust: at least 3 resyncs and the variable moved by a meaningful RMS amount.
inline bool usable(const Slope &s, float minSpread) {
  return s.n >= 3 && s.spread >= minSpread && !isnan(s.k);
}

// One learning-log line -> entry. Two formats, told apart by their field count:
//   7 fields (since 2026-10-25c):  shiftL,shiftR,dBlock,dRail%,block,rail,uptime
//  11 fields (before, with cells): shiftL,shiftR,dBlock,dCellL,dCellR,dRail%,block,cellL,cellR,rail,uptime
// Anything else is not an entry. strtod reads "nan" (cells absent in old lines).
inline bool parseLogLine(const char *s, Entry &x) {
  float v[11];
  uint8_t k = 0;
  while (*s && k < 11) {
    while (*s == ',' || *s == ' ' || *s == '\r') s++;
    if (!*s) break;
    char *end;
    const double d = strtod(s, &end);
    if (end == s) break;
    v[k++] = (float)d;
    s = end;
  }
  if (k != 7 && k != 11) return false;
  x.shift[0] = v[0]; x.shift[1] = v[1]; x.dBlock = v[2];
  x.dRailPct = (k == 11) ? v[5] : v[3];
  return true;
}

} // namespace ThermalFit
