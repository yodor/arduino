#pragma once
#include <Arduino.h>

// Names of available screens, used by the "Starting Screen" menu setting.
// Order and count here MUST match main.cpp's g_screens[] array exactly,
// since DisplaySettings::setStartingScreenIndex() stores a plain index
// into that array -- main.cpp enforces this with a static_assert against
// kScreenCount, so a mismatch fails to compile rather than silently
// pointing "Starting Screen" options at the wrong screen.
constexpr const char* kScreenNames[] = {
    "Spectrum",
    "Waveform",
    "Analog VU",
    "Digital VU",
    "Clock",
};
constexpr size_t kScreenCount = sizeof(kScreenNames) / sizeof(kScreenNames[0]);

// The screen a fresh unit (no saved settings) starts on. After that the unit
// remembers whichever screen was last shown (DisplaySettings' starting-screen
// index, saved a few seconds after each screen change).
constexpr uint8_t kDefaultScreenIndex = 4; // "Clock"

constexpr bool screenNameIs(const char* a, const char* b) {
    return (*a == *b) && (*a == '\0' || screenNameIs(a + 1, b + 1));
}
static_assert(screenNameIs(kScreenNames[kDefaultScreenIndex], "Clock"),
              "kDefaultScreenIndex must point at the Clock screen -- update it if the screen order changes");
