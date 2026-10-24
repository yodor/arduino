#pragma once
#include <Arduino.h>

// Names of the screens, in the order RIGHT/LEFT step through them. Order and
// count MUST match main.cpp's g_screens[] array (a static_assert there checks
// the count).
constexpr const char* kScreenNames[] = {
    "Clock",
    "Waveform",
    "Spectrum",
    "Digital VU",
    "Analog VU",
};
constexpr size_t kScreenCount = sizeof(kScreenNames) / sizeof(kScreenNames[0]);

// The unit always boots on the first screen, the Clock. The current screen
// isn't saved (no flash writes for screen changes).
constexpr uint8_t kDefaultScreenIndex = 0; // "Clock"

constexpr bool screenNameIs(const char* a, const char* b) {
    return (*a == *b) && (*a == '\0' || screenNameIs(a + 1, b + 1));
}
static_assert(screenNameIs(kScreenNames[kDefaultScreenIndex], "Clock"),
              "kDefaultScreenIndex must point at the Clock screen -- update it if the screen order changes");
