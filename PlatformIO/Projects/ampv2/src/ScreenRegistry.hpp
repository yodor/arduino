#pragma once
#include <Arduino.h>

// Names of available screens, used by the "Starting Screen" menu setting.
// Order and count here MUST match main.cpp's g_screens[] array exactly,
// since DisplaySettings::setStartingScreenIndex() stores a plain index
// into that array -- main.cpp enforces this with a static_assert against
// kScreenCount, so a mismatch fails to compile rather than silently
// pointing "Starting Screen" options at the wrong screen.
constexpr const char* kScreenNames[] = {
    "Spectrum Bars",
    "Waveform",
    "Analog VU",
    "Digital VU",
    "Octave RTA",
};
constexpr size_t kScreenCount = sizeof(kScreenNames) / sizeof(kScreenNames[0]);