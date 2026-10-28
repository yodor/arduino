#include "ThermalLog.hpp"
#include "ThermalFit.hpp"
#include "Board.hpp"
#include <LittleFS.h>
#include <stdlib.h>

namespace {
constexpr const char *STATE_PATH = "/thermal_state.txt";
constexpr const char *LOG_PATH   = "/thermal_log.csv";
constexpr uint8_t MAX_ENTRIES = 48;
// Trimmed to MAX_ENTRIES lines whenever it grows past that, so everything kept is also what
// the fit reads; WHICH lines stay is chosen by temperature (ThermalFit::selectKeep).
constexpr uint8_t KEEP_NEWEST = 12;     // always the newest resyncs
constexpr uint8_t KEEP_PER_BAND = 2;    // and the newest two in every 1 C band of block temperature

// The RP2040's ROM float routines do NOT propagate NaN: NaN - NaN comes out as infinity.
// So "unknown" must be checked BEFORE any arithmetic, never after.
float diffOrNan(float a, float b) { return (isnan(a) || isnan(b)) ? NAN : a - b; }
float pctOrNan(float now, float ref) { return (isnan(now) || isnan(ref) || !(ref > 0.0f)) ? NAN : (now / ref - 1.0f) * 100.0f; }

void printSigned(Print &p, float v, uint8_t dec) {
  if (!isfinite(v)) { p.print(F("--")); return; }
  if (v >= 0) p.print('+');
  p.print(v, dec);
}

// Up to n floats from a comma/space separated line (strtod understands "nan").
uint8_t parseFloats(const char *s, float *out, uint8_t n) {
  uint8_t k = 0;
  while (*s && k < n) {
    while (*s == ',' || *s == ' ') s++;
    if (!*s) break;
    char *end;
    const double v = strtod(s, &end);
    if (end == s) break;
    out[k++] = (float)v;
    s = end;
  }
  return k;
}

// The state file: "block rail" (since 2026-10-25c); older files hold "block cellL cellR rail".
bool loadState(ThermalLog::Env &e) {
  File f = LittleFS.open(STATE_PATH, "r");
  if (!f) return false;
  char buf[96] = {0};
  const size_t n = f.read((uint8_t *)buf, sizeof(buf) - 1);
  f.close();
  buf[n] = 0;
  float v[4];
  const uint8_t k = parseFloats(buf, v, 4);
  if (k == 4) { e.block = v[0]; e.rail = v[3]; return true; }   // old format (with the cells)
  if (k == 2) { e.block = v[0]; e.rail = v[1]; return true; }
  return false;
}

void saveState(const ThermalLog::Env &e) {
  File f = LittleFS.open(STATE_PATH, "w");
  if (!f) return;
  f.print(e.block, 3); f.print(' '); f.print(e.rail, 4); f.println();
  f.close();
}

// Read the log's entries (newest last). Two line formats, told apart by their field count:
//   7 fields (since 2026-10-25c):  shiftL,shiftR,dBlock,dRail%,block,rail,uptime
//  11 fields (before, with cells): shiftL,shiftR,dBlock,dCellL,dCellR,dRail%,block,cellL,cellR,rail,uptime
uint8_t loadEntries(ThermalFit::Entry *e, uint8_t maxN) {
  File f = LittleFS.open(LOG_PATH, "r");
  if (!f) return 0;
  uint8_t n = 0;
  char line[160]; size_t len = 0;
  auto take = [&]() {
    line[len] = 0;
    ThermalFit::Entry x;
    if (len && ThermalFit::parseLogLine(line, x)) {
      if (n == maxN) { for (uint8_t i = 1; i < maxN; i++) e[i - 1] = e[i]; n--; } // keep the newest
      e[n++] = x;
    }
    len = 0;
  };
  while (f.available()) {
    const int ch = f.read();
    if (ch < 0) break;
    if (ch == '\n') take();
    else if (len < sizeof(line) - 1) line[len++] = (char)ch;
  }
  take();
  f.close();
  return n;
}

void rotateIfLarge() {
  constexpr uint16_t MAXL = 160;            // lines considered at once (an older, larger log: its newest)
  static bool usable[MAXL];
  static float blockC[MAXL];
  static bool keep[MAXL];
  File f = LittleFS.open(LOG_PATH, "r");
  if (!f) return;
  // pass 1: what each line is
  uint16_t total = 0, n = 0;
  bool prevFailed = false;
  char line[160]; size_t len = 0;
  auto classify = [&]() {
    line[len] = 0;
    if (len) {
      ThermalFit::Entry e;
      const bool isEntry = ThermalFit::parseLogLine(line, e);
      const bool failed = isEntry && ThermalFit::seekFailed(e);
      const bool ok = isEntry && !failed && !prevFailed;
      if (isEntry) prevFailed = failed;
      if (n == MAXL) { for (uint16_t i = 1; i < MAXL; i++) { usable[i - 1] = usable[i]; blockC[i - 1] = blockC[i]; } n--; }
      usable[n] = ok; blockC[n] = ThermalFit::parseLogBlock(line); n++;
      total++;
    }
    len = 0;
  };
  while (f.available()) {
    const int ch = f.read();
    if (ch < 0) break;
    if (ch == '\n') classify();
    else if (len < sizeof(line) - 1) line[len++] = (char)ch;
  }
  classify();
  if (total <= MAX_ENTRIES) { f.close(); return; }
  ThermalFit::selectKeep(usable, blockC, n, keep, KEEP_NEWEST, KEEP_PER_BAND, MAX_ENTRIES);
  // pass 2: copy the kept lines, in order
  f.seek(0);
  File g = LittleFS.open("/thermal_log.tmp", "w");
  if (!g) { f.close(); return; }
  const uint16_t skip = total - n;          // lines older than the window: dropped
  uint16_t idx = 0; len = 0;
  auto copy = [&]() {
    line[len] = 0;
    if (len) {
      if (idx >= skip && keep[idx - skip]) { g.print(line); g.print('\n'); }
      idx++;
    }
    len = 0;
  };
  while (f.available()) {
    const int ch = f.read();
    if (ch < 0) break;
    if (ch == '\n') copy();
    else if (len < sizeof(line) - 1) line[len++] = (char)ch;
  }
  copy();
  f.close();
  g.close();
  LittleFS.remove(LOG_PATH);
  LittleFS.rename("/thermal_log.tmp", LOG_PATH);
}
} // namespace

namespace ThermalLog {

Env snapshot() {
  Env e;
  e.block = Board::blockTempC();
  e.rail = Board::railVolts();
  return e;
}

void onCalibrated(bool resync, const float *shift, const Env &now, Stream &out) {
  Env prev;
  const bool havePrev = loadState(prev);
  if (resync && havePrev && shift) {
    const float dB = diffOrNan(now.block, prev.block);
    const float dRail = pctOrNan(now.rail, prev.rail);
    File f = LittleFS.open(LOG_PATH, "a");
    if (f) {                       // 7 fields: shiftL,shiftR,dBlock,dRail%,block,rail,uptime
      f.print(shift[0], 3); f.print(','); f.print(shift[1], 3); f.print(',');
      f.print(dB, 3); f.print(','); f.print(dRail, 4); f.print(',');
      f.print(now.block, 3); f.print(','); f.print(now.rail, 4); f.print(','); f.print(Board::uptimeSeconds()); f.println();
      f.close();
      rotateIfLarge();
    }
    out.print(F("  thermal log: knee shift L ")); printSigned(out, shift[0], 2);
    out.print(F(" R ")); printSigned(out, shift[1], 2);
    out.print(F(" counts | since the last calibration: block ")); printSigned(out, dB, 2);
    out.print(F(" C, rail ")); printSigned(out, dRail, 2); out.println(F(" %"));
  } else if (resync) {
    out.println(F("  thermal log: first calibration with an environment -- saved; the NEXT resync is logged"));
  } else {
    out.println(F("  thermal log: environment at this calibration saved (resyncs are logged against it)"));
  }
  saveState(now);
}

void printSince(Stream &out) {
  Env prev;
  if (!loadState(prev)) { out.println(F("  since the last calibration: -- (no environment saved yet; the next CAL saves it)")); return; }
  const Env now = snapshot();
  out.print(F("  since the last calibration: block ")); printSigned(out, diffOrNan(now.block, prev.block), 2);
  out.print(F(" C, rail ")); printSigned(out, pctOrNan(now.rail, prev.rail), 2); out.println(F(" %"));
}

void printLog(Stream &out) {
  static ThermalFit::Entry e[MAX_ENTRIES];
  const uint8_t n = loadEntries(e, MAX_ENTRIES);
  out.print(F("--- thermal learning log: ")); out.print(n); out.println(F(" resync(s) ---"));
  if (n) out.println(F("   #  shift L   shift R  | d block  d rail%"));
  const uint8_t first = n > 20 ? (uint8_t)(n - 20) : 0;
  for (uint8_t i = first; i < n; i++) {
    out.print(F("  ")); if (i + 1 < 10) out.print(' '); out.print(i + 1); out.print(F("  "));
    printSigned(out, e[i].shift[0], 2); out.print(F("     ")); printSigned(out, e[i].shift[1], 2); out.print(F("   |  "));
    printSigned(out, e[i].dBlock, 2); out.print(F("    ")); printSigned(out, e[i].dRailPct, 2);
    if (ThermalFit::seekFailed(e[i])) out.print(F("   (failed seek: not used)"));
    else if (i > 0 && ThermalFit::seekFailed(e[i - 1])) out.print(F("   (catch-up after a failure: not used)"));
    out.println();
  }
  static const char *what[2] = {"block", "rail "};
  static const char *unit[2] = {"C", "%"};
  static const float minSpread[2] = {0.5f, 0.2f};
  out.println(F("--- fit: knee shift = k x change (through zero), per channel ---"));
  for (uint8_t c = 0; c < 2; c++) {
    for (uint8_t x = 0; x < 2; x++) {
      const ThermalFit::Slope s = ThermalFit::fit(e, n, c, x);
      out.print(c ? F("  R vs ") : F("  L vs ")); out.print(what[x]); out.print(F(": "));
      if (!ThermalFit::usable(s, minSpread[x])) {
        out.print(F("not yet (")); out.print(s.n); out.print(F(" resyncs, spread "));
        out.print(s.spread, 2); out.print(' '); out.print(unit[x]);
        out.print(F("; needs >=3 and >=")); out.print(minSpread[x], 1); out.print(' '); out.print(unit[x]); out.println(')');
        continue;
      }
      printSigned(out, s.k, 2); out.print(F(" counts/")); out.print(unit[x]);
      out.print(F("  R2 ")); out.print(s.r2, 2);
      out.print(F("  (")); out.print(s.n); out.print(F(" resyncs, spread ")); out.print(s.spread, 2); out.print(' ');
      out.print(unit[x]); out.println(')');
    }
  }
  out.println(F("  The compensation uses the block fit once it is trusted (3+ resyncs, 0.5 C spread, R2 >= 0.6)."));
}

void clear() { LittleFS.remove(LOG_PATH); }

void fitCounts(uint8_t &n, float &spread) {
  static ThermalFit::Entry e[MAX_ENTRIES];
  n = loadEntries(e, MAX_ENTRIES);
  const ThermalFit::Slope s = ThermalFit::fit(e, n, 0, 0);
  spread = s.spread;
}

void forgetEnvironment() { LittleFS.remove(STATE_PATH); }

float learnedSpanC() {
  static ThermalFit::Entry e[MAX_ENTRIES];
  const uint8_t n = loadEntries(e, MAX_ENTRIES);
  return ThermalFit::fit(e, n, 0, 0).maxAbs;
}

bool savedBlock(float &t) {
  Env e;
  if (!loadState(e) || isnan(e.block)) return false;
  t = e.block;
  return true;
}

void blockSlopes(float k[2], float r2[2], bool usable[2]) {
  static ThermalFit::Entry e[MAX_ENTRIES];
  const uint8_t n = loadEntries(e, MAX_ENTRIES);
  for (uint8_t c = 0; c < 2; c++) {
    const ThermalFit::Slope s = ThermalFit::fit(e, n, c, 0);
    k[c] = s.k; r2[c] = s.r2;
    usable[c] = ThermalFit::usable(s, 0.5f);
  }
}

} // namespace ThermalLog
