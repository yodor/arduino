#pragma once
#include <stdint.h>
#include <string.h>

// ============================================================================
// CalArgs -- the arguments of the controller's single `CAL` command.
//
// Grammar:   CAL             the firmware decides (CellCheck.hpp)          (protocol 7)
//            CAL R=<ohms>    manual R, a positive integer: re-solve + resync
//            CAL R=AUTO      back to the automatic choice
//            CAL FULL        always a full characterization, AUTO's Rtotal (protocol 19)
//            CAL VERIFY      measure how well the table holds; the table is not changed (20)
// This checks SYNTAX only. Whether <ohms> is acceptable depends on the hardware: it must
// lie in RMIN..RMAX as measured by the last calibration (SerialLink checks that). Refused
// here: an unknown token, a repeated R=, a non-numeric or zero value, and the old MODE=.
// Pure functions on plain C strings, tested on the host.
// ============================================================================
namespace CalArgs {

constexpr long MAX_OHMS = 10000000L; // 10 MOhm: anything above is a typo, not a request

// `rest` is what follows "CAL" (already upper-cased): empty (the firmware decides),
// R=<positive integer> (manual Rtotal), or R=AUTO (back to the automatic choice).
// FULL (alone) forces a characterization. Anything else is refused (the old FAST / MODE=).
inline bool parseCal(const char *rest, bool &hasR, bool &rAuto, long &ohms, bool &full, bool &verify) {
  hasR = false; rAuto = false; ohms = 0; full = false; verify = false;
  while (*rest == ' ') rest++;
  if (*rest == '\0') return true;
  for (int w = 0; w < 2; w++) {                       // FULL / VERIFY: a single word
    const char *word = w ? "VERIFY" : "FULL";
    const size_t len = strlen(word);
    if (strncmp(rest, word, len) == 0) {
      const char *p = rest + len;
      while (*p == ' ') p++;
      if (*p != '\0') return false;
      (w ? verify : full) = true;
      return true;
    }
  }
  if (strncmp(rest, "R=", 2) != 0) return false;
  rest += 2;
  if (strncmp(rest, "AUTO", 4) == 0) {
    rest += 4;
    while (*rest == ' ') rest++;
    if (*rest != '\0') return false;
    rAuto = true;
    return true;
  }
  if (*rest < '0' || *rest > '9') return false;
  long v = 0;
  while (*rest >= '0' && *rest <= '9') {
    v = v * 10 + (*rest - '0');
    if (v > MAX_OHMS) return false;
    rest++;
  }
  while (*rest == ' ') rest++;
  if (*rest != '\0' || v <= 0) return false;
  hasR = true;
  ohms = v;
  return true;
}

} // namespace CalArgs
