#pragma once
#include <stdint.h>
#include <math.h>
#include "Calibration.hpp"

// ============================================================================
// CellCheck -- one CAL command; the firmware decides how much work it needs.
//
// FINGERPRINT. Each of the four cells is read at a few BRIGHT duties (its partner
// held bright), both channels at once. The duties are MEASURED, not fixed: at each full
// characterization chooseDuties() takes them from the channel's own curves -- the lowest
// where both cells are within BRIGHT_FACTOR x their floor (clear of the knee, wherever a
// given driver puts it: ~1000 counts with 100k bleeds, ~600 with 1M), the highest at the
// bright hold -- and they are stored with the fingerprint, so a later check reads exactly
// the same points. (Fixed duties chosen for one board sat right on the knee of another.) The bright end is the repeatable part of a
// vactrol: it settles in milliseconds, and the LED current there is set by the
// driver's NPN stage, not by a few millivolts of Vbe drift. Measured on this
// board, the same four cells a day apart (2026-10-08 vs 09) moved by at most 1.5%
// at every duty, while two DIFFERENT cells of the same series differed by
// 27..51% -- except one pair that matched within 1..3% at 2047..2866 counts and
// differed by 12% only at 1023. Hence several duties across the bright range, and
// a cell counts as CHANGED if it is more than MATCH_TOL off at ANY of them
// (3x the day-to-day change, well under the closest different pair).
//
// PLAN (plan() below, pure):
//   no stored calibration/fingerprint on a channel  -> FULL characterization
//   cells CHANGED                                   -> FULL characterization
//   cells MATCH, R unchanged                        -> RESYNC (trim + settled quiet point)
//   cells MATCH, new R (R=<ohms>, or R=AUTO)        -> RESOLVE from the stored
//                                                      measurements, then RESYNC
// At boot, a MATCH just uses the stored calibration (no resync).
// Pure header (no Arduino), tested on the host.
// ============================================================================
namespace CellCheck {
constexpr uint8_t  NFP = 4;
// A different cell differs at EVERY fingerprint duty (12-50 percent between real cells);
// drive drift (e.g. a cooler room overnight) moves mainly the duty nearest the knee, by a
// few percent. So a cell counts as CHANGED when at least TWO of its duties are more than
// MATCH_TOL off, or ANY single duty is more than STRONG_TOL off. (One-duty-over-5% alone
// was a false alarm on a 2 C cooler morning: x1.055 at the lowest duty, the rest close.)
constexpr float    MATCH_TOL = 1.05f;
constexpr float    STRONG_TOL = 1.20f;
// Temperature-aware: the bright-end fingerprint moves ~1.5 % per C of die-temperature difference
// (a cool start after a warm calibration read x1.06 at two duties -- a false CHANGED). The
// tolerances widen with the difference since the fingerprint was taken; a real cell swap at a
// similar temperature is still caught at 5 %.
constexpr float    TOL_PER_C = 0.015f;
constexpr float    MATCH_TOL_MAX = 1.20f;
inline float matchTol(float dT)  { float t = MATCH_TOL + TOL_PER_C * (isnan(dT) ? 0.0f : fabsf(dT)); return t > MATCH_TOL_MAX ? MATCH_TOL_MAX : t; }
inline float strongTol(float dT) { return STRONG_TOL + (matchTol(dT) - MATCH_TOL); }
constexpr uint8_t  MIN_OFF_DUTIES = 2;
constexpr uint32_t SETTLE_MS = 400;       // bright end: settled well within this
constexpr float    BRIGHT_FACTOR = 4.0f;  // lowest fingerprint duty: both cells within 4x their floor
} // namespace CellCheck

struct CellFingerprint {
  uint16_t duty[CellCheck::NFP]; // the PWM duties it was taken at (chosen from the curves)
  float ser[CellCheck::NFP];   // series cell, ohms at duty[] (shunt bright)
  float shu[CellCheck::NFP];   // shunt cell, ohms at duty[] (series bright)
  float tempC;                 // RP2040 die temperature when taken
  uint8_t valid;               // 1 = all readings present
  uint8_t pad[3];
};

// History stored with the calibration.
enum class CalKind : uint8_t { NONE = 0, FULL = 1, RESYNC = 2, RESOLVE = 3 };
struct CalMeta {
  uint32_t fullCount;          // full characterizations of these cells
  uint32_t resyncCount;        // resyncs (and re-solves) since the last full one
  float tempAtFull;            // die temperature at the last full characterization
  float tempAtLast;            // ... at the last CAL of any kind
  uint8_t lastKind;            // CalKind
  uint8_t pad[3];
};

enum class CellState : uint8_t { NO_DATA, MATCH, CHANGED };
enum class CalAction : uint8_t { FULL, RESYNC, RESOLVE_RESYNC };

struct CalRequest {
  bool hasR = false;           // R=<ohms>
  bool rAuto = false;          // R=AUTO
  long ohms = 0;
};

namespace CellCheck {

// Worst mismatch between two fingerprints, as a ratio >= 1 (0 if either is
// invalid). which: 0 = series, 1 = shunt; at = index into DUTIES.
inline float worst(const CellFingerprint &a, const CellFingerprint &b, uint8_t &which, uint8_t &at) {
  if (!a.valid || !b.valid) return 0.0f;
  float w = 1.0f; which = 0; at = 0;
  for (uint8_t e = 0; e < 2; e++) {
    for (uint8_t i = 0; i < NFP; i++) {
      const float x = e ? a.shu[i] : a.ser[i], y = e ? b.shu[i] : b.ser[i];
      if (!(x > 0.0f) || !(y > 0.0f)) return 0.0f;
      const float r = x > y ? x / y : y / x;
      if (r > w) { w = r; which = e; at = i; }
    }
  }
  return w;
}

// The fingerprint duties for one channel, from its own curves: the lowest duty at which BOTH
// cells are within BRIGHT_FACTOR x their lowest reading, then evenly up to `top` (the
// bright hold). Falls back to a fixed bright set if a curve is unusable.
inline void chooseDuties(const LdrCurve &series, const LdrCurve &shunt, uint16_t top, uint16_t *out) {
  uint16_t lo = 0;
  const LdrCurve *cv[2] = {&series, &shunt};
  bool ok = true;
  for (uint8_t e = 0; e < 2 && ok; e++) {
    const LdrCurve &c = *cv[e];
    if (c.count() < 2) { ok = false; break; }
    float floorOhms = c.point(0).ohms;
    for (uint8_t k = 1; k < c.count(); k++) if (c.point(k).ohms < floorOhms) floorOhms = c.point(k).ohms;
    uint16_t d = 0;
    for (uint8_t k = 0; k < c.count(); k++) {               // ascending duty: first point bright enough
      if (c.point(k).ohms <= BRIGHT_FACTOR * floorOhms) { d = c.point(k).duty; break; }
    }
    if (d == 0) { ok = false; break; }
    if (d > lo) lo = d;
  }
  if (!ok) lo = 1638;
  if (lo + 300 > top) lo = top - 300;
  for (uint8_t i = 0; i < NFP; i++) out[i] = (uint16_t)(lo + (uint32_t)(top - lo) * i / (NFP - 1));
}

// How many of one element's fingerprint duties are more than MATCH_TOL off, and its worst
// ratio. e: 0 = series, 1 = shunt. Returns false if a reading is missing.
inline bool elementOff(const CellFingerprint &a, const CellFingerprint &b, uint8_t e, uint8_t &nOff, float &worstR,
                       float tol = MATCH_TOL) {
  nOff = 0; worstR = 1.0f;
  for (uint8_t i = 0; i < NFP; i++) {
    const float x = e ? a.shu[i] : a.ser[i], y = e ? b.shu[i] : b.ser[i];
    if (!(x > 0.0f) || !(y > 0.0f)) return false;
    const float r = x > y ? x / y : y / x;
    if (r > tol) nOff++;
    if (r > worstR) worstR = r;
  }
  return true;
}

// The duties beyond MATCH_TOL on the element that decided the verdict (the larger count).
inline float tempDiff(const CellFingerprint &a, const CellFingerprint &b) {
  return (isnan(a.tempC) || isnan(b.tempC)) ? NAN : b.tempC - a.tempC;
}
inline uint8_t offDuties(const CellFingerprint &a, const CellFingerprint &b) {
  const float tol = matchTol(tempDiff(a, b));
  uint8_t best = 0;
  for (uint8_t e = 0; e < 2; e++) {
    uint8_t n; float w;
    if (elementOff(a, b, e, n, w, tol) && n > best) best = n;
  }
  return best;
}

inline CellState judge(bool haveStored, const CellFingerprint &stored, const CellFingerprint &now) {
  if (!haveStored || !stored.valid) return CellState::NO_DATA;
  for (uint8_t i = 0; i < NFP; i++) if (stored.duty[i] != now.duty[i] || stored.duty[i] == 0) return CellState::NO_DATA;
  if (!stored.valid || !now.valid) return CellState::CHANGED;
  const float dT = tempDiff(stored, now);
  for (uint8_t e = 0; e < 2; e++) {
    uint8_t n; float w;
    if (!elementOff(stored, now, e, n, w, matchTol(dT))) return CellState::CHANGED; // a reading failed: do not trust it
    if (n >= MIN_OFF_DUTIES || w > strongTol(dT)) return CellState::CHANGED;
  }
  return CellState::MATCH;
}

inline CalAction plan(CellState cs, const CalRequest &req, bool currentlyAuto) {
  if (cs != CellState::MATCH) return CalAction::FULL;
  if (req.hasR || (req.rAuto && !currentlyAuto)) return CalAction::RESOLVE_RESYNC;
  return CalAction::RESYNC;
}

} // namespace CellCheck
