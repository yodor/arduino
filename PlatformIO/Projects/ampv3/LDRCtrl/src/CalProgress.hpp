#pragma once
// What a running calibration is doing, for the master's display (protocol 8: PROGRESS).
// Phases carry real progress where it exists (trim step, the hold's countdown, seek probes),
// otherwise time against the phase's nominal duration (capped below 100 %). The nominal
// durations are from logged runs on the v1b hardware: full ~378 s, resync ~137 s.
#include <Arduino.h>

namespace CalProgress {

enum class Phase : uint8_t { IDLE, CHECK, SWEEP_SERIES, SWEEP_SHUNT, WALK, SOLVE, TRIM, HOLD, SEEK, SAVE, DONE };
enum class Kind : uint8_t { NONE, FULL, RESYNC, RESOLVE };
constexpr uint8_t NPHASES = 11;

inline const char *phaseName(Phase p) {
  static const char *N[NPHASES] = {"IDLE", "CHECK", "SWEEP_SERIES", "SWEEP_SHUNT", "WALK", "SOLVE",
                                   "TRIM", "HOLD", "SEEK", "SAVE", "DONE"};
  return N[(uint8_t)p < NPHASES ? (uint8_t)p : 0];
}
inline const char *kindName(Kind k) {
  return k == Kind::FULL ? "FULL" : k == Kind::RESYNC ? "RESYNC" : k == Kind::RESOLVE ? "RESOLVE" : "NONE";
}

// Nominal seconds of a phase in a kind of calibration (0 = not part of it). NONE = the cell
// check before the kind is decided (estimated as a resync, the common case).
inline uint16_t nominal(Kind k, Phase p) {
  switch (k) {
    case Kind::FULL:
      switch (p) { case Phase::SWEEP_SERIES: return 80; case Phase::SWEEP_SHUNT: return 80;
                   case Phase::WALK: return 75; case Phase::SOLVE: return 3; case Phase::TRIM: return 45;
                   case Phase::HOLD: return 60; case Phase::SEEK: return 30; case Phase::SAVE: return 5;
                   default: return 0; }
    case Kind::RESOLVE:
      if (p == Phase::SOLVE) return 3;
      // fall through: otherwise a resync
    case Kind::RESYNC:
    case Kind::NONE:
      switch (p) { case Phase::CHECK: return 8; case Phase::TRIM: return 40; case Phase::HOLD: return 60;
                   case Phase::SEEK: return 25; case Phase::SAVE: return 4; default: return 0; }
  }
  return 0;
}

// Percent done (0..99, 100 only at DONE) and seconds left, given the current phase and how
// far into it we are (0..1).
inline void estimate(Kind k, Phase p, float frac, uint8_t &pct, uint16_t &eta) {
  if (p == Phase::DONE) { pct = 100; eta = 0; return; }
  if (p == Phase::IDLE) { pct = 0; eta = 0; return; }
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  uint32_t total = 0, before = 0;
  for (uint8_t i = 1; i < NPHASES - 1; i++) {
    const uint16_t d = nominal(k, (Phase)i);
    total += d;
    if (i < (uint8_t)p) before += d;
  }
  const float cur = nominal(k, p);
  const float done = before + frac * cur;
  if (total == 0) { pct = 0; eta = 0; return; }
  float pc = 100.0f * done / total;
  if (pc > 99.0f) pc = 99.0f;
  pct = (uint8_t)pc;
  const float left = total - done;
  eta = (uint16_t)(left > 0 ? left + 0.5f : 0);
}

// ---- runtime ----
void begin();                                   // a CAL starts (CHECK, kind not yet known)
void setKind(Kind k);
void phase(Phase p, char side = 0, uint8_t part = 0, uint8_t parts = 1);
void step(uint16_t i, uint16_t n);              // progress inside the phase (i of n done)
void end();                                     // DONE (stays until the next begin)
void report(Print &out);                        // " KIND=.. PHASE=.. [SIDE=..] [STEP=i/n] PCT=.. ETA=.."

} // namespace CalProgress
