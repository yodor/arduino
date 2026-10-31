#pragma once
#include <stdint.h>

// ============================================================================
// SweepLadder -- low-end extension of the calibration sweep.
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

// ---------------------------------------------------------------------------
// Dark-edge bisection. A steep knee can jump from UNREADABLE (too dark for the ADC,
// reported as ohms <= 0) straight to a reading far below what the LUT needs, within a
// dozen counts -- the 1M-bleed driver's shunt went from ERR at duty 577 to 37.5k at 589,
// so the loudest steps (which need ~40-48k) could not be solved. The low-end ladder does
// not help there (its darkest point is already unreadable, which counts as dark enough).
//
// For ONE channel's ascending samples: if the darkest READABLE sample is still below
// BELOW_FACTOR x Rtotal and the next darker sample is more than one count away, return
// the duty halfway between them. Each call halves the gap.
// ---------------------------------------------------------------------------
constexpr uint8_t EDGE_MAX_PASSES = 7;   // 128-count gap -> 1 count (the coarse list is ~41 apart at the knee)
constexpr uint8_t EDGE_REREADS    = 2;   // re-reads of a timed-out unreadable point next to the edge

// Index of the first sample of the CONTINUOUS readable run that ends at the bright end
// (samples in ascending duty), or -1 if the brightest sample is unreadable. Readable
// samples before an unreadable one are not part of it: a cell still recovering from being
// held bright reads at the darkest duties, then goes properly dark (unreadable) -- a
// physically impossible order for settled readings, i.e. a lag island.
// A SINGLE unreadable sample between two readable ones is a read glitch and is bridged;
// a gap of two or more unreadable samples (the cell really went dark) ends the run.
inline int brightRunStart(const float *ohms, uint8_t n) {
  if (n == 0 || !(ohms[n - 1] > 0.0f)) return -1;
  int v = (int)n - 1;
  while (v > 0) {
    if (ohms[v - 1] > 0.0f) { v--; continue; }
    if (v >= 2 && ohms[v - 2] > 0.0f) { v -= 2; continue; }  // lone glitch: bridge it
    break;
  }
  return v;
}

// Mark every lag island (readable samples darker than an unreadable one) unreadable, so it
// can neither enter the curve nor hide the real dark edge. Returns how many were dropped.
inline uint8_t dropLagIslands(float *ohms, uint8_t n) {
  const int v = brightRunStart(ohms, n);
  uint8_t dropped = 0;
  for (int i = 0; i < v; i++) if (ohms[i] > 0.0f) { ohms[i] = 0.0f; dropped++; }
  return dropped;
}

inline bool darkEdgeMid(const uint16_t *duty, const float *ohms, uint8_t n, float rTotalOhms, uint16_t &mid) {
  // The real dark edge: where the continuous readable run towards the bright end begins.
  const int v = brightRunStart(ohms, n);
  if (v <= 0) return false;                     // nothing readable, or nothing darker was measured
  if (!tooLow(ohms[v], rTotalOhms)) return false; // already dark enough for this Rtotal
  const uint16_t lo = duty[v - 1], hi = duty[v];
  if (hi <= lo + 1) return false;               // adjacent counts: nothing left to split
  mid = (uint16_t)(lo + (hi - lo) / 2);
  return true;
}

// The edge has closed to one count but its first readable point is still not dark enough,
// and the neighbour is "unreadable". A coarse reading taken while the cell was still rising
// from fully dark can simply time out (a 2-count step cannot go from ~300k to beyond the
// sense range), so that point deserves one settled re-read. Returns its duty in `lo`.
inline bool edgeStalled(const uint16_t *duty, const float *ohms, uint8_t n, float rTotalOhms, uint16_t &lo) {
  const int v = brightRunStart(ohms, n);
  if (v <= 0 || !tooLow(ohms[v], rTotalOhms)) return false;
  if (duty[v] > duty[v - 1] + 1) return false;    // still a gap to split: darkEdgeMid's job
  lo = duty[v - 1];
  return true;
}

} // namespace SweepLadder
