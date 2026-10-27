#include "AutoPref.hpp"
#include <Arduino.h>
#include <LittleFS.h>

namespace {
constexpr const char *PATH = "/auto_pref.txt";
AutoPref::Pref gPref = AutoPref::Pref::BALANCED;
}

namespace AutoPref {

void begin() {
  File f = LittleFS.open(PATH, "r");
  if (!f) return;
  char buf[16] = {0};
  const size_t n = f.read((uint8_t *)buf, sizeof(buf) - 1);
  f.close();
  buf[n] = 0;
  for (size_t i = 0; i < n; i++) if (buf[i] == '\r' || buf[i] == '\n' || buf[i] == ' ') { buf[i] = 0; break; }
  Pref p;
  if (parse(buf, p)) gPref = p;
}

Pref current() { return gPref; }

void set(Pref p) {
  gPref = p;
  File f = LittleFS.open(PATH, "w");
  if (f) { f.println(name(p)); f.close(); }
}

} // namespace AutoPref
