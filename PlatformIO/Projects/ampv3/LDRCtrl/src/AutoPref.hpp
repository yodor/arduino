#pragma once
// How AUTO trades depth against settling (SET AUTO SNAPPY|BALANCED|DEEP, stored). The deeper the
// Rtotal, the darker the series cell sits at the quiet steps -- where CdS is slowest and has the
// most memory. Each preference sets how deep the dark-end walk measures, AUTO's memory and slope
// limits, and a cap on AUTO's pick, so the result is consistent (the walk's doubling steps used
// to land anywhere between ~650k and ~1.4M, and AUTO followed wherever they landed).
#include <stdint.h>

namespace AutoPref {

enum class Pref : uint8_t { SNAPPY = 0, BALANCED = 1, DEEP = 2 };
struct Params { float walkTopOhms, memMax, slopeMax, capOhms; };

inline Params params(Pref p) {
  switch (p) {
    case Pref::SNAPPY: return Params{ 600000.0f, 1.06f, 0.25f,  600000.0f};
    case Pref::DEEP:   return Params{1600000.0f, 1.20f, 0.40f, 1600000.0f};
    default:           return Params{1000000.0f, 1.12f, 0.30f, 1000000.0f};
  }
}
inline const char *name(Pref p) { return p == Pref::SNAPPY ? "SNAPPY" : p == Pref::DEEP ? "DEEP" : "BALANCED"; }
inline bool parse(const char *s, Pref &out) {
  auto eq = [](const char *a, const char *b) { while (*a && *b) { char x = *a, y = *b; if (x >= 'a' && x <= 'z') x -= 32; if (x != y) return false; a++; b++; } return *a == 0 && *b == 0; };
  if (eq(s, "SNAPPY")) { out = Pref::SNAPPY; return true; }
  if (eq(s, "BALANCED")) { out = Pref::BALANCED; return true; }
  if (eq(s, "DEEP")) { out = Pref::DEEP; return true; }
  return false;
}

// ---- runtime (stored in "/auto_pref.txt"; default BALANCED) ----
void begin();
Pref current();
inline Params now() { return params(current()); }
void set(Pref p);

} // namespace AutoPref
