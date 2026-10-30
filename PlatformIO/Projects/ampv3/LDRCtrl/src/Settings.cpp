#include "Settings.hpp"
#include <Arduino.h>
#include <LittleFS.h>
#include <math.h>
#include "Console.hpp"

namespace Settings { void formatNumber(char *out, size_t cap, float v); }

namespace {
constexpr const char *PATH = "/settings.txt";
SettingsText::Store gStore;

void save() {
  static char buf[SettingsText::MAX_KEYS * (SettingsText::KEY_LEN + SettingsText::VAL_LEN + 2) + 64];
  const size_t n = SettingsText::format(gStore, buf, sizeof buf, "LDR attenuator settings (key=value; unknown keys are kept)");
  File f = LittleFS.open(PATH, "w");
  if (!f) return;
  f.write((const uint8_t *)buf, n);
  f.close();
}

// Read a whole small file into buf; false if missing.
bool readFile(const char *path, char *buf, size_t cap) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  const size_t n = f.read((uint8_t *)buf, cap - 1);
  f.close();
  buf[n] = '\0';
  return true;
}

// One-time take-over from the files used before settings.txt existed.
void migrate() {
  char buf[64];
  if (readFile("/audio_model.txt", buf, sizeof buf)) {      // "<source ohms> <load ohms>"
    char *end;
    const double s = strtod(buf, &end);
    if (end != buf) {
      const char *p = end;
      const double l = strtod(p, &end);
      char v[24];
      Settings::formatNumber(v, sizeof v, (float)s); gStore.set("source_ohms", v);
      if (end != p) { Settings::formatNumber(v, sizeof v, (float)l); gStore.set("load_ohms", v); }
    }
  }
  if (readFile("/auto_pref.txt", buf, sizeof buf)) {         // "BALANCED"
    for (char *p = buf; *p; p++) if (*p == '\r' || *p == '\n' || *p == ' ') { *p = '\0'; break; }
    if (*buf) gStore.set("auto_pref", buf);
  }
  save();
  LittleFS.remove("/audio_model.txt");
  LittleFS.remove("/auto_pref.txt");
}
} // namespace

namespace Settings {

uint16_t gBoot = 0;

void begin() {
  static char buf[2048];
  if (readFile(PATH, buf, sizeof buf)) SettingsText::parse(buf, gStore);
  else migrate();
  const char *b = gStore.get("boot_count");
  gBoot = (uint16_t)((b ? strtoul(b, nullptr, 10) : 0) + 1);
  char v[12]; snprintf(v, sizeof v, "%u", (unsigned)gBoot);
  set("boot_count", v);
}

uint16_t bootNumber() { return gBoot; }

const char *get(const char *key) { return gStore.get(key); }

void set(const char *key, const char *value) {
  gStore.set(key, value);
  save();
}

const SettingsText::Store &all() { return gStore; }

void formatNumber(char *out, size_t cap, float v) {
  if (fabsf(v - roundf(v)) < 0.05f) snprintf(out, cap, "%ld", (long)lroundf(v));
  else snprintf(out, cap, "%s", Console::num(v, 1).s);
}

} // namespace Settings
