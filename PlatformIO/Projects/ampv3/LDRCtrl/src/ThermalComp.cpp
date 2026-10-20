#include "ThermalComp.hpp"
#include "ThermalLog.hpp"
#include "RailComp.hpp"
#include "DriverChannels.hpp"
#include "Board.hpp"

namespace {
constexpr uint32_t TICK_MS = 3000;
DriverChannels *gDrv = nullptr;
float gK[2] = {NAN, NAN}, gR2[2] = {NAN, NAN};
bool gUse[2] = {false, false};    // this side's fit is trusted
float gRef = NAN;                 // block temperature at the last calibration
float gEma = NAN;                 // block, 1-minute average
float gApplied[2] = {0.0f, 0.0f}; // offsets currently in the driver, counts
bool gFrozen = false, gEnabled = true;
uint32_t gLastMs = 0;

void refit() {
  bool usable[2];
  ThermalLog::blockSlopes(gK, gR2, usable);
  for (uint8_t c = 0; c < 2; c++) gUse[c] = ThermalComp::trusted(usable[c], gR2[c]);
  float t;
  gRef = ThermalLog::savedBlock(t) ? t : NAN;
}

void apply(bool force) {
  if (!gDrv) return;
  float want[2];
  for (uint8_t c = 0; c < 2; c++)
    want[c] = (gEnabled && !gFrozen && gUse[c]) ? ThermalComp::offsetCounts(gK[c], gEma, gRef) : 0.0f;
  if (!force && fabsf(want[0] - gApplied[0]) <= ThermalComp::DEADBAND_COUNTS &&
      fabsf(want[1] - gApplied[1]) <= ThermalComp::DEADBAND_COUNTS) return;
  gApplied[0] = want[0]; gApplied[1] = want[1];
  const float fpc = (float)DriverChannels::FINE_PER_COUNT;
  gDrv->setThermalOffsetFine((int32_t)lroundf(want[0] * fpc), (int32_t)lroundf(want[1] * fpc));
}

float seedBlock() {
  float sum = 0; uint8_t n = 0;
  for (uint8_t i = 0; i < 4; i++) { const float v = Board::blockTempC(); if (!isnan(v)) { sum += v; n++; } }
  return n ? sum / n : NAN;
}
} // namespace

namespace ThermalComp {

void begin(DriverChannels &driver) {
  gDrv = &driver;
  refit();
  gEma = seedBlock();
  apply(true);
  gLastMs = millis();
}

void tick() {
  if (gFrozen || millis() - gLastMs < TICK_MS) return;
  gLastMs = millis();
  gEma = RailComp::emaStep(gEma, Board::blockTempC());
  apply(false);
}

void suspend() { gFrozen = true; apply(true); }

void onCalibrated() {
  gFrozen = false;
  refit();
  gEma = seedBlock();             // the reference was just taken from a fresh reading
  apply(true);
}

void resumeIfSuspended() { if (gFrozen) onCalibrated(); }

float blockAverage() { return gEma; }
float blockAtCalibration() { return gRef; }
bool sideActive(uint8_t side) { return side < 2 && gEnabled && !gFrozen && gUse[side]; }
float sideOffsetCounts(uint8_t side) { return side < 2 ? gApplied[side] : 0.0f; }
float sideSlope(uint8_t side) { return side < 2 ? gK[side] : NAN; }

void setEnabled(bool on) { gEnabled = on; apply(true); }
bool enabled() { return gEnabled; }

void report(Stream &out) {
  out.print(F("  thermal compensation: "));
  if (!gEnabled) { out.println(F("OFF (COMP ON to enable)")); return; }
  if (!gUse[0] && !gUse[1]) {
    out.println(F("not active yet -- needs a trusted fit vs the block (LEARN: 3+ resyncs, 0.5 C spread, R2 >= 0.6)"));
    return;
  }
  out.print(F("block 1-min average ")); if (isnan(gEma)) out.print(F("--")); else out.print(gEma, 2);
  out.print(F(" C vs ")); if (isnan(gRef)) out.print(F("--")); else out.print(gRef, 2);
  out.println(F(" C at calibration"));
  for (uint8_t c = 0; c < 2; c++) {
    out.print(c ? F("    R: ") : F("    L: "));
    if (!gUse[c]) { out.println(F("fit not trusted yet -- no offset")); continue; }
    out.print(gK[c], 2); out.print(F(" counts/C (R2 ")); out.print(gR2[c], 2);
    out.print(F(") -> offset ")); if (gApplied[c] >= 0) out.print('+'); out.print(gApplied[c], 2);
    out.println(F(" counts"));
  }
}

} // namespace ThermalComp
