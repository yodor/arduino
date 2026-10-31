#pragma once
#include <stdint.h>

// ============================================================================
// Dither -- the per-channel sigma-delta that turns a FINE duty (count x levels +
// fraction) into one hardware PWM level per wrap.
//
// Each wrap: acc += frac; if acc >= LEVELS then emit base+1 and acc -= LEVELS,
// else emit base. Over any LEVELS consecutive wraps the emitted levels sum to
// exactly LEVELS*base + frac, so the average is exact; the pattern spreads the
// "+1"s as evenly as a first-order modulator can, so its lowest frequency is
// PWM/LEVELS. frac == 0 never toggles: undithered duties behave exactly as before.
//
// Pure and header-only: the PWM interrupt uses it (forced inline, so it runs
// from RAM with the handler), and the host test checks it exhaustively.
// ============================================================================
template <uint32_t LEVELS>
struct DitherChannel {
  static_assert(LEVELS >= 1 && (LEVELS & (LEVELS - 1)) == 0, "LEVELS must be a power of two");
  uint32_t acc = 0;

  // `fine` = count * LEVELS + fraction; `maxLevel` = the PWM wrap (never exceeded).
  __attribute__((always_inline)) inline uint16_t next(uint32_t fine, uint16_t maxLevel) {
    const uint32_t base = fine / LEVELS;            // a shift: LEVELS is a power of two
    const uint32_t frac = fine & (LEVELS - 1);
    uint32_t level = base;
    if (frac) {
      acc += frac;
      if (acc >= LEVELS) { acc -= LEVELS; level = base + 1; }
    }
    return (uint16_t)(level > maxLevel ? maxLevel : level);
  }
};
