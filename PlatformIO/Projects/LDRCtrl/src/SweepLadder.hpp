#pragma once
#include <stdint.h>

// ============================================================================
// SweepLadder -- low-end extension of the CALAUTO sweep.
//
// The coarse list starts at 15% duty. That is below the knee for the original
// driver (series step 0 at 50k sits near duty 1040), but a larger base bleed
// moves the knee down to ~630 -- right at that first point, or below it. When
// the lowest reading is already low-resistance the curve has no dark end, step 0
// cannot be solved, and the whole quiet end of the range is lost.
//
// So after the coarse pass the sweep checks its lowest point. If any channel
// reads below BELOW_FACTOR x its Rtotal there, it measures a short ladder of
// lower duties (ascending, from a relaxed dark state, like every other point),
// prepends them, and checks again -- up to MAX_ROUNDS, each round reaching twice
// as far. With the original hardware the first reading is already deep in the
// dark, nothing triggers, and the sweep is exactly what it was.
//
// Pure functions, no hardware: tested on the host.
// ============================================================================
namespace SweepLadder {

constexpr uint8_t  POINTS       = 5;     // points measured per round
constexpr uint8_t  MAX_ROUNDS   = 4;     // reach: 1/64, 1/32, 1/16, 1/8 of WRAP below the anchor
constexpr float    BELOW_FACTOR = 2.0f;  // keep extending while lowest reading < this x Rtotal
constexpr uint16_t MIN_DUTY     = 24;    // never probe below this: the LED is simply off

// True if `ohms` is a valid reading that is still too low-resistance to be the
// dark end of the curve. An invalid reading (<= 0) means "too dark to read",
// which is dark enough.
inline bool tooLow(float ohms, float rTotalOhms) {
  return ohms > 0.0f && ohms < BELOW_FACTOR * rTotalOhms;
}

// Writes the ladder for round `round` (0-based) into out[0..n), in ASCENDING
// duty order, every entry strictly below `lowest` and >= MIN_DUTY. Returns n,
// or 0 when there is nothing left to probe.
inline uint8_t build(uint16_t lowest, uint8_t round, uint16_t wrap, uint16_t out[POINTS]) {
  if (lowest <= MIN_DUTY) return 0;
  uint32_t span = (uint32_t)(wrap / 64 < 8 ? 8 : wrap / 64) << round;
  const uint32_t room = (uint32_t)(lowest - MIN_DUTY);
  if (span > room) span = room;
  uint8_t n = 0;
  uint16_t prev = 0;
  for (uint8_t k = POINTS; k >= 1; k--) {
    const uint16_t off = (uint16_t)((span * k) / POINTS);
    if (off == 0) continue;                  // span too small for this rung
    const uint16_t d = (uint16_t)(lowest - off);
    if (n > 0 && d <= prev) continue;        // keep strictly ascending
    out[n++] = d;
    prev = d;
  }
  return n;
}

} // namespace SweepLadder