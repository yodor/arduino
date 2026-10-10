#pragma once
#include <stdint.h>
#include <math.h>

// ============================================================================
// DarkProfile -- what a full calibration MEASURES about one series cell deep in
// the dark, where the sweep cannot be trusted: the sweep reads every point coming
// up out of the dark, so its dark end is 2-3x optimistic.
//
// A full characterization walks each series cell darker from about START_OHMS, at whole-count
// duties, both channels at once (shunt held bright, as at the quiet end). Each
// point is:
//   * approached from APPROACH_COUNTS darker -- brightening settles in seconds,
//     darkening takes about a minute -- and read once its readings stop moving
//     (the last 3, one second apart, within SETTLE_BAND);
//   * then checked for MEMORY, the way real listening does it: the loudest real
//     state -- the transparent top step: series at TRANSPARENT_SERIES_OHMS, shunt
//     LED off -- for LOUD_MS, then back to the point and read after RETURN_MS, the
//     few seconds of settling this project accepts. memory = that reading vs the settled one, as a factor >= 1: how far
//     the quiet level sits off after loud listening, once the cells have settled.
//     (A plain full-bright excursion with the shunt left alone barely shows it:
//     on the bench x1.03-1.10, against x1.37 for one cell after a real loud->quiet
//     change at the same depth.)
// The walk aims for about a doubling of resistance per point and stops at
// TOP_OHMS (the deepest Rtotal a calibration will consider), at MAX_POINTS, or
// at the first point that does not settle, cannot be read, or is not darker than
// the one before: that point is kept with ok=0 and marks the end of what this
// cell can do.
//
// Nothing here describes particular hardware. Swap a cell; the next CAL sees it, and the
// profile -- and with it RMAX and the AUTO choice -- follows the new cell.
// Pure header (no Arduino), tested on the host.
// ============================================================================
namespace DarkWalk {
constexpr float    START_OHMS      = 25000.0f;
constexpr float    TOP_OHMS        = 400000.0f;
constexpr float    STEP_RATIO      = 2.0f;
constexpr uint8_t  FIRST_STEP      = 2;      // counts, before a slope is known
constexpr uint8_t  MAX_STEP        = 6;
constexpr uint8_t  APPROACH_COUNTS = 2;
constexpr uint32_t APPROACH_MS     = 6000;
constexpr uint32_t SETTLE_POLL_MS  = 1000;
constexpr uint32_t SETTLE_MAX_MS   = 20000;
constexpr float    SETTLE_BAND     = 1.02f;
constexpr uint32_t LOUD_MS          = 10000;
constexpr uint32_t RETURN_MS        = 5000;      // the accepted settling time
constexpr float    MIN_RISE        = 1.05f;  // each point must read at least 5% darker than the last

// The next duty to try, from the last two settled points (or FIRST_STEP before
// there are two). Duties fall as the walk goes darker.
inline int32_t nextDuty(int32_t lastDuty, float lastOhms, int32_t prevDuty, float prevOhms, bool havePrev) {
  int32_t step = FIRST_STEP;
  if (havePrev && prevDuty > lastDuty && lastOhms > prevOhms) {
    const float perCount = logf(lastOhms / prevOhms) / (float)(prevDuty - lastDuty);
    if (perCount > 0.0f) {
      step = (int32_t)lroundf(logf(STEP_RATIO) / perCount);
      if (step < 1) step = 1;
      if (step > MAX_STEP) step = MAX_STEP;
    }
  }
  return lastDuty - step;
}
} // namespace DarkWalk

struct DarkPoint {
  uint16_t duty;    // whole PWM counts
  uint8_t ok;       // 1 = settled, readable, darker than the point before
  uint8_t settleS;  // seconds it took to settle (SETTLE_MAX_MS/1000 if it never did)
  float ohms;       // settled resistance, arrived from the dark side
  float memory;     // >= 1: level after a loud passage vs settled (0 = not measured)
};

struct DarkProfile {
  static constexpr uint8_t MAX_POINTS = 8;
  DarkPoint p[MAX_POINTS];
  uint8_t n = 0;

  void clear() { n = 0; }
  bool add(const DarkPoint &d) {
    if (n >= MAX_POINTS) return false;
    p[n++] = d;
    return true;
  }
  // Walk order: brightest (lowest ohms) first. ok points form a prefix.
  uint8_t okCount() const {
    uint8_t k = 0;
    while (k < n && p[k].ok) k++;
    return k;
  }
  float deepestOkOhms() const { const uint8_t k = okCount(); return k ? p[k - 1].ohms : 0.0f; }
  float shallowestOkOhms() const { return okCount() ? p[0].ohms : 0.0f; }

  // Memory at resistance r from the ok points, log-interpolated in ohms.
  // Below the first point: the first point's value. Above the deepest ok point:
  // unknown -> false (the calibration never extrapolates into the dark).
  bool memoryAt(float r, float &h) const {
    const uint8_t k = okCount();
    if (k == 0 || r > p[k - 1].ohms * 1.0001f) return false;
    if (r <= p[0].ohms) { h = p[0].memory; return true; }
    for (uint8_t i = 0; i + 1 < k; i++) {
      if (r <= p[i + 1].ohms) {
        const float f = logf(r / p[i].ohms) / logf(p[i + 1].ohms / p[i].ohms);
        h = p[i].memory + f * (p[i + 1].memory - p[i].memory);
        return true;
      }
    }
    h = p[k - 1].memory;
    return true;
  }
};
