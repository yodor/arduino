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
void characterize(LDRVolume *channels[], uint8_t count, Stream &out, const char *labels = nullptr);

// characterize() for the left/right pair, labelled "L"/"R".
void characterizeBoth(LDRVolume &left, LDRVolume &right, Stream &out);

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
// the audio back. This is the RESYNC (and RESOLVE) step of CAL. No sweep, curves untouched.
void touchUp(LDRVolume *channels[], uint8_t count, Stream &out,
             const char *labels = nullptr, CalKind kind = CalKind::RESYNC);

// The cell check alone: a fresh fingerprint against the stored one (relay energized by
// the caller). Prints one line per channel; decides nothing.
CellState checkCells(LDRVolume *const *ch, uint8_t na, const char *labels, Stream &out);

// The single CAL: cell check, then FULL / RESYNC / RESOLVE+RESYNC as CellCheck::plan()
// decides; at boot a MATCH just keeps the stored calibration. Returns what the check found.
CellState calibrate(LDRVolume &left, LDRVolume &right, const CalRequest &req, Stream &out, bool boot);

// The settled dark-end walk (DarkProfile.hpp) on the given channels at once; the
// relay must be energized and the amp held muted by the caller. Fills prof[].
void walkDarkEnd(LDRVolume *const *ch, uint8_t na, const char *labels, DarkProfile *prof, Stream &out);
// Place the quiet-end series point with settled readings (after a solve/trim).
void settleQuietPoint(LDRVolume *const *ch, uint8_t na, const char *labels, Stream &out);

} // namespace DualCalibration
