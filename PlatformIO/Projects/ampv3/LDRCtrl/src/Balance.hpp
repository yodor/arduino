#pragma once
// Balance (protocol 18: BAL <n>, BAL). n = -16..+16 in 0.25 dB units (+-4 dB): negative lowers
// the RIGHT channel, positive the LEFT; 0 = centre, where nothing changes at all. The lowered
// channel plays BETWEEN its table's steps: the target resistances are interpolated on a log
// scale between the two neighbouring steps and mapped to duties through the stored curves plus
// each step's trimmed offset from its curve (exact at the steps themselves). Not stored: like the
// volume, the master keeps it and sends it after power-up.
#include <stdint.h>
#include <math.h>

class LDRVolume;

namespace Balance {

constexpr float  DB_PER_UNIT = 0.25f;
constexpr int8_t MAX_UNITS = 16;

// The attenuation (dB, >= 0) balance n gives one channel.
inline float attenuationDb(int8_t n, bool right) {
  if (right) return n < 0 ? -n * DB_PER_UNIT : 0.0f;
  return n > 0 ? n * DB_PER_UNIT : 0.0f;
}

// Where a level `att` dB below step `step` lies in a table of levels db[] (valid[] marks usable
// steps; louder = higher index): between step `up` (louder) and `lo` (quieter), fraction f from
// up (0) to lo (1). up == lo when it falls on a step, or below the quietest step (clamped).
inline void locate(const float *db, const bool *valid, uint8_t n, uint8_t step, float att,
                   uint8_t &up, uint8_t &lo, float &f) {
  up = lo = step; f = 0.0f;
  if (!(att > 0.0f) || step >= n) return;
  const float want = db[step] - att;
  int16_t prev = step;
  for (int16_t k = step; k >= 0; k--) {
    if (!valid[k]) continue;
    if (db[k] <= want) {
      if (k == step || fabsf(db[k] - want) < 1e-4f) { up = lo = (uint8_t)k; return; }
      up = (uint8_t)prev; lo = (uint8_t)k;
      f = (db[up] - want) / (db[up] - db[lo]);
      return;
    }
    prev = k;
  }
  up = lo = (uint8_t)prev;                       // below the quietest step: stay there
}

// The divider's level (dB) with source rsrc and load rl (0 = unloaded).
inline float levelDb(float rs, float rsh, float rsrc, float rl) {
  const float p = rl > 0.0f ? rsh * rl / (rsh + rl) : rsh;
  return 20.0f * log10f(p / (rsrc + rs + p));
}

// The resistances that give level `want` between step `up` (louder) and `lo` (quieter), under the
// rules the table was built with: between ordinary steps Rs + Rsh is constant (Rtotal), so the
// shunt is found by bisection (on a log scale) and the series is the rest; between the
// TRANSPARENT top (shunt off) and the step below, the shunt stays at that step's dark value and
// the series follows from the divider formula directly.
inline void solveBetween(float want, float rsU, float rshU, bool upTransparent, float rsL, float rshL,
                         float rsrc, float rl, float &rs, float &rsh) {
  if (upTransparent) {
    rsh = rshL;
    const float p = rl > 0.0f ? rsh * rl / (rsh + rl) : rsh;
    const float g = powf(10.0f, want / 20.0f);
    rs = p / g - p - rsrc;
    if (rs < rsU) rs = rsU;
    if (rs > rsL) rs = rsL;
    return;
  }
  const float total = rsL + rshL;
  float a = logf(rshL), b = logf(rshU);            // quieter -> smaller shunt
  if (a > b) { const float t = a; a = b; b = t; }
  for (uint8_t i = 0; i < 40; i++) {
    const float m = 0.5f * (a + b), x = expf(m);
    const float lv = levelDb(total - x, x, rsrc, rl);
    if (lv < want) a = m; else b = m;               // louder with a larger shunt
  }
  rsh = expf(0.5f * (a + b));
  rs = total - rsh;
}

inline float lnInterp(float a, float b, float f) {
  if (!(a > 0.0f) || !(b > 0.0f)) return a + f * (b - a);
  return expf(logf(a) + f * (logf(b) - logf(a)));
}

// ---- runtime ----
int8_t get();
void set(int8_t n, LDRVolume &left, LDRVolume &right);   // clamps to +-MAX_UNITS, applies at once

} // namespace Balance
