#pragma once
#include <stdint.h>

// ============================================================================
// AmpHold -- "keep the amp muted for the whole of a calibration".
//
// Pure state, no hardware: Board owns one and does the pin writes. Nests (a
// calibration that runs a trim is two holds); only the OUTERMOST begin records
// the amp's state, and only the outermost end can give it back.
//
// The amp is restored to UNMUTED only if it was unmuted when the calibration
// began and the boot hold is not (any longer) in charge. If it was already
// muted -- by the master, the console, or the boot pre-latch -- it stays muted:
// a calibration must never un-mute something someone else muted. In the boot
// case Board::update()'s one-shot auto-release then decides when the amp opens,
// and it also refuses to fire while a hold is active.
// ============================================================================
struct AmpHold {
  uint8_t depth = 0;
  bool    mutedBefore = true;

  // Returns true: the caller must now force the amp muted.
  bool begin(bool currentlyMuted) {
    if (depth == 0) mutedBefore = currentlyMuted;
    if (depth < 255) depth++;
    return true;
  }

  // Returns true: the caller must now un-mute the amp (the hold is over and it
  // was open before). Unbalanced ends are ignored.
  bool end(bool bootHoldActive) {
    if (depth == 0) return false;
    depth--;
    return depth == 0 && !mutedBefore && !bootHoldActive;
  }

  bool active() const { return depth > 0; }
};
