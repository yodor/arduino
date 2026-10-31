#include "AutoPref.hpp"
#include <Arduino.h>
#include "Settings.hpp"

namespace {
AutoPref::Pref gPref = AutoPref::Pref::BALANCED;
}

namespace AutoPref {

void begin() {                                     // after Settings::begin(); settings.txt: auto_pref
  const char *v = Settings::get("auto_pref");
  Pref p;
  if (v && parse(v, p)) gPref = p;
}

Pref current() { return gPref; }

void set(Pref p) {
  gPref = p;
  Settings::set("auto_pref", name(p));
}

} // namespace AutoPref
