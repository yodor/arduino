#pragma once
#include <stdint.h>
#include <string.h>
#include "Config.hpp" // CAL_R_CHOICES

// ============================================================================
// CalArgs -- the arguments of the master's `CAL FULL`.
//
// Grammar (protocol revision 5):   CAL FULL            keep the Rtotal currently set
//                                  CAL FULL R=<ohms>   <ohms> must be one of CAL_R_CHOICES
// Anything else is refused -- an unknown token, a repeated R=, a non-numeric or
// out-of-menu value, and the old MODE=... parameter (the FIXEDSERIES mode it
// selected no longer exists). Pure functions on plain C strings, tested on the host.
// ============================================================================
namespace CalArgs {

inline bool allowedR(long ohms) {
  for (uint8_t i = 0; i < CAL_R_CHOICE_COUNT; i++) {
    if (CAL_R_CHOICES[i] == ohms) return true;
  }
  return false;
}

// `rest` is what follows "CAL FULL" (already upper-cased). Returns true if it is
// empty/blank (hasR=false) or exactly one valid R=<ohms> (hasR=true, ohms set).
inline bool parseFull(const char *rest, bool &hasR, long &ohms) {
  hasR = false;
  while (*rest == ' ') rest++;
  if (*rest == '\0') return true;
  if (strncmp(rest, "R=", 2) != 0) return false;
  rest += 2;
  if (*rest < '0' || *rest > '9') return false;
  long v = 0;
  while (*rest >= '0' && *rest <= '9') {
    v = v * 10 + (*rest - '0');
    if (v > 10000000L) return false;          // no overflow games
    rest++;
  }
  while (*rest == ' ') rest++;
  if (*rest != '\0') return false;            // trailing junk, a second token, ...
  if (!allowedR(v)) return false;
  hasR = true;
  ohms = v;
  return true;
}

} // namespace CalArgs