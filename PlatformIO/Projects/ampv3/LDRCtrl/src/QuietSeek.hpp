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

} // namespace QuietSeek
