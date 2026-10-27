#pragma once
// The quiet-point seek's per-probe decision (DualCalibration::settleQuietPoint) and the
// shunt guard (LDRVolume::guardShunt) as pure functions, host-tested on real data.
#include <stdint.h>
#include <math.h>

namespace QuietSeek {

constexpr float TOL = 1.015f;              // within 1.5 % of the target
constexpr float MAX_STEP_COUNTS = 8.0f;    // per probe (was 3: a 5 C change, ~30 counts, never caught up)
constexpr float DARK_JUMP_COUNTS = 6.0f;   // unreadable at the quiet step = far too dark: go brighter

struct Decision { uint32_t next; bool ok; float err; bool readable; };

// f: the duty just probed (fine units), r: its settled reading (<= 0 = unreadable),
// target: the quiet step's target Rs, perCount: ln(ohms) per count there, fpc: fine per count.
inline Decision decide(uint32_t f, float r, float target, float perCount, uint32_t fpc) {
  Decision d{};
  if (!(r > 0.0f)) {
    // So dark (several MOhm) that too little current flows to measure: after a big temperature
    // change the old duty is far too dark. Not a reason to stop -- that silently recorded
    // "no shift" -- but to go brighter.
    d.readable = false; d.ok = false; d.err = logf(10.0f);
    d.next = f + (uint32_t)lroundf(DARK_JUMP_COUNTS * (float)fpc);
    return d;
  }
  d.readable = true;
  d.err = logf(r / target);
  d.ok = fabsf(d.err) <= logf(TOL);
  float step = d.err / perCount;              // > 0: too dark -> brighter
  if (step > MAX_STEP_COUNTS) step = MAX_STEP_COUNTS;
  if (step < -MAX_STEP_COUNTS) step = -MAX_STEP_COUNTS;
  const int32_t nf = (int32_t)f + (int32_t)lroundf(step * (float)fpc);
  d.next = nf > (int32_t)fpc ? (uint32_t)nf : fpc;
  return d;
}

// Shunt guard over one channel's steps in order (use[i] false = skip: invalid/transparent).
// No step's shunt duty may be brighter than the previous used step's; returns how many changed.
inline uint8_t guardShunt(uint32_t *shunt, const bool *use, uint8_t n) {
  uint8_t fixed = 0; bool havePrev = false; uint32_t prev = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (!use[i]) continue;
    if (havePrev && shunt[i] > prev) { shunt[i] = prev; fixed++; }
    else prev = shunt[i];
    havePrev = true;
  }
  return fixed;
}

// Series guard over one channel's steps in order: the series target FALLS from step to step,
// so no step's series duty may be DARKER (lower) than the previous used step's. A violating
// step gets the stored curve's duty for its own target, shifted by the offset the previous
// step has from the curve (the driver's temperature shift is the same at every depth); never
// darker than the previous step. curve[i] = the curve's fine duty for step i's target, 0 if
// the curve has none. Returns how many changed.
inline uint8_t guardSeries(uint32_t *ser, const bool *use, const uint32_t *curve, uint8_t n) {
  uint8_t fixed = 0; int16_t prev = -1;
  for (uint8_t i = 0; i < n; i++) {
    if (!use[i]) continue;
    if (prev >= 0 && ser[i] < ser[prev]) {
      uint32_t est = ser[prev];
      if (curve[i] && curve[prev]) {
        const int64_t e = (int64_t)curve[i] + ((int64_t)ser[prev] - (int64_t)curve[prev]);
        if (e > (int64_t)ser[prev]) est = (uint32_t)e;
      }
      ser[i] = est; fixed++;
    }
    prev = (int16_t)i;
  }
  return fixed;
}

// Depth-dependent refinement of the deep steps after a resync's quiet-point seek. The seek
// measures one shift, at the quiet point, and every deep (curve-based) step gets it; real cells
// shifted a little more at middle depths (measured: x1.14..1.24 too dark between 84k and 990k).
// So a few middle-depth steps are probed (settled) and every deep step gets its own correction,
// interpolated in ln(Rs) between the probes, 0 at the quiet point (just matched by the seek).
namespace DeepRefine {
constexpr uint8_t NPROBE = 3;
constexpr float FRACTIONS[NPROBE] = {0.8f, 0.5f, 0.2f};  // of ln(lo..quiet), deepest probe first
constexpr float MAX_CORR_COUNTS = 12.0f;                 // a sanity bound per probe
constexpr float QUIET_SHARE = 0.9f;                      // steps at >= 0.9 x the quiet target ARE the quiet point

// deep[i]: step i is curve-based (its series stays as solved from the walk's curve). probe[k]:
// the deep step nearest to fraction k of the ln distance lo..quiet, or -1. Distinct steps only.
inline void chooseProbes(const float *rs, const bool *deep, uint8_t n, float lo, float quiet, int16_t probe[NPROBE]) {
  for (uint8_t k = 0; k < NPROBE; k++) probe[k] = -1;
  if (!(lo > 0.0f) || !(quiet > lo)) return;
  const float a = logf(lo), b = logf(quiet);
  for (uint8_t k = 0; k < NPROBE; k++) {
    const float want = a + FRACTIONS[k] * (b - a);
    float bestD = 1e9f;
    for (uint8_t i = 0; i < n; i++) {
      if (!deep[i] || !(rs[i] > 0.0f) || rs[i] >= QUIET_SHARE * quiet) continue;
      bool taken = false;
      for (uint8_t j = 0; j < k; j++) if (probe[j] == (int16_t)i) taken = true;
      if (taken) continue;
      const float d = fabsf(logf(rs[i]) - want);
      if (d < bestD) { bestD = d; probe[k] = (int16_t)i; }
    }
  }
}

// The correction (counts, + = brighter) for a deep step at target rs: 0 at/above the quiet
// point, linear in ln(rs) between the probes, the shallowest probe's below it.
inline float correctionAt(float rs, const float *pRs, const float *pCorr, uint8_t np, float quiet) {
  if (!(rs > 0.0f) || rs >= QUIET_SHARE * quiet) return 0.0f;
  // points sorted by ln R: the probes (any order in) + the anchor (quiet, 0)
  float x[NPROBE + 1], y[NPROBE + 1]; uint8_t m = 0;
  for (uint8_t k = 0; k < np; k++) if (pRs[k] > 0.0f && isfinite(pCorr[k])) { x[m] = logf(pRs[k]); y[m] = pCorr[k]; m++; }
  if (m == 0) return 0.0f;
  x[m] = logf(quiet); y[m] = 0.0f; m++;
  for (uint8_t i = 1; i < m; i++) for (uint8_t j = i; j > 0 && x[j] < x[j - 1]; j--) {
    float t = x[j]; x[j] = x[j - 1]; x[j - 1] = t; t = y[j]; y[j] = y[j - 1]; y[j - 1] = t;
  }
  const float lr = logf(rs);
  if (lr <= x[0]) return y[0];
  for (uint8_t i = 0; i + 1 < m; i++)
    if (lr <= x[i + 1]) return y[i] + (lr - x[i]) / (x[i + 1] - x[i]) * (y[i + 1] - y[i]);
  return 0.0f;
}
} // namespace DeepRefine

} // namespace QuietSeek
