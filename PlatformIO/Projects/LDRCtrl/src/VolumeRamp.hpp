#pragma once
#include <Arduino.h>
#include "LDRVolume.hpp"

// ============================================================================
// VolumeRamp
//
// A direct VOL <n> request can jump an arbitrary distance in one command
// (e.g. VOL 20 -> VOL 1) -- confirmed on real hardware that applying that
// large a resistance change to the LDR network in a single instantaneous
// step produces an audible pop. This smooths it by advancing one count at
// a time toward the target, pausing briefly between each, instead of
// jumping straight there.
//
// Takes 1 or 2 channels (mirrors SerialConsole's own SideSelection
// pattern) since a bench VOL command can target either side alone or
// both together, and each channel ramps from its OWN current step --
// they don't need to start from the same position (e.g. one channel
// degraded to its LDR mute() fallback while the other has a real
// calibration) to still converge on the same target in lockstep.
// ============================================================================
namespace VolumeRamp {

// channels/okOut must each have 'count' entries (count is 1 or 2).
// okOut[i] reports whether channels[i]'s FINAL setStep() call (the one
// landing it exactly on targetStep) succeeded -- same meaning as calling
// setStep() directly, just smoothed in transit. A channel that's already
// at targetStep moves zero times and reports true trivially.
void rampTo(LDRVolume *channels[], uint8_t count, uint8_t targetStep, bool okOut[]);

} // namespace VolumeRamp