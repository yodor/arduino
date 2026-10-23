#include "RailComp.hpp"
#include "DriverChannels.hpp"
#include "Board.hpp"
#include <LittleFS.h>
#include <stdlib.h>

namespace {
constexpr const char *REF_PATH = "/rail_ref.txt";
constexpr uint32_t TICK_MS = 3000;
DriverChannels *gDrv = nullptr;
float gRef = NAN;        // rail the stored calibration's duties belong to
float gPending = NAN;    // rail at the start of a full characterization in progress
bool gFrozen = false;    // a full characterization is running: no correction
uint32_t gLastMs = 0;
float gEma = NAN;        // 1-minute running average of the rail: the correction follows THIS,
                         // not single readings (they scatter ~0.05 %)

float seedAverage() {    // 8 quick readings averaged (~25 ms)
  float sum = 0; uint8_t n = 0;
  for (uint8_t i = 0; i < 8; i++) { const float v = Board::railVolts(); if (!isnan(v)) { sum += v; n++; } }
  return n ? sum / n : NAN;
}

void saveRef() {
  File f = LittleFS.open(REF_PATH, "w");
  if (!f) return;
  f.print(gRef, 5); f.println();
  f.close();
}

void loadRef() {
  File f = LittleFS.open(REF_PATH, "r");
  if (!f) return;
  char buf[24] = {0};
  const size_t n = f.read((uint8_t *)buf, sizeof(buf) - 1);
  f.close();
  buf[n] = 0;
  const float v = (float)strtod(buf, nullptr);
  if (v > 2.6f && v < 4.0f) gRef = v;
}

void apply(float nowVolts) {
  if (!gDrv || gFrozen) return;
  const float s = isnan(gRef) ? 1.0f : RailComp::scaleFor(gRef, nowVolts);
  if (RailComp::changed(gDrv->railScale(), s)) gDrv->setRailScale(s);
}
} // namespace

namespace RailComp {

void begin(DriverChannels &driver) {
  gDrv = &driver;
  loadRef();
  gEma = seedAverage();
  apply(gEma);
  gLastMs = millis();
}

void tick() {
  if (gFrozen || millis() - gLastMs < TICK_MS) return;
  gLastMs = millis();
  gEma = RailComp::emaStep(gEma, Board::railVolts());
  apply(gEma);
}

void beginFullCharacterization() {
  gPending = isnan(gEma) ? seedAverage() : gEma;
  gFrozen = true;
  if (gDrv) gDrv->setRailScale(1.0f);
}

void onCalibrated(bool full) {
  if (isnan(gEma)) gEma = seedAverage();
  const float now = gEma;
  if (full && !isnan(gPending)) { gRef = gPending; saveRef(); }
  else if (!full && isnan(gRef) && !isnan(now)) { gRef = now; saveRef(); }   // first reference
  gPending = NAN;
  gFrozen = false;
  apply(now);
}

void endFullCharacterization() {
  if (!gFrozen) return;                   // onCalibrated() already ran
  gFrozen = false;                        // aborted: keep the old reference, correct again
  gPending = NAN;
  apply(gEma);
}

float referenceVolts() { return gRef; }
float averageVolts() { return gEma; }

void forgetReference() {
  gRef = NAN;
  LittleFS.remove(REF_PATH);
  if (gDrv) gDrv->setRailScale(1.0f);
}

void report(Stream &out) {
  const float latest = Board::railVolts();
  out.print(F("  rail compensation: "));
  if (isnan(gRef)) { out.println(F("not active yet -- the next CAL stores the reference rail")); return; }
  out.print(F("reference ")); out.print(gRef, 4);
  out.print(F(" V, 1-min average ")); if (isnan(gEma)) out.print(F("--")); else out.print(gEma, 4);
  out.print(F(" V (latest reading ")); if (isnan(latest)) out.print(F("--")); else out.print(latest, 4);
  out.print(F(") -> drive x")); out.print(gDrv ? gDrv->railScale() : 1.0f, 5);
  if (gDrv) {
    const float pct = (gDrv->railScale() - 1.0f) * 100.0f;
    out.print(F(" (")); if (pct >= 0) out.print('+'); out.print(pct, 3); out.print(F(" %)"));
  }
  out.println();
}

} // namespace RailComp
