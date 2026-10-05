#pragma once
#include <math.h>

// ============================================================================
// DividerMath -- the series/shunt divider as it is actually loaded.
//
//   source (Rsrc) --- Rs ---+--- out --> amp input (Rload to virtual ground)
//                           |
//                          Rsh
//                           |
//                          GND
//
//   K = P / (Rsrc + Rs + P),   P = Rsh || Rload
//
// The old solve used the bare ratio Rsh/(Rs+Rsh), i.e. assumed nothing loads the
// divider. The amp board's 10k input resistor does -- by up to 7 dB at steps
// 24-29 with Rtotal = 50k -- and the JFET buffer's ~400 ohm output adds a little
// more at the loud end. Solving against the loaded gain makes every step's dB
// (and so STATUS DB=) the real one. loadOhms <= 0 means "unloaded" and
// reduces to the old formulas exactly.
//
// Pure functions, no hardware: tested on the host.
// ============================================================================
namespace DividerMath {

struct Load {
  float srcOhms;   // source output impedance in series with Rs (JFET buffer)
  float loadOhms;  // amp input resistance to ground; <= 0: unloaded
};

constexpr float UNREACHABLE_OHMS = 1.0e9f; // sentinel: fails any curve's range check

inline float parallel(float a, float b) { return (a * b) / (a + b); }

// Loaded gain of a divider with the given cell resistances.
inline float gain(float rs, float rsh, const Load &l) {
  const float p = (l.loadOhms > 0.0f) ? parallel(rsh, l.loadOhms) : rsh;
  return p / (l.srcOhms + rs + p);
}

// CONSTANT_RTOTAL: Rs + Rsh = rTotal. Returns the Rsh that gives gain k (0<k<1).
// The result can exceed rTotal -- then k is beyond what Rs -> 0 can reach, and the
// caller's Rs = rTotal - Rsh falls at/below zero and fails the series curve's range
// check, which is the correct "OUT OF RANGE".
// Exact (a quadratic), no iteration:
//   with c = k/(1-k), S = rTotal + Rsrc:   c x^2 + (L(1+c) - cS) x - c S L = 0
inline float rshForRtotal(float k, float rTotal, const Load &l) {
  if (k <= 0.0f) return 0.0f;
  if (k >= 0.9999f) return UNREACHABLE_OHMS;
  const float s = rTotal + l.srcOhms;
  if (l.loadOhms <= 0.0f) return k * s;
  const float c = k / (1.0f - k);
  const float L = l.loadOhms;
  const float b = L * (1.0f + c) - c * s;
  const float q = c * s * L;
  const float sq = sqrtf(b * b + 4.0f * c * q);
  return (b >= 0.0f) ? (2.0f * q) / (b + sq)   // stable form: no cancellation at small k
                     : (sq - b) / (2.0f * c);
}

// FIXED_SERIES: Rs is whatever the fixed duty gives. Returns the Rsh for gain k, or
// UNREACHABLE_OHMS when k is above what Rsh -> infinity can reach (with a load that
// ceiling is L/(Rsrc+Rs+L), below 1).
inline float rshForFixedSeries(float k, float rs, const Load &l) {
  if (k <= 0.0f) return 0.0f;
  if (k >= 0.9999f) return UNREACHABLE_OHMS;
  const float p = k * (l.srcOhms + rs) / (1.0f - k); // required Rsh || Rload
  if (l.loadOhms <= 0.0f) return p;
  if (p >= l.loadOhms) return UNREACHABLE_OHMS;
  return (p * l.loadOhms) / (l.loadOhms - p);
}

} // namespace DividerMath