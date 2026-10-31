#include "ThermalComp.hpp"
#include "VerifyHistory.hpp"
#include "ThermalLog.hpp"
#include "RailComp.hpp"
#include "DriverChannels.hpp"
#include "Board.hpp"
#include "DualCalibration.hpp"
#include "BoardSensors.hpp"

namespace {
constexpr uint32_t TICK_MS = 3000;
DriverChannels *gDrv = nullptr;
float gK[2] = {NAN, NAN}, gR2[2] = {NAN, NAN};
bool gUse[2] = {false, false};    // this side's fit is trusted
float gRef = NAN;                 // block temperature at the last calibration
float gEma = NAN;                 // block, 1-minute average
float gApplied[2] = {0.0f, 0.0f}; // offsets currently in the driver, counts
bool gFrozen = false, gEnabled = true;
float gSpanC = 0.0f;              // the largest block change the fit learned from
// The block's trend: one sample of the 1-minute average per minute, the last 11 (10 minutes).
constexpr uint8_t HIST = 11;
float gHist[HIST];
uint8_t gHistN = 0;
uint32_t gHistMs = 0;
uint32_t gLastMs = 0;

void refit() {
  bool usable[2];
  ThermalLog::blockSlopes(gK, gR2, usable);
  for (uint8_t c = 0; c < 2; c++) gUse[c] = ThermalComp::trusted(usable[c], gR2[c], gK[c]);
  gSpanC = ThermalLog::learnedSpanC();
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
  const float t = Board::blockTempC();             // NAN unless the NTC is OK
  if (isnan(t)) {
    // A faulty NTC: no offset at once (never keep applying a stale average), fresh start later.
    const bool had = !isnan(gEma);
    gEma = NAN;
    if (had) apply(true);
    return;
  }
  gEma = isnan(gEma) ? t : RailComp::emaStep(gEma, t);
  apply(false);
  if (gHistN == 0 || millis() - gHistMs >= 60000UL) {
    gHistMs = millis();
    if (gHistN == HIST) { for (uint8_t i = 1; i < HIST; i++) gHist[i - 1] = gHist[i]; gHistN--; }
    gHist[gHistN++] = gEma;
  }
}

void suspend() { gFrozen = true; apply(true); }

void onCalibrated() {
  gFrozen = false;
  refit();
  gEma = seedBlock();             // the reference was just taken from a fresh reading
  apply(true);
}

void resumeIfSuspended() { if (gFrozen) onCalibrated(); }
void refresh() { refit(); apply(true); }

float blockAverage() { return gEma; }
float blockAtCalibration() { return gRef; }
bool sideActive(uint8_t side) { return side < 2 && gEnabled && !gFrozen && gUse[side] && !isnan(gEma); }
bool ntcOk() { return Board::ntcState() == BoardSensors::NtcState::OK; }

float blockRatePer10Min() { return gHistN == HIST ? gHist[HIST - 1] - gHist[0] : NAN; }

Hint hint() {
  const float dT = (isnan(gEma) || isnan(gRef)) ? NAN : gEma - gRef;
  const bool compOn = sideActive(0) && sideActive(1);
  const float rate = blockRatePer10Min();
  const bool warming = warmingHold(compOn, Board::msSinceAmpOn(), rate);
  return resyncHint(compOn, dT, gSpanC, DualCalibration::lastSeekFailed(), Board::ampCommandedMuted(), warming,
                    VerifyHistory::pendingResync());
}

void printHint(Print &out) {
  const Hint h = hint();
  if (h == Hint::NONE) return;
  if (h == Hint::WARMING) {
    out.print(F("  resync: not yet -- the temperature is still settling after power-up (block "));
    const float r = blockRatePer10Min();
    if (isnan(r)) out.print(F("trend not known yet"));
    else { if (r >= 0) out.print('+'); out.print(r, 2); out.print(F(" C per 10 min")); }
    out.println(F("); it is recommended once steady"));
    return;
  }
  out.print(F("  resync recommended: "));
  switch (h) {
    case Hint::SPAN:   out.println(F("the block is outside the range the compensation has learned -- a CAL makes it exact and teaches it this range")); break;
    case Hint::LIMIT:  out.println(F("the block is 30 C or more from the calibration temperature, beyond the compensation's range")); break;
    case Hint::NOCOMP: out.println(F("the block has moved since the last calibration and the compensation is not active")); break;
    case Hint::SEEK:   out.println(F("the last calibration's quiet-point seek did not converge")); break;
    case Hint::WARMING: break;
    case Hint::VERIFY: out.println(F("the last VERIFY found the table off (residual or middle steps) -- a resync fixes it")); break;
    default: break;
  }
}
float sideOffsetCounts(uint8_t side) { return side < 2 ? gApplied[side] : 0.0f; }
float sideSlope(uint8_t side) { return side < 2 ? gK[side] : NAN; }

void setEnabled(bool on) { gEnabled = on; apply(true); }
bool enabled() { return gEnabled; }

void report(Stream &out) {
  out.print(F("  thermal compensation: "));
  if (!gEnabled) { out.println(F("OFF (COMP ON to enable)")); return; }
  const BoardSensors::NtcState ns = Board::ntcState();
  if (ns != BoardSensors::NtcState::OK) {
    out.print(F("OFF -- block NTC "));
    out.print(BoardSensors::ntcStateName(ns));
    out.println(ns == BoardSensors::NtcState::OPEN ? F(" (missing or broken wire): no offsets until it reads again")
              : ns == BoardSensors::NtcState::SHORT ? F(" (shorted): no offsets until it reads again")
              : F(" (implausible reading): no offsets until it reads again"));
    return;
  }
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
    out.print(F(" counts"));
    out.println();
  }
  const float dT = (isnan(gEma) || isnan(gRef)) ? 0.0f : gEma - gRef;
  if (fabsf(dT) >= ThermalComp::MAX_SPAN_C)
    out.println(F("    AT THE RANGE LIMIT (30 C from the calibration temperature)"));
  else if (gSpanC > 0.0f && fabsf(dT) > gSpanC) {
    out.print(F("    extrapolating: the block is ")); out.print(fabsf(dT), 1);
    out.print(F(" C from calibration, the fit learned up to ")); out.print(gSpanC, 1);
    out.println(F(" C"));
  }
  printHint(out);
}

} // namespace ThermalComp
