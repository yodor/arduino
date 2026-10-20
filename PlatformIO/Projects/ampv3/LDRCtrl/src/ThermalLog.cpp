#include "ThermalLog.hpp"
#include "ThermalFit.hpp"
#include "Board.hpp"
#include "CellTemps.hpp"
#include <LittleFS.h>
#include <stdlib.h>

namespace {
constexpr const char *STATE_PATH = "/thermal_state.txt";
constexpr const char *LOG_PATH   = "/thermal_log.csv";
constexpr uint8_t MAX_ENTRIES = 48;
constexpr size_t ROTATE_BYTES = 6000;   // past this, keep the newest 30 lines
constexpr uint8_t KEEP_LINES = 30;

void printSigned(Print &p, float v, uint8_t dec) {
  if (isnan(v)) { p.print(F("--")); return; }
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

bool loadState(ThermalLog::Env &e) {
  File f = LittleFS.open(STATE_PATH, "r");
  if (!f) return false;
  char buf[96] = {0};
  const size_t n = f.read((uint8_t *)buf, sizeof(buf) - 1);
  f.close();
  buf[n] = 0;
  float v[4];
  if (parseFloats(buf, v, 4) != 4) return false;
  e.block = v[0]; e.cell[0] = v[1]; e.cell[1] = v[2]; e.rail = v[3];
  return true;
}

void saveState(const ThermalLog::Env &e) {
  File f = LittleFS.open(STATE_PATH, "w");
  if (!f) return;
  f.print(e.block, 3); f.print(' '); f.print(e.cell[0], 3); f.print(' ');
  f.print(e.cell[1], 3); f.print(' '); f.print(e.rail, 4); f.println();
  f.close();
}

// Read the log's entries (newest last). Each line: shiftL,shiftR,dBlock,dCellL,dCellR,dRail%,...
uint8_t loadEntries(ThermalFit::Entry *e, uint8_t maxN) {
  File f = LittleFS.open(LOG_PATH, "r");
  if (!f) return 0;
  uint8_t n = 0;
  char line[160]; size_t len = 0;
  auto take = [&]() {
    line[len] = 0;
    float v[6];
    if (len && parseFloats(line, v, 6) == 6) {
      if (n == maxN) { for (uint8_t i = 1; i < maxN; i++) e[i - 1] = e[i]; n--; } // keep the newest
      ThermalFit::Entry &x = e[n++];
      x.shift[0] = v[0]; x.shift[1] = v[1]; x.dBlock = v[2]; x.dCell[0] = v[3]; x.dCell[1] = v[4]; x.dRailPct = v[5];
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
  File f = LittleFS.open(LOG_PATH, "r");
  if (!f) return;
  const size_t size = f.size();
  if (size <= ROTATE_BYTES) { f.close(); return; }
  // count lines, then copy the last KEEP_LINES into a new file
  uint16_t lines = 0;
  while (f.available()) if (f.read() == '\n') lines++;
  f.seek(0);
  File g = LittleFS.open("/thermal_log.tmp", "w");
  uint16_t seen = 0;
  while (f.available()) {
    const int ch = f.read();
    if (ch < 0) break;
    if (seen + KEEP_LINES >= lines && g) g.write((uint8_t)ch);
    if (ch == '\n') seen++;
  }
  f.close();
  if (g) { g.close(); LittleFS.remove(LOG_PATH); LittleFS.rename("/thermal_log.tmp", LOG_PATH); }
}
} // namespace

namespace ThermalLog {

Env snapshot() {
  Env e;
  e.block = Board::blockTempC();
  e.rail = Board::railVolts();
  e.cell[0] = e.cell[1] = NAN;
  if (CellTemps::count() > 0) CellTemps::read(e.cell);
  return e;
}

void onCalibrated(bool resync, const float *shift, const Env &now, Stream &out) {
  Env prev;
  const bool havePrev = loadState(prev);
  if (resync && havePrev && shift) {
    const float dB = now.block - prev.block;
    const float dL = now.cell[0] - prev.cell[0], dR = now.cell[1] - prev.cell[1];
    const float dRail = (now.rail / prev.rail - 1.0f) * 100.0f;
    File f = LittleFS.open(LOG_PATH, "a");
    if (f) {
      f.print(shift[0], 3); f.print(','); f.print(shift[1], 3); f.print(',');
      f.print(dB, 3); f.print(','); f.print(dL, 3); f.print(','); f.print(dR, 3); f.print(',');
      f.print(dRail, 4); f.print(',');
      f.print(now.block, 3); f.print(','); f.print(now.cell[0], 3); f.print(','); f.print(now.cell[1], 3); f.print(',');
      f.print(now.rail, 4); f.print(','); f.print(Board::uptimeSeconds()); f.println();
      f.close();
      rotateIfLarge();
    }
    out.print(F("  thermal log: knee shift L ")); printSigned(out, shift[0], 2);
    out.print(F(" R ")); printSigned(out, shift[1], 2);
    out.print(F(" counts | since the last calibration: block ")); printSigned(out, dB, 2);
    out.print(F(" C, cells L ")); printSigned(out, dL, 2); out.print(F(" R ")); printSigned(out, dR, 2);
    out.print(F(" C, rail ")); printSigned(out, dRail, 2); out.println(F(" %"));
  } else if (resync) {
    out.println(F("  thermal log: first calibration with sensors -- environment saved; the NEXT resync is logged"));
  } else {
    out.println(F("  thermal log: environment at this calibration saved (resyncs are logged against it)"));
  }
  saveState(now);
}

void printSince(Stream &out) {
  Env prev;
  if (!loadState(prev)) { out.println(F("  since the last calibration: -- (no environment saved yet; the next CAL saves it)")); return; }
  const Env now = snapshot();
  out.print(F("  since the last calibration: block ")); printSigned(out, now.block - prev.block, 2);
  out.print(F(" C, cells L ")); printSigned(out, now.cell[0] - prev.cell[0], 2);
  out.print(F(" R ")); printSigned(out, now.cell[1] - prev.cell[1], 2);
  out.print(F(" C, rail ")); printSigned(out, (now.rail / prev.rail - 1.0f) * 100.0f, 2); out.println(F(" %"));
}

void printLog(Stream &out) {
  static ThermalFit::Entry e[MAX_ENTRIES];
  const uint8_t n = loadEntries(e, MAX_ENTRIES);
  out.print(F("--- thermal learning log: ")); out.print(n); out.println(F(" resync(s) ---"));
  if (n) out.println(F("   #  shift L   shift R  | d block  d cell L  d cell R  d rail%"));
  const uint8_t first = n > 20 ? (uint8_t)(n - 20) : 0;
  for (uint8_t i = first; i < n; i++) {
    out.print(F("  ")); if (i + 1 < 10) out.print(' '); out.print(i + 1); out.print(F("  "));
    printSigned(out, e[i].shift[0], 2); out.print(F("     ")); printSigned(out, e[i].shift[1], 2); out.print(F("   |  "));
    printSigned(out, e[i].dBlock, 2); out.print(F("    ")); printSigned(out, e[i].dCell[0], 2); out.print(F("     "));
    printSigned(out, e[i].dCell[1], 2); out.print(F("     ")); printSigned(out, e[i].dRailPct, 2); out.println();
  }
  static const char *what[3] = {"block", "cell ", "rail "};
  static const char *unit[3] = {"C", "C", "%"};
  static const float minSpread[3] = {0.5f, 0.5f, 0.2f};
  out.println(F("--- fit: knee shift = k x change (through zero), per channel ---"));
  for (uint8_t c = 0; c < 2; c++) {
    for (uint8_t x = 0; x < 3; x++) {
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
  out.println(F("  The variable with the higher R2 explains the drift; stage 2 compensates with it."));
}

void clear() { LittleFS.remove(LOG_PATH); }

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
