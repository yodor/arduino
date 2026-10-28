#pragma once
#include "Config.hpp"

// The serial debug output, switchable at run time (Diagnostics > Debug
// Output): the once-a-second [Audio]/[Spectrum] timing lines and the [Perf]
// frame-rate line. They start from Config.hpp's DEBUG_* defaults and aren't
// saved -- after a reboot they're back to those defaults.
struct DebugFlags {
    static inline volatile bool audio = DEBUG_AUDIO_DIAGNOSTICS; // read by core 1 too
    static inline volatile bool perf  = DEBUG_PERF;
};
