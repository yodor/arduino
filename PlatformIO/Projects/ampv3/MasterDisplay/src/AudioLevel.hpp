#pragma once
#include <Arduino.h>
#include <cmath>

// Shared audio-level utilities: RMS of a raw sample block, and conversion
// to a 0..1 value using the same -50..0dB convention used throughout the
// display screens. Used by every screen that needs a level-to-display
// mapping -- AnalogVuScreen and DigitalVuMeterScreen call computeRms()
// directly (their level source is raw waveform RMS); SpectrumScreen only
// uses dbNormalize() (its level source is already an FFT-bin-derived
// magnitude/RMS from AudioEngine, not raw samples).
//
// Deliberately kept in its own lightweight header rather than folded into
// AudioEngine (which pulls in the RP2040 hardware headers and kissfft) --
// screens that only need the dB mapping shouldn't have to pull in the
// whole audio engine for it.
namespace AudioLevel {

inline float computeRms(const int16_t* samples, size_t count) {
    if (!samples || count == 0) return 0.0f;

    float sumSq = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        float s = samples[i] / 2048.0f; // normalize against the same +/-2048 full-scale used elsewhere
        sumSq += s * s;
    }
    return sqrtf(sumSq / static_cast<float>(count));
}

inline float dbNormalize(float rms) {
    if (rms <= 0.001f) return 0.0f;
    float db = 20.0f * log10f(rms);
    float norm = (db + 50.0f) / 50.0f;
    return constrain(norm, 0.0f, 1.0f);
}

} // namespace AudioLevel