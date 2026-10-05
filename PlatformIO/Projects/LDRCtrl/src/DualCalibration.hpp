#pragma once
#include <Arduino.h>
#include "LDRVolume.hpp"
#include "Config.hpp"

// ============================================================================
// DualCalibration
//
// The one place calibration sweeps live -- for one channel or two. Each
// duty point is driven on every channel at once and polled for
// convergence in alternation (the dominant cost is each LDR's own physical
// settling, not CPU time, and i2c0/i2c1 don't contend), which roughly
// halves a two-channel run versus doing them back to back.
//
// Beyond the coarse percent-spaced list, every sweep now REFINES ADAPTIVELY
// across the steep knee: wherever the resistance changes by more than
// ~1.4x between neighbouring points, it measures the midpoint, repeating
// until neighbours are ~6 duty counts apart. Real hardware showed why this
// matters: at Rtotal=30k the quiet end sits in the gap between two coarse
// points 41 counts apart where the curve falls ~30x, so interpolating
// across it was off by ~1.8x, and R's shunt curve had a 450-count hole.
//
// Built on LDRVolume's public API (calBegin / calDrive* / adcRead /
// calFeed*Point / calSolve* / calSave / relayEnergize).
// ============================================================================
namespace DualCalibration {

// FULL: characterizes 1 or 2 channels (coarse list + adaptive knee
// refinement), solves, saves, then TRIMS every step against a live
// measurement and saves again -- the sweep alone is systematically off by
// 2-3 duty counts (~25% in resistance at the knee) because it approaches
// each point from the dark side, so a calibration isn't finished until it
// is trimmed. Lands each channel on a defined step.
// FAST: a drift touch-up -- no sweep; keeps the existing curves/LUT and
// only re-measures and trims every step (touchUp()), then returns each
// channel to its CURRENT volume. If any channel has no usable calibration
// it falls back to FULL.
// Either way the audio path stays disconnected (relay energized) until
// each channel is back on its real operating point and has had
// CAL_RELAX_BEFORE_RECONNECT_MS to relax -- no loud burst on reconnect.
// labels: optional per-channel letters for output (e.g. "LR"), indexed like
// channels[]. A channel whose ADS1115 doesn't ACK is skipped (with a
// message); if none respond this does nothing.
void runChannels(LDRVolume *channels[], uint8_t count, LDRVolume::CalMode mode,
                 Stream &out, const char *labels = nullptr);

// runChannels() for the left/right pair, labelled "L"/"R".
void runBoth(LDRVolume &left, LDRVolume &right, LDRVolume::CalMode mode, Stream &out);

// Solve both channels from their existing curves with a STEREO-COMMON
// range: the smaller of the two channels' own computed ranges is used for
// both, so every step has the same dB on L and R. (Each channel's range
// comes from its own cell floors -- on real hardware R could reach 3.2 dB
// deeper than L at the quiet end, audibly unbalanced, when each channel
// used its own.) Does not save.
void solveBoth(LDRVolume &left, LDRVolume &right, Stream &out,
               uint8_t &validLeft, uint8_t &validRight,
               uint8_t steps = NUM_VOLUME_STEPS_DEFAULT);

// Trim touch-up of 1 or 2 EXISTING calibrations: energizes the relays,
// runs each channel's trim loop, saves (only if it completed), returns each
// channel to its current volume, waits for the cells to relax, and hands
// the audio back. This is what CAL FAST does. No sweep, curves untouched.
void touchUp(LDRVolume *channels[], uint8_t count, Stream &out,
             const char *labels = nullptr);

// For after a single-channel recalibration: if either channel's range
// differs from the common one, re-solve that channel (no sweep), save it,
// and trim it -- a re-solve discards the trimmed duties, so it is always
// followed by a touch-up. Does nothing if the ranges already match.
void equalizeRanges(LDRVolume &left, LDRVolume &right, Stream &out);

} // namespace DualCalibration