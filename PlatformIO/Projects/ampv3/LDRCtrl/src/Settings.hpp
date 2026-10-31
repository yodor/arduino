#pragma once
// The user settings, in one readable file: /settings.txt (key=value, see SettingsText.hpp).
//   source_ohms=300        SET SRC
//   load_ohms=10000        SET LOAD
//   auto_pref=BALANCED     SET AUTO
// begin() loads it once at start-up; the first time, it takes over the values from the files
// used before (/audio_model.txt, /auto_pref.txt) and removes them. set() saves at once.
#include "SettingsText.hpp"

namespace Settings {
void begin();                              // also counts this power-up (boot_count)
uint16_t bootNumber();
const char *get(const char *key);          // nullptr if not set
void set(const char *key, const char *value);
const SettingsText::Store &all();
// An impedance (or any value) as settings text: whole numbers without decimals ("300"),
// otherwise one decimal ("47.5").
void formatNumber(char *out, size_t cap, float v);
} // namespace Settings
