#include "ConsoleCommands.hpp"
#include "Commands.hpp"
#include "Results.hpp"
#include "Settings.hpp"
#include "Backup.hpp"
#include "CalProgress.hpp"
#include "NullStream.hpp"
#include "VerifyHistory.hpp"
#include "VerifyAnalysis.hpp"
#include <LittleFS.h>
#include "Console.hpp"
#include "AudioModel.hpp"
#include "Pins.hpp"
#include "ThermalLog.hpp"
#include "RailComp.hpp"
#include "ThermalComp.hpp"
#include "BoardSensors.hpp"
#include "Config.hpp"
#include "VersionInfo.hpp"
#include "AutoPref.hpp"
#include "Balance.hpp"
#include "DualCalibration.hpp"
#include "CalArgs.hpp"
#include "BusyHook.hpp"
#include "Capabilities.hpp"
#include "DividerMath.hpp"
#include "VolumeRamp.hpp"

namespace {
// "VOL=8 (-40.04 dB)" -- the same number the strict controller protocol uses
// (0-based, 0 = quietest), plus the attenuation that step is solved for.
void describeVolume(Stream &out, LDRVolume &v) {
  if (!v.hasLut() || v.currentStep() >= v.numSteps()) {
    out.print(F("VOL=? (no calibration)"));
    return;
  }
  out.print(F("VOL="));
  out.print(v.currentStep());
  out.print(F(" ("));
  out.print(v.lut().step(v.currentStep()).targetDb, 2);
  out.print(F(" dB)"));
  if (v.isMuted()) out.print(F(" [muted]"));
}

} // namespace


namespace {
// One creep-log line: time, then per side the live Rs/Rsh and how far each is from the LUT target.
void creepLine(Stream &out, unsigned long tSec, LDRVolume *const *v, const uint8_t *step, float *serRatio) {
  out.print(F("  t="));
  if (tSec < 100) out.print(' ');
  if (tSec < 10) out.print(' ');
  out.print(tSec);
  out.print('s');
  for (uint8_t c = 0; c < 2; c++) {
    float vref, rs, rsh;
    const bool ok = v[c]->adcRead(vref, rs, rsh);
    const VolumeLut::Entry &e = v[c]->lut().step(step[c]);
    out.print(c ? F(" | R ") : F(" | L "));
    serRatio[c] = NAN;
    if (!ok || rs <= 0.0f || rsh <= 0.0f) { out.print(F("unreadable                    ")); continue; }
    serRatio[c] = rs / e.targetRs;
    out.print(F("Rs "));
    out.print(rs, 0);
    out.print(F(" (x"));
    out.print(rs / e.targetRs, 3);
    out.print(F(") Rsh "));
    out.print(rsh, 0);
    out.print(F(" (x"));
    out.print(rsh / e.targetRsh, 3);
    out.print(')');
  }
  out.println();
}
} // namespace


void ConsoleCommands::verifyCreep(uint16_t quietSeconds) {
  LDRVolume *v[2] = {&left_, &right_};
  for (uint8_t c = 0; c < 2; c++) {
    if (!v[c]->hasLut() || v[c]->lowestValidStep() < 0) { stream_.println(F("  skipped: needs a calibration on both sides")); return; }
  }
  stream_.println(F("--- CREEP: settling after volume jumps ---"));
  stream_.println(F("Ratios are live resistance / LUT target for that step (x1.000 = exactly on target)."));
  uint8_t loud[2], quiet[2], mid[2];
  for (uint8_t c = 0; c < 2; c++) {
    loud[c] = (uint8_t)v[c]->highestValidStep();
    quiet[c] = (uint8_t)v[c]->lowestValidStep();
    mid[c] = (uint8_t)((loud[c] + quiet[c]) / 2);
  }
  // Phase A: sit loud for 10 s (series cell bright), then jump to the quietest step and log.
  for (uint8_t c = 0; c < 2; c++) v[c]->driveLutStep(loud[c]);
  BusyHook::wait(10000);
  stream_.print(F("--- PHASE A: loudest step -> quietest step (series cell DARKENS), logging "));
  stream_.print(quietSeconds);
  stream_.println(F(" s ---"));
  unsigned long t0 = millis();
  for (uint8_t c = 0; c < 2; c++) v[c]->driveLutStep(quiet[c]);
  Results::Creep &R = Results::creep;                   // REPORT
  R.nA = 0; R.nB = 0;
  for (uint16_t k = 0; k <= quietSeconds; k++) {
    while (millis() - t0 < (unsigned long)k * 1000UL) BusyHook::wait(10);
    float r[2];
    creepLine(stream_, k, v, quiet, r);
    if (k < Results::CREEP_A) { R.a[0][k] = r[0]; R.a[1][k] = r[1]; R.nA = (uint16_t)(k + 1); }
  }
  // Phase B: quietest -> middle step (series cell BRIGHTENS), 30 s.
  stream_.println(F("--- PHASE B: quietest step -> middle step (series cell BRIGHTENS), logging 30 s ---"));
  t0 = millis();
  for (uint8_t c = 0; c < 2; c++) v[c]->driveLutStep(mid[c]);
  for (uint16_t k = 0; k <= 30; k++) {
    while (millis() - t0 < (unsigned long)k * 1000UL) BusyHook::wait(10);
    float r[2];
    creepLine(stream_, k, v, mid, r);
    if (k < Results::CREEP_B) { R.b[0][k] = r[0]; R.b[1][k] = r[1]; R.nB = (uint16_t)(k + 1); }
  }
  R.ms = millis() | 1;
  Board::reportDieTemp(stream_, F("Die temp"));
}

void ConsoleCommands::verifyWalk() {
  // Part of VERIFY: the calibration's own settled dark-end walk (DualCalibration::walkDarkEnd), run on
  // both series cells WITHOUT changing the calibration, then compared with the profile the
  // last full characterization stored -- do the cells still behave the way they were calibrated?
  LDRVolume *v[2] = {&left_, &right_};
  for (uint8_t c = 0; c < 2; c++) {
    // adsLastSeen(), NOT adsPresent(): the probe de-inits the I2C bus, and inside VERIFY's
    // relay session that left every later reading (walk, creep) unreadable.
    if (v[c]->seriesCurve().empty() || v[c]->adsLastSeen() == 0) {
      stream_.println(F("  skipped: needs curves (a calibration) and an ADS1115 on both sides"));
      return;
    }
  }
  stream_.println(F("--- DARK-END WALK: run again WITHOUT saving, compared with the stored profile ---"));
  DarkProfile now[2];
  const char lab[3] = {'L', 'R', 0};
  DualCalibration::walkDarkEnd(v, 2, lab, now, stream_);
  stream_.println(F("--- compared with the stored calibration (at the duties shifted by what the resyncs tracked) ---"));
  // The stored walk is from the full characterization; every resync since moved the step
  // table by the knee shift it measured. That accumulated shift = where the quiet step's
  // series duty sits now minus where the stored walk puts that resistance. Comparing the
  // fresh walk with the stored one at duty d - shift asks: does the table still fit?
  float worstDev = 1.0f;
  for (uint8_t c = 0; c < 2; c++) {
    const DarkProfile &st = v[c]->darkProfile();
    stream_.print(F("  ")); stream_.print(lab[c]);
    stream_.print(F(": settles up to ")); stream_.print(now[c].deepestOkOhms() / 1000.0f, 1);
    stream_.print(F("k now, ")); stream_.print(st.deepestOkOhms() / 1000.0f, 1);
    stream_.print(F("k at calibration"));
    const float FPCf = (float)DriverChannels::FINE_PER_COUNT;
    const uint8_t s0 = v[c]->lowestValidStep();
    float shift = NAN;
    if (st.okCount() >= 2 && s0 < v[c]->numSteps()) {
      const VolumeLut::Entry &q = v[c]->lut().step(s0);
      const float dStored = DarkProfileMath::dutyAt(st, q.targetRs);
      if (!isnan(dStored)) shift = q.seriesFine / FPCf - dStored;
    }
    Results::walk.fromVerify = true; Results::walk.shift[c] = shift;   // REPORT
    const float off = ThermalComp::sideActive(c) ? ThermalComp::sideOffsetCounts(c) : 0.0f;
    const VerifyAnalysis::Shift mv = VerifyAnalysis::walkShift(st, 0.0f, now[c], off);
    Results::walk.compOff[c] = off; Results::walk.moved[c] = mv.mean;
    Results::walk.movedSpread[c] = mv.n ? mv.max - mv.min : NAN;
    if (!isnan(shift)) {
      stream_.print(F(" | tracked shift ")); if (shift >= 0) stream_.print('+'); stream_.print(shift, 1); stream_.print(F(" counts"));
      for (uint8_t i = 0; i < now[c].okCount(); i++) {
        const float expect = DarkProfileMath::ohmsAt(st, (float)now[c].p[i].duty - shift);
        if (isnan(expect) || !(expect > 0.0f)) continue;
        const float ratio = now[c].p[i].ohms / expect;
        stream_.print(F(" | duty ")); stream_.print(now[c].p[i].duty);
        stream_.print(F(": x")); stream_.print(ratio, 2);
        const float dev = ratio >= 1.0f ? ratio : 1.0f / ratio;
        if (dev > worstDev) worstDev = dev;
        if (isnan(Results::walk.ratioMin[c]) || ratio < Results::walk.ratioMin[c]) Results::walk.ratioMin[c] = ratio;
        if (isnan(Results::walk.ratioMax[c]) || ratio > Results::walk.ratioMax[c]) Results::walk.ratioMax[c] = ratio;
      }
    }
    stream_.println();
  }
  // The same in counts of effective drive (commanded duty + compensation offset): how far the
  // cells moved since the stored walk, how much the table's shift plus today's compensation
  // supply, and the residual (positive = driven brighter than the cells need = louder).
  for (uint8_t c = 0; c < 2; c++) {
    const Results::Walk &w = Results::walk;
    stream_.print(F("  ")); stream_.print(lab[c]); stream_.print(F(": cells moved "));
    if (isnan(w.moved[c])) { stream_.println(F("-- (no overlapping deep points)")); continue; }
    if (w.moved[c] >= 0) stream_.print('+'); stream_.print(w.moved[c], 1);
    stream_.print(F(" counts (spread ")); stream_.print(w.movedSpread[c], 1);
    stream_.print(F(") | table ")); if (w.shift[c] >= 0) stream_.print('+'); stream_.print(w.shift[c], 1);
    stream_.print(F(" + compensation ")); if (w.compOff[c] >= 0) stream_.print('+'); stream_.print(w.compOff[c], 1);
    const float res = w.shift[c] + w.compOff[c] - w.moved[c];
    stream_.print(F(" -> residual ")); if (res >= 0) stream_.print('+'); stream_.print(res, 1); stream_.println(F(" counts"));
  }
  if (worstDev <= 1.10f)
    stream_.println(F("Ratios near x1.00: the step table (with the shifts the resyncs tracked) still fits the cells."));
  else if (worstDev <= 1.35f) {
    stream_.print(F("The deep part of the table is off by up to ")); stream_.print((worstDev - 1.0f) * 100.0f, 0);
    stream_.print(F(" %"));
    stream_.println(F(" at some depths (the resyncs track one shift, measured at the quiet point) -- the middle steps sit a little quieter than labelled, on both channels alike."));
  } else
    stream_.println(F("Some ratios far from x1.00: the cells changed shape since the full characterization -- CAL FULL would re-measure them."));
}




// Both channels at once: every LUT step driven, read after VERIFY_SETTLE_MS (the
// accepted settling time), volume up then down. Per channel: the real loaded gain and
// its error, and the live/target ratio of each cell; then L-R and up-minus-down.
void ConsoleCommands::verifySteps() {
  LDRVolume *v[2] = {&left_, &right_};
  if (!left_.hasLut() || !right_.hasLut()) { stream_.println(F("--- VERIFY: skipped (needs a calibration on both channels) ---")); return; }
  stream_.print(F("--- VERIFY: every step, both channels, read "));
  stream_.print(VERIFY_SETTLE_MS / 1000.0f, 1);
  stream_.println(F(" s after arriving (cells settled) ---"));
  const DividerMath::Load load = AudioModel::load();
  static_assert(Results::MAXS >= VolumeLut::MAX_STEPS, "Results::MAXS must hold every step");
  Results::Verify &R = Results::verify;                 // REPORT, VOL INFO and HEALTH read it
  float (*up)[Results::MAXS] = R.up;
  float (*dn)[Results::MAXS] = R.dn;
  for (uint8_t c = 0; c < 2; c++) for (uint8_t i = 0; i < Results::MAXS; i++) {
    up[c][i] = NAN; dn[c][i] = NAN; R.upSer[c][i] = NAN; R.upShu[c][i] = NAN; R.target[i] = NAN;
  }
  const uint8_t n = left_.numSteps() > right_.numSteps() ? left_.numSteps() : right_.numSteps();
  R.n = n;
  for (uint8_t i = 0; i < n; i++) R.target[i] = left_.lut().step(i).targetDb;
  bool anyAssumed = false;
  for (uint8_t pass = 0; pass < 2; pass++) {
    const bool down = (pass == 1);
    stream_.println(down ? F("volume DOWN (30 -> 0): step | target dB | L real (err)  ser/shu live:target | R real (err)  ser/shu live:target | L-R | L up-down | R up-down")
                         : F("volume UP (0 -> 30):   step | target dB | L real (err)  ser/shu live:target | R real (err)  ser/shu live:target | L-R"));
    float worst[2] = {0, 0}, sumSq[2] = {0, 0}; uint8_t cnt[2] = {0, 0};
    for (uint8_t k = 0; k < n; k++) {
      const uint8_t i = down ? (uint8_t)(n - 1 - k) : k;
      // The transparent step (shunt open) cannot be measured by the sense circuit, and
      // driving it only adds a bright excursion before the down pass: skip it.
      if (left_.lut().isTransparent(i) || right_.lut().isTransparent(i)) continue;
      bool any = false;
      for (uint8_t c = 0; c < 2; c++) if (v[c]->driveLutStep(i)) any = true;
      if (!any) continue;
      BusyHook::wait(VERIFY_SETTLE_MS);
      stream_.print(F("  "));
      if (i < 10) stream_.print(' ');
      stream_.print(i);
      stream_.print(F(" | "));
      stream_.print(left_.lut().step(i).targetDb, 2);
      float db[2] = {NAN, NAN};
      for (uint8_t c = 0; c < 2; c++) {
        stream_.print(F(" | "));
        const VolumeLut::Entry &e = v[c]->lut().step(i);
        if (!e.valid) { stream_.print(F("--")); continue; }
        float rsh;
        float rs = v[c]->readAveraged(rsh);
        bool assumedShunt = false;
        if ((rs <= 0.0f || rsh <= 0.0f) && e.targetRsh > 0.0f) {
          // Too little measuring current (the shunt as dark as this step wants, ~Rtotal): read
          // the series with the shunt held bright -- its resistance does not depend on the
          // shunt -- and take the shunt at its target (masked by the load at these steps).
          v[c]->driveElementFine(false, (uint32_t)CAL_BRIGHT_HOLD_DUTY * DriverChannels::FINE_PER_COUNT);
          BusyHook::wait(300);
          float dummy;
          rs = v[c]->readAveraged(dummy);
          v[c]->driveElementFine(false, e.shuntFine);
          rsh = e.targetRsh;
          assumedShunt = rs > 0.0f;
          if (assumedShunt) anyAssumed = true;
        }
        if (rs <= 0.0f || rsh <= 0.0f) { stream_.print(F("unreadable")); continue; }
        db[c] = 20.0f * log10f(DividerMath::gain(rs, rsh, load));
        const float err = db[c] - e.targetDb;
        (down ? dn : up)[c][i] = db[c];
        if (!down) {
          R.upSer[c][i] = rs / e.targetRs;
          R.upShu[c][i] = e.targetRsh > 0.0f ? rsh / e.targetRsh : NAN;
        }
        stream_.print(db[c], 2);
        stream_.print(F(" ("));
        if (err >= 0) stream_.print('+');
        stream_.print(err, 2);
        stream_.print(F(")  x"));
        stream_.print(rs / e.targetRs, 2);
        if (assumedShunt) stream_.print(F("/dark*"));
        else if (e.targetRsh > 0.0f) { stream_.print(F("/x")); stream_.print(rsh / e.targetRsh, 2); }
        else stream_.print(F("/open"));
        if (fabsf(err) > worst[c]) worst[c] = fabsf(err);
        sumSq[c] += err * err; cnt[c]++;
      }
      stream_.print(F(" | "));
      if (!isnan(db[0]) && !isnan(db[1])) { const float d = db[0] - db[1]; if (d >= 0) stream_.print('+'); stream_.print(d, 2); }
      else stream_.print(F("--"));
      if (down) {
        for (uint8_t c = 0; c < 2; c++) {
          stream_.print(F(" | "));
          if (!isnan(up[c][i]) && !isnan(dn[c][i])) { const float h = up[c][i] - dn[c][i]; if (h >= 0) stream_.print('+'); stream_.print(h, 2); }
          else stream_.print(F("--"));
        }
      }
      stream_.println();
    }
    for (uint8_t c = 0; c < 2; c++) {
      stream_.print(F("  summary ")); stream_.print(c ? 'R' : 'L');
      stream_.print(down ? F(" down: ") : F(" up: "));
      stream_.print(cnt[c]); stream_.print(F(" steps, worst |error| ")); stream_.print(worst[c], 2);
      stream_.print(F(" dB, rms ")); stream_.print(cnt[c] ? sqrtf(sumSq[c] / cnt[c]) : 0.0f, 2); stream_.println(F(" dB"));
    }
  }
  float worstBal = 0, worstHyst = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (!isnan(up[0][i]) && !isnan(up[1][i]) && fabsf(up[0][i] - up[1][i]) > worstBal) worstBal = fabsf(up[0][i] - up[1][i]);
    for (uint8_t c = 0; c < 2; c++)
      if (!isnan(up[c][i]) && !isnan(dn[c][i]) && fabsf(up[c][i] - dn[c][i]) > worstHyst) worstHyst = fabsf(up[c][i] - dn[c][i]);
  }
  stream_.print(F("  worst |L-R| (up) ")); stream_.print(worstBal, 2);
  stream_.print(F(" dB, worst |up-down| ")); stream_.print(worstHyst, 2); stream_.println(F(" dB"));
  if (anyAssumed)
    stream_.println(F("  * dark*: too little measuring current at this depth -- series read with the shunt held bright, shunt taken at its target"));
  R.ms = millis() | 1;
}

void ConsoleCommands::printCurve(const char *label, const LdrCurve &curve) {
  stream_.print(label);
  stream_.print(F(" curve ("));
  stream_.print(curve.count());
  stream_.println(F(" points):"));
  for (uint8_t i = 0; i < curve.count(); i++) {
    const auto &p = curve.point(i);
    stream_.print(F("  duty="));
    stream_.print(p.duty);
    stream_.print(F("  ohms="));
    stream_.println(p.ohms, 1);
  }
}

void ConsoleCommands::printLut(const VolumeLut &lut) {
  stream_.print(F("LUT ("));
  stream_.print(lut.numSteps());
  stream_.println(F(" steps):"));
  for (uint8_t i = 0; i < lut.numSteps(); i++) {
    const auto &e = lut.step(i);
    stream_.print(F("  step "));
    stream_.print(i);
    stream_.print(F(": "));
    stream_.print(e.targetDb, 2);
    stream_.print(F("dB  Rs="));
    stream_.print(e.targetRs, 0);
    stream_.print(F(" Rsh="));
    if (e.targetRsh > 0.0f) stream_.print(e.targetRsh, 0); else stream_.print(F("open"));
    stream_.print(F("  seriesDuty="));
    stream_.print(e.seriesFine / (float)DriverChannels::FINE_PER_COUNT, 2);
    stream_.print(F(" shuntDuty="));
    stream_.print(e.shuntFine / (float)DriverChannels::FINE_PER_COUNT, 2);
    stream_.println(lut.isTransparent(i) ? F("  TRANSPARENT (series bright, shunt off)") : e.valid ? F("  OK") : F("  OUT OF RANGE"));
  }
}




// ---------------------------------------------------------------------------
// DUMP
// ---------------------------------------------------------------------------
void ConsoleCommands::printState() {
  for (uint8_t i = 0; i < DriverChannels::NUM_CHANNELS; i++) {
    auto ch = static_cast<DriverChannels::Channel>(i);
    stream_.print(F("  "));
    stream_.print(DriverChannels::name(ch));
    stream_.print(F(": duty "));
    stream_.print(driver_.getDutyFine(ch) / (float)DriverChannels::FINE_PER_COUNT, 2);
    stream_.print('/');
    stream_.println(DriverChannels::WRAP);
  }
  stream_.print(F("  relays: L "));
  stream_.print(left_.relayEnergized() ? F("ENERGIZED") : F("audio"));
  stream_.print(F(", R "));
  stream_.print(right_.relayEnergized() ? F("ENERGIZED") : F("audio"));
  stream_.print(F("   amp mute: "));
  stream_.println(board_.ampMuted() ? F("ON") : F("off"));
  stream_.print(F("  volume: L "));
  describeVolume(stream_, left_);
  stream_.print(F("   R "));
  describeVolume(stream_, right_);
  stream_.println();
}

void ConsoleCommands::printStored(LDRVolume &v, const char *label) {
  stream_.print(F("=== ")); stream_.print(label); stream_.println(F(" ==="));
  stream_.print(F("Rtotal "));
  stream_.print(v.rTotalOhms(), 0);
  stream_.print(v.rAuto() ? F(" (AUTO)") : F(" (manual)"));
  stream_.print(F("  range "));
  stream_.print(v.rangeMinOhms(), 0);
  stream_.print(F(".."));
  stream_.print(v.rangeMaxOhms(), 0);
  stream_.print(F("  depth "));
  stream_.print(v.rangeDb(), 2);
  stream_.print(F(" dB  model Rsrc="));
  stream_.print(AudioModel::sourceOhms(), 0);
  stream_.print(F(" Rload="));
  stream_.println(AudioModel::loadOhms(), 0);
  const CalMeta &m = v.calMeta();
  stream_.print(F("History: full characterizations "));
  stream_.print(m.fullCount);
  stream_.print(F(", resyncs since "));
  stream_.print(m.resyncCount);
  stream_.print(F(", last "));
  stream_.print(m.lastKind == (uint8_t)CalKind::FULL ? F("FULL") : m.lastKind == (uint8_t)CalKind::RESYNC ? F("RESYNC")
              : m.lastKind == (uint8_t)CalKind::RESOLVE ? F("RESOLVE") : F("NONE"));
  stream_.print(F(", die at full "));
  stream_.print(m.tempAtFull, 1);
  stream_.print(F(" C, at last "));
  stream_.print(m.tempAtLast, 1);
  stream_.println(F(" C"));
  const CellFingerprint &f = v.fingerprint();
  stream_.print(F("Fingerprint ("));
  stream_.print(f.valid ? F("valid") : F("none"));
  stream_.print(F(", die "));
  stream_.print(f.tempC, 1);
  stream_.println(F(" C):"));
  for (uint8_t e = 0; e < 2; e++) {
    stream_.print(e ? F("  shunt ") : F("  series"));
    for (uint8_t i = 0; i < CellCheck::NFP; i++) {
      stream_.print(F("  @")); stream_.print(f.duty[i]); stream_.print(F(" "));
      stream_.print(e ? f.shu[i] : f.ser[i], 1);
    }
    stream_.println();
  }
  printCurve("Series", v.seriesCurve());
  printCurve("Shunt", v.shuntCurve());
  printLut(v.lut());
}

void ConsoleCommands::dumpSnapshot() {
  stream_.print(F("Firmware: " FW_VERSION ", built " __DATE__ " " __TIME__ "  driver "));
  stream_.print(DRIVER_BOARD_NAME);
  stream_.print(F("  HW rev "));
  stream_.print(CAL_HW_REV);
  stream_.print(F("  cal rev "));
  stream_.print(CAL_ALGO_REV);
  stream_.print(F("  protocol "));
  stream_.print(PROTO_VERSION);
  stream_.print(F("  dither "));
  stream_.print(PWM_DITHER_LEVELS);
  stream_.print(F("  PWM "));
  stream_.print(DriverChannels::frequencyHz());
  stream_.println(F(" Hz"));
  Board::reportDieTemp(stream_, F("Die temp"));
  ThermalLog::printSince(stream_);
  stream_.print(F("  sense wiring: L top junction on "));
  stream_.print(left_.sensorTopOnA3() ? F("A3 (2nd-edition board)") : F("A1 (original board)"));
  stream_.print(F(", R on "));
  stream_.println(right_.sensorTopOnA3() ? F("A3 (2nd-edition board)") : F("A1 (original board)"));
  RailComp::report(stream_);
  ThermalComp::report(stream_);
  stream_.println(F("--- STATE ---"));
  printState();
  stream_.println(F("--- STORED CALIBRATION ---"));
  printStored(left_, "L");
  printStored(right_, "R");
  stream_.println(F("--- CAPABILITIES ---"));
  const LdrCurve *ser[2] = {&left_.seriesCurve(), &right_.seriesCurve()};
  const LdrCurve *shu[2] = {&left_.shuntCurve(), &right_.shuntCurve()};
  const char *labels[2] = {"L", "R"};
  const DarkProfile *pr[2] = {&left_.darkProfile(), &right_.darkProfile()};
  Capabilities::printReport(stream_, ser, shu, labels, 2, left_.rTotalOhms(), pr);
}

// The measured sections (cell check, steps, walk, creep) run inside VERIFY's single
// relay session: they expect both relays energized and never switch them.
void ConsoleCommands::verifyCells() {
  if (!left_.fingerprint().valid || !right_.fingerprint().valid) {
    stream_.println(F("--- cell check: skipped (no stored fingerprint) ---"));
    return;
  }
  LDRVolume *v[2] = {&left_, &right_};
  DualCalibration::checkCells(v, 2, "LR", stream_);
}

// One line per channel for VOL: the step, its dB, and what each cell is driven to and
// aimed at. With the relay energized (RELAY ON) the cells are also measured live --
// in audio mode the sensors are disconnected.
// MODEL / SET SRC / SET LOAD: the audio model in use, and the one the calibration was solved for.
void ConsoleCommands::printModel() {
  stream_.print(F("OK model: source ")); stream_.print(AudioModel::sourceOhms(), 0);
  stream_.print(F(" ohm, load "));
  if (AudioModel::loadOhms() > 0.0f) { stream_.print(AudioModel::loadOhms(), 0); stream_.print(F(" ohm")); }
  else stream_.print(F("none (unloaded)"));
  float s, l;
  if (DualCalibration::solvedModel(s, l)) {
    stream_.print(F(" | calibration solved for ")); stream_.print(s, 0); stream_.print(F(" / ")); stream_.print(l, 0);
  }
  stream_.print(F(" | AUTO ")); stream_.print(AutoPref::name(AutoPref::current()));
  { const float reach = DualCalibration::walkReachOhms(left_, right_);
    if (reach > 0.0f) { stream_.print(F(", walk reaches ")); stream_.print(reach / 1000.0f, 0); stream_.print('k'); } }
  stream_.print(F(" | CAL=")); stream_.println(DualCalibration::calNeedName());
  if (DualCalibration::calNeed() == DualCalibration::CalNeed::STALE)
    stream_.println(F("  the calibration was solved for another model: send CAL to re-solve (~2-3 min); the LDR mute stays on until then"));
}

void ConsoleCommands::printVolumeDetail(LDRVolume &v, const char *label) {
  stream_.print(F("  ")); stream_.print(label); stream_.print(F(": "));
  if (!v.hasLut() || v.currentStep() >= v.numSteps()) { stream_.println(F("no calibration")); return; }
  const uint8_t i = v.currentStep();
  const VolumeLut::Entry &e = v.lut().step(i);
  const float FPC = (float)DriverChannels::FINE_PER_COUNT;
  stream_.print(F("VOL=")); stream_.print(i);
  stream_.print(F("  ")); stream_.print(e.targetDb, 2); stream_.print(F(" dB"));
  if (v.lut().isTransparent(i)) stream_.print(F("  TRANSPARENT"));
  if (v.isMuted()) stream_.print(F("  [muted]"));
  stream_.print(F(" | series duty ")); stream_.print(e.seriesFine / FPC, 2);
  stream_.print(F(" target Rs ")); stream_.print(e.targetRs, 0);
  stream_.print(F(" | shunt duty ")); stream_.print(e.shuntFine / FPC, 2);
  if (e.targetRsh > 0.0f) { stream_.print(F(" target Rsh ")); stream_.print(e.targetRsh, 0); }
  else stream_.print(F(" (LED off, open)"));
  stream_.print(F(" | "));
  if (!v.relayEnergized()) { stream_.println(F("live: RELAY ON to measure")); return; }
  float s = 0, h = 0; uint8_t m = 0;
  for (uint8_t r = 0; r < 3; r++) {
    float vref, rs, rsh;
    if (v.adcRead(vref, rs, rsh) && rs > 0.0f && rsh > 0.0f) { s += rs; h += rsh; m++; }
    BusyHook::wait(20);
  }
  const DividerMath::Load load = AudioModel::load();
  if (!m) {
    // Too little measuring current: at the loud steps of a deep Rtotal the shunt is so dark
    // (~Rtotal) that only a few uA flow. The audio is disconnected (relay on), so read the
    // series with the shunt briefly held bright -- its resistance does not depend on the shunt.
    v.driveElementFine(false, (uint32_t)CAL_BRIGHT_HOLD_DUTY * DriverChannels::FINE_PER_COUNT);
    BusyHook::wait(300);
    float s2 = 0; uint8_t m2 = 0;
    for (uint8_t r = 0; r < 3; r++) {
      float vref, rs, rsh;
      if (v.adcRead(vref, rs, rsh) && rs > 0.0f) { s2 += rs; m2++; }
      BusyHook::wait(20);
    }
    v.driveElementFine(false, e.shuntFine);           // back to this step's shunt
    if (!m2) { stream_.println(F("live: unreadable")); return; }
    const float rs = s2 / m2;
    stream_.print(F("live Rs ")); stream_.print(rs, 0);
    stream_.print(F(" (x")); stream_.print(rs / e.targetRs, 2); stream_.print(')');
    stream_.print(F(" [read with the shunt held bright] Rsh too dark to read"));
    if (e.targetRsh > 0.0f) {
      stream_.print(F("  real ~")); stream_.print(20.0f * log10f(DividerMath::gain(rs, e.targetRsh, load)), 2);
      stream_.print(F(" dB (shunt taken at its target)"));
    }
    stream_.println();
    return;
  }
  const float rs = s / m, rsh = h / m;
  stream_.print(F("live Rs ")); stream_.print(rs, 0);
  stream_.print(F(" (x")); stream_.print(rs / e.targetRs, 2); stream_.print(')');
  stream_.print(F(" Rsh ")); stream_.print(rsh, 0);
  if (e.targetRsh > 0.0f) { stream_.print(F(" (x")); stream_.print(rsh / e.targetRsh, 2); stream_.print(')'); }
  stream_.print(F("  real ")); stream_.print(20.0f * log10f(DividerMath::gain(rs, rsh, load)), 2);
  stream_.println(F(" dB"));
}

// VOL INFO: every step, L and R side by side -- the stored settings (no live check) and,
// from the last VERIFY verify, the measured dB per channel and their match:
// match % = the quieter channel's amplitude as a share of the louder's (0.8 dB -> 91%).
void ConsoleCommands::printVolumeTable() {
  if (!left_.hasLut() || !right_.hasLut()) { stream_.println(F("ERR no calibration")); return; }
  const float FPC = (float)DriverChannels::FINE_PER_COUNT;
  const bool haveVerify = Results::verify.ms != 0;
  stream_.print(F("Rtotal ")); stream_.print(left_.rTotalOhms(), 0);
  stream_.print(left_.rAuto() ? F(" (AUTO)") : F(" (manual)"));
  stream_.print(F("  current VOL=")); stream_.print(left_.currentStep());
  if (haveVerify) {
    stream_.print(F("  | measured: last VERIFY verify, "));
    stream_.print((millis() - Results::verify.ms) / 60000UL);
    stream_.println(F(" min ago (volume up, read 3 s after arriving)"));
  } else {
    stream_.println(F("  | measured: none since boot -- run VERIFY to fill those columns"));
  }
  stream_.println(F(" step | target dB | L ser duty  Rs target | L sh duty  Rsh target | R ser duty  Rs target | R sh duty  Rsh target | L meas dB | R meas dB | L-R dB | match %"));
  const uint8_t n = left_.numSteps() > right_.numSteps() ? left_.numSteps() : right_.numSteps();
  float worst = 0.0f; uint8_t worstAt = 0; float sum = 0.0f; uint8_t cnt = 0;
  LDRVolume *v[2] = {&left_, &right_};
  for (uint8_t i = 0; i < n; i++) {
    stream_.print(i == left_.currentStep() ? F(">") : F(" "));
    if (i < 10) stream_.print(' ');
    stream_.print(i);
    stream_.print(F("  | "));
    stream_.print(left_.lut().step(i).targetDb, 2);
    for (uint8_t c = 0; c < 2; c++) {
      const VolumeLut::Entry &e = v[c]->lut().step(i);
      stream_.print(F(" | "));
      if (!e.valid) { stream_.print(F("--  -- | --  --")); continue; }
      stream_.print(e.seriesFine / FPC, 2); stream_.print(F("  ")); stream_.print(e.targetRs, 0);
      stream_.print(F(" | "));
      stream_.print(e.shuntFine / FPC, 2); stream_.print(F("  "));
      if (e.targetRsh > 0.0f) stream_.print(e.targetRsh, 0); else stream_.print(F("open"));
    }
    const float a = haveVerify ? Results::verify.up[0][i] : NAN, b = haveVerify ? Results::verify.up[1][i] : NAN;
    stream_.print(F(" | "));
    if (isnan(a)) stream_.print(F("--")); else stream_.print(a, 2);
    stream_.print(F(" | "));
    if (isnan(b)) stream_.print(F("--")); else stream_.print(b, 2);
    stream_.print(F(" | "));
    if (isnan(a) || isnan(b)) {
      stream_.print(F("-- | --"));
    } else {
      const float d = a - b;
      if (d >= 0) stream_.print('+');
      stream_.print(d, 2);
      stream_.print(F(" | "));
      stream_.print(100.0f * powf(10.0f, -fabsf(d) / 20.0f), 1);
      if (fabsf(d) > worst) { worst = fabsf(d); worstAt = i; }
      sum += fabsf(d); cnt++;
    }
    if (left_.lut().isTransparent(i)) stream_.print(F("  TRANSPARENT"));
    stream_.println();
  }
  if (cnt) {
    stream_.print(F("match: mean "));
    stream_.print(100.0f * powf(10.0f, -(sum / cnt) / 20.0f), 1);
    stream_.print(F("% (|L-R| ")); stream_.print(sum / cnt, 2);
    stream_.print(F(" dB), worst ")); stream_.print(100.0f * powf(10.0f, -worst / 20.0f), 1);
    stream_.print(F("%, at step ")); stream_.print(worstAt);
    stream_.print(F(" (")); stream_.print(worst, 2); stream_.println(F(" dB)"));
  }
  printHealth();
}

namespace {
// Median of the finite values in v[0..n) (0 if none); sorts a copy.
float medianOf(const float *v, uint8_t n) {
  float t[VolumeLut::MAX_STEPS]; uint8_t k = 0;
  for (uint8_t i = 0; i < n; i++) if (!isnan(v[i])) t[k++] = v[i];
  if (!k) return 0.0f;
  for (uint8_t i = 1; i < k; i++) { const float x = t[i]; int8_t j = (int8_t)i - 1; while (j >= 0 && t[j] > x) { t[j + 1] = t[j]; j--; } t[j + 1] = x; }
  return (k & 1) ? t[k / 2] : 0.5f * (t[k / 2 - 1] + t[k / 2]);
}
} // namespace

void ConsoleCommands::printHealth() {
  // Thresholds (findings, not calibration policy).
  static const float SER_OFF = 1.12f;     // a series cell >12% off target where it sets the level (~1 dB)
  static const float PAIR_OFF = 1.08f;    // the two channels' series >8 percent apart (~0.7 dB of balance)
  static const float SHU_OFF = 1.12f;     // a shunt >12% off where it sets the level
  static const float SHU_MATTERS = 2000.0f; // ... i.e. at steps whose shunt target is <= this
  static const uint8_t SLOW_S = 10;       // a dark-end point taking this long to settle
  static const float TEMP_DRIFT = 3.0f;   // C of die temperature since the last CAL
  LDRVolume *v[2] = {&left_, &right_};
  const char LAB[2] = {'L', 'R'};
  uint8_t warns = 0;
  stream_.println(F("--- HEALTH ---"));
  if (!left_.hasLut() || !right_.hasLut()) { stream_.println(F("  WARN no calibration on both channels -- send CAL")); return; }
  const char *SUGGEST_SER = "     suggest: CAL once warm (resync). If it comes back: clean/inspect the %c series driver (PNP, bleed, base traces -- flux leaks nA there), then swap the LDR boards between channels (the next boot re-characterizes) to tell cell from driver.";
  char buf[260];
  // 1. series cells off target / apart, from the last verify (quiet and middle steps)
  if (Results::verify.ms != 0) {
    float med[2] = {0, 0};
    for (uint8_t c = 0; c < 2; c++) {
      float lnr[VolumeLut::MAX_STEPS]; uint8_t n = 0;
      for (uint8_t i = 0; i < v[c]->numSteps(); i++) {
        const VolumeLut::Entry &e = v[c]->lut().step(i);
        if (!e.valid || v[c]->lut().isTransparent(i) || e.targetRs < v[c]->rTotalOhms() / 3.0f) continue; // series sets the level here
        lnr[n++] = isnan(Results::verify.upSer[c][i]) || Results::verify.upSer[c][i] <= 0 ? NAN : logf(Results::verify.upSer[c][i]);
      }
      med[c] = medianOf(lnr, n);
    }
    const float apart = med[1] - med[0]; // > 0: R's series further above target -> R quieter
    // Both channels off target TOGETHER (e.g. both drivers warmed since calibration):
    // the levels moved, the balance did not -- nothing to fix in either driver.
    if (fabsf(apart) <= logf(PAIR_OFF) && (fabsf(med[0]) > logf(SER_OFF) || fabsf(med[1]) > logf(SER_OFF)) &&
        (med[0] > 0) == (med[1] > 0)) {
      const float common = 0.5f * (med[0] + med[1]);
      stream_.print(F("  INFO both series sit about "));
      stream_.print(100.0f * fabsf(expf(common) - 1.0f), 0);
      stream_.print('%');
      stream_.print(common > 0 ? F(" above") : F(" below"));
      stream_.print(F(" target together -> the quiet/middle steps are about "));
      stream_.print(fabsf(8.686f * common), 1);
      stream_.print(common > 0 ? F(" dB quieter") : F(" dB louder"));
      stream_.print(F(" than calibrated on BOTH channels; L/R balance intact ("));
      stream_.print(fabsf(8.686f * apart), 2);
      stream_.println(F(" dB)."));
      stream_.println(F("     This is a common drift since the last CAL (usually warm-up). Suggest: CAL once the board is warm."));
    } else for (uint8_t c = 0; c < 2; c++) {
      const bool off = fabsf(med[c]) > logf(SER_OFF);
      const bool worseOfPair = fabsf(apart) > logf(PAIR_OFF) && fabsf(med[c]) >= fabsf(med[1 - c]);
      if (!off && !worseOfPair) continue;
      warns++;
      stream_.print(F("  WARN ")); stream_.print(LAB[c]); stream_.print(F(" series: sits "));
      stream_.print(100.0f * fabsf(expf(med[c]) - 1.0f), 0);
      stream_.print('%'); stream_.print(med[c] > 0 ? F(" above") : F(" below"));
      stream_.print(F(" target at the quiet/middle steps ("));
      stream_.print(LAB[0]); stream_.print(F(" ")); stream_.print(100.0f * (expf(med[0]) - 1.0f), 0); stream_.print(F("%, "));
      stream_.print(LAB[1]); stream_.print(F(" ")); stream_.print(100.0f * (expf(med[1]) - 1.0f), 0);
      stream_.print(F("%) -> R ")); stream_.print(fabsf(8.686f * apart), 1);
      stream_.print(apart >= 0 ? F(" dB quieter than L") : F(" dB louder than L"));
      stream_.print(F(", VERIFY ")); stream_.print((millis() - Results::verify.ms) / 60000UL); stream_.println(F(" min ago"));
      snprintf(buf, sizeof buf, SUGGEST_SER, LAB[c]);
      stream_.println(buf);
    }
    // 2. shunt cells off target where they set the level
    for (uint8_t c = 0; c < 2; c++) {
      float lnr[VolumeLut::MAX_STEPS]; uint8_t n = 0;
      for (uint8_t i = 0; i < v[c]->numSteps(); i++) {
        const VolumeLut::Entry &e = v[c]->lut().step(i);
        if (!e.valid || e.targetRsh <= 0.0f || e.targetRsh > SHU_MATTERS) continue;
        lnr[n++] = isnan(Results::verify.upShu[c][i]) || Results::verify.upShu[c][i] <= 0 ? NAN : logf(Results::verify.upShu[c][i]);
      }
      const float m = medianOf(lnr, n);
      if (fabsf(m) <= logf(SHU_OFF)) continue;
      warns++;
      stream_.print(F("  WARN ")); stream_.print(LAB[c]); stream_.print(F(" shunt: sits "));
      stream_.print(100.0f * fabsf(expf(m) - 1.0f), 0);
      stream_.print('%'); stream_.print(m > 0 ? F(" above") : F(" below"));
      stream_.println(F(" target at the steps where it sets the level"));
      snprintf(buf, sizeof buf, "     suggest: CAL (resync). If it comes back: clean/inspect the %c shunt driver, then swap the LDR boards to tell cell from driver.", LAB[c]);
      stream_.println(buf);
    }
  } else {
    stream_.println(F("  INFO no VERIFY since boot: the measured checks (cells on target, L/R balance) need one"));
  }
  // 3. the dark-end walk: memory, slow settling, an early end
  for (uint8_t c = 0; c < 2; c++) {
    const DarkProfile &p = v[c]->darkProfile();
    if (!p.okCount()) { warns++; stream_.print(F("  WARN ")); stream_.print(LAB[c]); stream_.println(F(" series: no dark-end profile -- send CAL")); continue; }
    float worstMem = 0; float worstAt = 0; uint8_t slowest = 0; float slowAt = 0;
    for (uint8_t k = 0; k < p.okCount(); k++) {
      if (p.p[k].memory > worstMem) { worstMem = p.p[k].memory; worstAt = p.p[k].ohms; }
      if (p.p[k].settleS > slowest) { slowest = p.p[k].settleS; slowAt = p.p[k].ohms; }
    }
    if (worstMem > CAPS_AUTO_MAX_MEMORY && worstAt <= v[c]->rTotalOhms() * 1.5f) {
      warns++;
      stream_.print(F("  WARN ")); stream_.print(LAB[c]); stream_.print(F(" series: memory x")); stream_.print(worstMem, 2);
      stream_.print(F(" at ")); stream_.print(worstAt / 1000.0f, 0);
      stream_.println(F("k -- the quiet level sits off for a while after max volume (VERIFY creep shows how long)"));
      stream_.println(F("     suggest: compare with the other channel; a spare cell in this position tells cell from driver"));
    }
    if (slowest >= SLOW_S) {
      warns++;
      stream_.print(F("  WARN ")); stream_.print(LAB[c]); stream_.print(F(" series: needed ")); stream_.print(slowest);
      stream_.print(F(" s to settle at ")); stream_.print(slowAt / 1000.0f, 0); stream_.println(F("k -- slower than the other cells"));
    }
    if (p.n > p.okCount()) {
      warns++;
      stream_.print(F("  WARN ")); stream_.print(LAB[c]); stream_.print(F(" series: did not settle beyond "));
      stream_.print(p.deepestOkOhms() / 1000.0f, 0); stream_.println(F("k during the last full characterization"));
    }
  }
  // 4. which channel and which limit set AUTO
  {
    using namespace Capabilities;
    const DividerMath::Load load = AudioModel::load();
    ChannelCaps cc[2]; const LdrCurve *ser[2];
    for (uint8_t c = 0; c < 2; c++) { cc[c] = measure(v[c]->seriesCurve(), v[c]->shuntCurve(), &v[c]->darkProfile()); ser[c] = &v[c]->seriesCurve(); }
    float own[2];
    for (uint8_t c = 0; c < 2; c++) own[c] = autoPickOhms(&cc[c], &ser[c], 1, load, RANGE_SAFETY_MARGIN_DB, CAPS_DITHER_LEVELS);
    if (own[0] > 0 && own[1] > 0 && fabsf(own[0] - own[1]) > 0.1f * (own[0] < own[1] ? own[0] : own[1])) {
      const uint8_t lim = own[0] < own[1] ? 0 : 1;
      const Row w = evaluate(cc[lim], *ser[lim], own[lim] + 500.0f, load, RANGE_SAFETY_MARGIN_DB, CAPS_DITHER_LEVELS);
      stream_.print(F("  INFO AUTO is set by the ")); stream_.print(LAB[lim]); stream_.print(F(" series: "));
      stream_.print(w.slope > CAPS_AUTO_MAX_SLOPE ? F("its dark end is steeper") : w.memoryUnknown ? F("its walk ended earlier")
                  : w.memory > CAPS_AUTO_MAX_MEMORY ? F("its memory is larger") : F("its step error is larger"));
      stream_.print(F(" (alone it would allow ")); stream_.print(own[lim], 0);
      stream_.print(F(", the other ")); stream_.print(own[1 - lim], 0); stream_.println(F(")"));
    }
  }
  // 5. temperature since the last calibration
  {
    const float now = Board::dieTempC(), then = left_.calMeta().tempAtLast;
    if (then > 0.0f && fabsf(now - then) >= TEMP_DRIFT) {
      warns++;
      stream_.print(F("  WARN die ")); stream_.print(now, 1); stream_.print(F(" C now, ")); stream_.print(then, 1);
      stream_.println(F(" C at the last CAL -- the knees move with temperature: send CAL"));
    }
  }
  if (!warns) stream_.println(F("  OK no findings"));
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
// ============================================================================
// The debug console's commands: ONE table, keyed by the first word (case-insensitive).
// ============================================================================
const ConsoleCommands::ConsoleCommand ConsoleCommands::kConsole[] = {
  // calibration
  {"CAL", &ConsoleCommands::cmdCal,
   "  CAL [R=<ohms>|R=AUTO]   the calibration, as the controller's CAL: checks the cells,\n"
   "                          then characterization / resync / re-solve as needed\n"
   "  CAL FULL                always a full characterization (the learning is kept)\n"
   "  CAL VERIFY              measure how well the table holds (~8 min): cell check, every step\n"
   "                          up/down, the walk again, creep; stored and compared with the last run"},
  {"FORGET", &ConsoleCommands::cmdForget,
   "  FORGET CAL|COMP|ALL     forget the calibrations / the thermal learning / both"},
  // volume
  {"VOL", &ConsoleCommands::cmdVol,
   "  VOL [<n>|UP|DOWN]       set (0-based) or report the volume, with each channel's\n"
   "                          duties and target Rs/Rsh (live too, after RELAY ON)\n"
   "  VOL INFO                every step as a table, L|R, with the L/R match measured by\n"
   "                          the last VERIFY"},
  {"BAL", &ConsoleCommands::cmdBal,
   "  BAL [-16..16]           balance in 0.25 dB units (negative lowers R); not stored"},
  {"MUTE", &ConsoleCommands::cmdMutes,     "  MUTE ON|OFF             LDR mute"},
  {"AMP_MUTE", &ConsoleCommands::cmdMutes, "  AMP_MUTE ON|OFF         amplifier mute"},
  // settings
  {"SET", &ConsoleCommands::cmdSet,
   "  SET SRC <ohms>          source (splitter) output impedance; stored; CAL re-solves\n"
   "  SET LOAD <ohms>         load after the attenuator (amp input; 0 = none); stored; CAL re-solves\n"
   "  SET AUTO SNAPPY|BALANCED|DEEP  AUTO's depth vs settling preference; stored; CAL re-solves\n"
   "  SET <channel> <duty>    raw duty 0-4095 on SER_L, SER_R, SHUNT_L or SHUNT_R"},
  {"EXPORT", &ConsoleCommands::cmdExport,
   "  EXPORT [ALL|SETTINGS|CAL|LEARN]  backup block of the stored files (text + base64, checksums)"},
  {"IMPORT", &ConsoleCommands::cmdImport,
   "  IMPORT                  restore: paste an EXPORT block; verified first, then written + restart"},
  {"SETTINGS", &ConsoleCommands::cmdSettings,
   "  SETTINGS                the stored settings (settings.txt): source, load, AUTO preference"},
  {"MODEL", &ConsoleCommands::cmdModel,
   "  MODEL                   the source/load in use and what the calibration was solved for"},
  // temperature
  {"TEMP", &ConsoleCommands::cmdTemp,
   "  TEMP                    temperatures and rail: die, driver block (NTC), 3.3 V (TL431)"},
  {"LEARN", &ConsoleCommands::cmdLearn,
   "  LEARN                   thermal learning log: knee shift per resync vs temperature changes"},
  {"COMP", &ConsoleCommands::cmdComp,
   "  COMP [ON|OFF]           thermal compensation from the learned fit (A/B; boots ON)"},
  // bench
  {"RELAY", &ConsoleCommands::cmdMutes,
   "  RELAY ON|OFF            both relays (OFF restores the volume, then reconnects)"},
  {"READ", &ConsoleCommands::cmdRead,
   "  READ                    one sensor reading per side (after RELAY ON)"},
  // diagnostics
  {"VERIFY", &ConsoleCommands::cmdVerify,
   "  VERIFY [HISTORY]        the stored CAL VERIFY runs (the last 4), one line each"},
  {"REPORT", &ConsoleCommands::cmdReport,
   "  REPORT                  compact summary for analysis: state, last calibration, last VERIFY"},
  {"DUMP", &ConsoleCommands::cmdDump,
   "  DUMP                    the stored calibration in full: curves, step tables, capabilities"},
  // information
  {"VER", &ConsoleCommands::cmdVer,
   "  VER | FW                firmware version, build, chip, flash ID, core and SDK versions"},
  {"FW", &ConsoleCommands::cmdVer, nullptr},     // alias, listed with VER
  {"HELP", &ConsoleCommands::cmdHelp, nullptr},
  {"?", &ConsoleCommands::cmdHelp, nullptr},
};

// ============================================================================
// REPORT: the compact summary to send for analysis -- the current state, the last calibration,
// and what the last VERIFY measured (Results). About 70-90 lines; VERIFY keeps printing
// everything in full.
// ============================================================================
namespace {
// A float for a report column: "--" when not available, an explicit '+' when signed.
struct Fx { char s[24]; };
Fx fx(float v, int decimals, bool sign = false) {
  Fx r;
  if (isnan(v) || isinf(v)) { strcpy(r.s, "--"); return r; }
  const Console::Num n = Console::num(v, decimals);
  snprintf(r.s, sizeof r.s, "%s%s", (sign && v >= 0.0f) ? "+" : "", n.s);
  return r;
}
// "3h05" / "12 min" / "--" for a Results timestamp.
struct Ago { char s[16]; };
Ago ago(uint32_t ms) {
  Ago r;
  if (ms == 0) { strcpy(r.s, "--"); return r; }
  const uint32_t m = (millis() - ms) / 60000UL;
  if (m < 60) snprintf(r.s, sizeof r.s, "%lu min", (unsigned long)m);
  else snprintf(r.s, sizeof r.s, "%luh%02lu", (unsigned long)(m / 60), (unsigned long)(m % 60));
  return r;
}
// "this power-up, 25 min ago" / "2 restarts ago" for a record.
struct When { char s[40]; };
When whenOf(const VerifyHistory::Record &r) {
  When w;
  const uint16_t now = Settings::bootNumber();
  if (r.boot == now) {
    const uint32_t m = (millis() / 1000 - r.upS) / 60;
    if (m < 60) snprintf(w.s, sizeof w.s, "this power-up, %lu min ago", (unsigned long)m);
    else snprintf(w.s, sizeof w.s, "this power-up, %luh%02lu ago", (unsigned long)(m / 60), (unsigned long)(m % 60));
  } else {
    snprintf(w.s, sizeof w.s, "%u restart%s ago", (unsigned)(now - r.boot), now - r.boot == 1 ? "" : "s");
  }
  return w;
}

const char *kindName(uint8_t k) {
  return k == (uint8_t)CalKind::FULL ? "FULL" : k == (uint8_t)CalKind::RESYNC ? "RESYNC" : k == (uint8_t)CalKind::RESOLVE ? "RESOLVE" : "NONE";
}
} // namespace

void ConsoleCommands::cmdReport(Args &) {
  Console out(stream_);
  char iso[24]; VersionInfo::isoBuild(iso, sizeof iso);
  const uint32_t up = millis() / 1000;
  out.line("===== REPORT BEGIN (send everything down to REPORT END) =====");
  out.line("FW %s  protocol %u  built %s  uptime %lu:%02lu", FW_VERSION_ID, (unsigned)PROTO_VERSION, iso,
           (unsigned long)(up / 3600), (unsigned long)(up / 60 % 60));
  // ---- state ----
  out.line("STATE  VOL L %u R %u (%s dB)  LDR mute %d  amp mute %d  BAL %d  CAL=%s",
           left_.currentStep(), right_.currentStep(),
           fx(left_.hasLut() ? left_.lut().step(left_.currentStep()).targetDb : NAN, 2).s,
           left_.isMuted() ? 1 : 0, board_.ampMuted() ? 1 : 0, (int)Balance::get(), DualCalibration::calNeedName());
  const float b = ThermalComp::blockAverage(), b0 = ThermalComp::blockAtCalibration();
  out.line("TEMP   die %s C  block %s C (since calibration %s)  rail %s V  NTC %s",
           fx(Board::dieTempC(), 1).s, fx(b, 2).s, fx((isnan(b) || isnan(b0)) ? NAN : b - b0, 2, true).s,
           fx(RailComp::averageVolts(), 4).s, BoardSensors::ntcStateName(Board::ntcState()));
  {
    uint8_t n; float spread; ThermalLog::fitCounts(n, spread);
    float k[2], r2[2]; bool usable[2]; ThermalLog::blockSlopes(k, r2, usable);
    const bool aL = ThermalComp::sideActive(0), aR = ThermalComp::sideActive(1);
    const ThermalComp::Hint h = ThermalComp::hint();
    out.line("COMP   %s  L k %s R2 %s off %s | R k %s R2 %s off %s | log %u, spread %s C | hint RESYNC=%d WHY=%s",
             !ThermalComp::enabled() ? "OFF" : (aL && aR) ? "ON" : (aL || aR) ? "PART" : "LEARNING",
             fx(k[0], 2, true).s, fx(r2[0], 2).s, fx(aL ? ThermalComp::sideOffsetCounts(0) : NAN, 2, true).s,
             fx(k[1], 2, true).s, fx(r2[1], 2).s, fx(aR ? ThermalComp::sideOffsetCounts(1) : NAN, 2, true).s,
             (unsigned)n, fx(n ? spread : NAN, 2).s, ThermalComp::hintRecommends(h) ? 1 : 0, ThermalComp::hintName(h));
  }
  // ---- the stored table ----
  out.line("TABLE  Rtotal %s %s (%s)  depth %s dB  walk reach %sk",
           fx(left_.rTotalOhms(), 0).s, left_.rAuto() ? "AUTO" : "manual", AutoPref::name(AutoPref::current()),
           fx(left_.rangeDb(), 2).s, fx(DualCalibration::walkReachOhms(left_, right_) / 1000.0f, 0).s);
  {
    const CalMeta &m = left_.calMeta();
    out.line("       history: full %u, resyncs since %u, last %s", (unsigned)m.fullCount, (unsigned)m.resyncCount, kindName(m.lastKind));
  }
  printModel();
  // ---- the last calibration ----
  {
    const Results::Cal &c = Results::cal;
    if (c.ms == 0) out.line("LASTCAL --  (none since power-up)");
    else {
      out.line("LASTCAL %s, %s ago, %lu s | seek L %s R %s counts | quiet match R-L %s dB | hold %u s",
               kindName(c.kind), ago(c.ms).s, (unsigned long)c.durS, fx(c.shift[0], 2, true).s, fx(c.shift[1], 2, true).s,
               fx(c.matchDb, 2, true).s, (unsigned)c.holdS);
      out.line("        deep refine L %s..%s (%u steps) R %s..%s (%u steps) | guards shunt L %u R %u, series L %u R %u",
               fx(c.refineMin[0], 2, true).s, fx(c.refineMax[0], 2, true).s, (unsigned)c.refineN[0],
               fx(c.refineMin[1], 2, true).s, fx(c.refineMax[1], 2, true).s, (unsigned)c.refineN[1],
               (unsigned)c.guardShunt[0], (unsigned)c.guardShunt[1], (unsigned)c.guardSeries[0], (unsigned)c.guardSeries[1]);
    }
  }
  // ---- cells ----
  {
    const Results::Cells &c = Results::cells;
    if (c.ms == 0) out.line("CELLS  --");
    else out.line("CELLS  L x%s (%u off, tol x%s) %s | R x%s (%u off, tol x%s) %s  (%s ago)",
                  fx(c.worst[0], 3).s, (unsigned)c.nOff[0], fx(c.tol[0], 3).s, c.changed[0] ? "CHANGED" : "MATCH",
                  fx(c.worst[1], 3).s, (unsigned)c.nOff[1], fx(c.tol[1], 3).s, c.changed[1] ? "CHANGED" : "MATCH", ago(c.ms).s);
  }
  // ---- the last walk ----
  {
    const Results::Walk &w = Results::walk;
    if (w.ms == 0) out.line("WALK   --");
    else {
      const VerifyHistory::Record *rec = VerifyHistory::latest();
      if (w.fromVerify && rec) out.line("WALK   (VERIFY #%u, %s) duty:kohm/settle-s, memory at 5 s (peak@s)", (unsigned)rec->seq, whenOf(*rec).s);
      else out.line("WALK   (%s, %s ago) duty:kohm/settle-s, memory at 5 s (peak@s)", w.fromVerify ? "VERIFY" : "calibration", ago(w.ms).s);
      for (uint8_t c = 0; c < 2; c++) {
        char buf[300]; size_t k = (size_t)snprintf(buf, sizeof buf, "  %c", c ? 'R' : 'L');
        for (uint8_t i = 0; i < w.p[c].n && k < sizeof buf - 40; i++) {
          const DarkPoint &d = w.p[c].p[i];
          k += (size_t)snprintf(buf + k, sizeof buf - k, " %u:%s/%u%s", (unsigned)d.duty, fx(d.ohms / 1000.0f, 0).s, (unsigned)d.settleS, d.ok ? "" : "!");
          if (d.memory > 0.0f) {
            k += (size_t)snprintf(buf + k, sizeof buf - k, " m%s", fx(d.memory, 3).s);
            if (w.peak[c][i] > d.memory + 0.005f) k += (size_t)snprintf(buf + k, sizeof buf - k, "(%s@%u)", fx(w.peak[c][i], 3).s, (unsigned)w.peakS[c][i]);
          }
        }
        out.line("%s", buf);
      }
      if (w.fromVerify) {
        out.line("  vs stored: shift L %s R %s counts | ratio L x%s..%s R x%s..%s",
                 fx(w.shift[0], 1, true).s, fx(w.shift[1], 1, true).s, fx(w.ratioMin[0], 2).s, fx(w.ratioMax[0], 2).s,
                 fx(w.ratioMin[1], 2).s, fx(w.ratioMax[1], 2).s);
        for (uint8_t c = 0; c < 2; c++)
          out.line("  %c: cells moved %s (spread %s) | table %s + compensation %s -> residual %s counts", c ? 'R' : 'L',
                   fx(w.moved[c], 1, true).s, fx(w.movedSpread[c], 1).s, fx(w.shift[c], 1, true).s, fx(w.compOff[c], 1, true).s,
                   fx(w.shift[c] + w.compOff[c] - w.moved[c], 1, true).s);
      }
    }
  }
  // ---- verify ----
  {
    const Results::Verify &v = Results::verify;
    if (v.ms == 0) out.line("STEPS  --  (run VERIFY)");
    else {
      const VerifyHistory::Record *rec = VerifyHistory::latest();
      if (rec) out.line("STEPS  (VERIFY #%u, %s) level error in dB, volume up / down; L-R measured (up)", (unsigned)rec->seq, whenOf(*rec).s);
      else out.line("STEPS  (VERIFY, %s ago) level error in dB, volume up / down; L-R measured (up)", ago(v.ms).s);
      out.line("  st  target   L up   R up   L dn   R dn    L-R");
      for (uint8_t i = 0; i < v.n; i++) {
        if (isnan(v.target[i])) continue;
        const float e[4] = {v.up[0][i] - v.target[i], v.up[1][i] - v.target[i], v.dn[0][i] - v.target[i], v.dn[1][i] - v.target[i]};
        if (isnan(e[0]) && isnan(e[1]) && isnan(e[2]) && isnan(e[3])) continue;
        out.line("  %2u %7s %6s %6s %6s %6s %6s", (unsigned)i, fx(v.target[i], 2).s, fx(e[0], 2, true).s, fx(e[1], 2, true).s,
                 fx(e[2], 2, true).s, fx(e[3], 2, true).s, fx(v.up[0][i] - v.up[1][i], 2, true).s);
      }
      float rms[4], worst[4]; uint8_t cnt[4];
      Results::errorStats(v.up[0], v.target, v.n, rms[0], worst[0], cnt[0]);
      Results::errorStats(v.up[1], v.target, v.n, rms[1], worst[1], cnt[1]);
      Results::errorStats(v.dn[0], v.target, v.n, rms[2], worst[2], cnt[2]);
      Results::errorStats(v.dn[1], v.target, v.n, rms[3], worst[3], cnt[3]);
      float lr = 0;
      for (uint8_t i = 0; i < v.n; i++) if (!isnan(v.up[0][i]) && !isnan(v.up[1][i]) && fabsf(v.up[0][i] - v.up[1][i]) > lr) lr = fabsf(v.up[0][i] - v.up[1][i]);
      out.line("  rms    L up %s R up %s | L dn %s R dn %s | worst L %s R %s | worst |L-R| %s",
               fx(rms[0], 2).s, fx(rms[1], 2).s, fx(rms[2], 2).s, fx(rms[3], 2).s,
               fx(worst[0] > worst[2] ? worst[0] : worst[2], 2).s, fx(worst[1] > worst[3] ? worst[1] : worst[3], 2).s, fx(lr, 2).s);
    }
  }
  // ---- creep (from the newest VERIFY run on record) ----
  {
    const VerifyHistory::Record *r = VerifyHistory::latest();
    if (!r) out.line("CREEP  --");
    else {
      out.line("CREEP  (VERIFY #%u, %s) series live/target; t95 = first of 3 s within 5 %%", (unsigned)r->seq, whenOf(*r).s);
      for (uint8_t ph = 0; ph < 2; ph++) {
        char t[2][8];
        for (uint8_t ch = 0; ch < 2; ch++) { if (r->t95[ph][ch] < 0) strcpy(t[ch], "--"); else snprintf(t[ch], sizeof t[ch], "%ds", r->t95[ph][ch]); }
        char pk[2][24];
        for (uint8_t ch = 0; ch < 2; ch++) {
          if (isnan(r->creepPeak[ph][ch])) strcpy(pk[ch], "--");
          else snprintf(pk[ch], sizeof pk[ch], "x%s@%ds", fx(r->creepPeak[ph][ch], 3).s, r->creepPeakT[ph][ch]);
        }
        out.line("  %s L t95 %s peak %s end %s%s | R t95 %s peak %s end %s%s",
                 ph == 0 ? "A loud->quiet:" : "B quiet->mid: ",
                 t[0], pk[0], isnan(r->creepEnd[ph][0]) ? "" : "x", fx(r->creepEnd[ph][0], 3).s,
                 t[1], pk[1], isnan(r->creepEnd[ph][1]) ? "" : "x", fx(r->creepEnd[ph][1], 3).s);
      }
      const VerifyHistory::Record *p = VerifyHistory::latest(1);
      if (p) printVerifyChanges(out, *r, *p);
      else out.line("CHANGES  -- (one VERIFY on record so far)");
    }
  }
  printHealth();
  out.line("===== REPORT END =====");
}

// SETTINGS: settings.txt as it stands (changed by SET SRC / SET LOAD / SET AUTO).
void ConsoleCommands::cmdSettings(Args &) {
  Console out(stream_);
  const SettingsText::Store &st = Settings::all();
  out.line("settings.txt (%u values):", (unsigned)st.n);
  for (uint8_t i = 0; i < st.n; i++) out.line("  %s=%s", st.key[i], st.val[i]);
}

// ============================================================================
// EXPORT / IMPORT: backup and restore of the attenuator's files (Backup.hpp for the format).
// ============================================================================
namespace {
struct Group { const char *name; const char *paths[6]; };
const Group kGroups[] = {
  {"SETTINGS", {"/settings.txt"}},
  {"CAL",      {"/cal_left.bin", "/cal_right.bin", "/model.txt", "/lastmatch.txt", "/rail_ref.txt", "/thermal_state.txt"}},
  {"LEARN",    {"/thermal_log.csv"}},
};
constexpr uint32_t IMPORT_ARENA = 32768;
constexpr uint32_t IMPORT_IDLE_MS = 60000;          // no line for a minute: the import is abandoned
uint8_t gArena[IMPORT_ARENA];                       // shared: one import at a time
Backup::Parser *gParser = nullptr;
bool gImportBusy = false;                           // either link
} // namespace

void ConsoleCommands::cmdExport(Args &args) {
  String which = args.n >= 2 ? args.tok[1] : String("ALL");
  which.toUpperCase();
  bool any = which == "ALL";
  for (const Group &g : kGroups) if (which == g.name) any = true;
  if (!any) { stream_.println(F("ERR usage: EXPORT [ALL|SETTINGS|CAL|LEARN]")); return; }
  Console out(stream_);
  out.line("%s", Backup::BEGIN_LINE);
  out.line("# FW %s, EXPORT %s -- paste everything from BEGIN to END after IMPORT", FW_VERSION_ID, which.c_str());
  unsigned files = 0;
  for (const Group &g : kGroups) {
    if (which != "ALL" && which != g.name) continue;
    for (const char *path : g.paths) {
      if (!path) continue;
      File f = LittleFS.open(path, "r");
      if (!f) { out.line("# %s: not present", path); continue; }
      const size_t n = f.size();
      if (n > IMPORT_ARENA / 2) { f.close(); out.line("# %s: %u bytes, too large -- skipped", path, (unsigned)n); continue; }
      const size_t got = f.read(gArena, n);       // the import arena doubles as the read buffer
      f.close();
      Backup::emitFile(path, gArena, got, [&](const char *line) { out.line("%s", line); });
      files++;
    }
  }
  out.line("%s %u files =====", Backup::END_PREFIX, files);
}

void ConsoleCommands::cmdImport(Args &args) {
  if (args.n >= 2 && args.tok[1].equalsIgnoreCase("CANCEL")) { cancelImport(); stream_.println(F("OK no import running")); return; }
  if (gImportBusy) { stream_.println(F("ERR an import is already running on the other link")); return; }
  static Backup::Parser parser(gArena, IMPORT_ARENA);
  parser = Backup::Parser(gArena, IMPORT_ARENA);
  gParser = &parser;
  gImportBusy = true;
  importing_ = true;
  importLastMs_ = millis();
  stream_.println(F("OK paste the EXPORT block now (BEGIN to END). Nothing is written until every file has"));
  stream_.println(F("   been received and verified; then the files are replaced and the attenuator restarts."));
  stream_.println(F("   IMPORT CANCEL (or EXIT) stops; a minute without data abandons it."));
}

void ConsoleCommands::cancelImport() {
  if (!importing_) return;
  importing_ = false;
  gImportBusy = false;
  gParser = nullptr;
}

void ConsoleCommands::processLine(const String &line) {
  if (importing_ && millis() - importLastMs_ > IMPORT_IDLE_MS) {
    cancelImport();
    stream_.println(F("ERR import abandoned (no data for a minute) -- nothing written"));
  }
  if (importing_) { importLine(line); return; }
  handleLine(line);
}

void ConsoleCommands::importLine(const String &line) {
  importLastMs_ = millis();
  String t = line; t.trim();
  if (t.equalsIgnoreCase("IMPORT CANCEL")) { cancelImport(); stream_.println(F("OK import cancelled -- nothing written")); return; }
  gParser->feed(line.c_str());
  if (gParser->state() == Backup::Parser::State::FAILED) {
    stream_.print(F("ERR import refused, nothing written: ")); stream_.println(gParser->error());
    cancelImport();
    return;
  }
  if (gParser->state() != Backup::Parser::State::DONE) return;
  // Every file received and verified: write each to <path>.imp, then replace the originals.
  Console out(stream_);
  const uint8_t n = gParser->fileCount();
  for (uint8_t i = 0; i < n; i++) {
    const Backup::FileEntry &f = gParser->file(i);
    char tmp[32]; snprintf(tmp, sizeof tmp, "%s.imp", f.path);
    File w = LittleFS.open(tmp, "w");
    const size_t put = w ? w.write(gParser->data(f), f.size) : 0;
    if (w) w.close();
    if (put != f.size) {
      for (uint8_t j = 0; j <= i; j++) { char t2[32]; snprintf(t2, sizeof t2, "%s.imp", gParser->file(j).path); LittleFS.remove(t2); }
      out.line("ERR writing %s failed -- nothing replaced", f.path);
      cancelImport();
      return;
    }
  }
  for (uint8_t i = 0; i < n; i++) {
    const Backup::FileEntry &f = gParser->file(i);
    char tmp[32]; snprintf(tmp, sizeof tmp, "%s.imp", f.path);
    if (LittleFS.rename(tmp, f.path))          // atomic: replaces the existing file (littlefs rename)
      out.line("  restored %s (%lu bytes)", f.path, (unsigned long)f.size);
    else
      out.line("  ERR replacing %s failed (the previous file is kept)", f.path);
  }
  out.line("OK %u files restored -- restarting to load them", (unsigned)n);
  stream_.flush();
  cancelImport();
  delay(300);
  rp2040.reboot();
}

// HELP: generated from the command table, in its order (EXIT belongs to the serial link).
void ConsoleCommands::printHelp() {
  stream_.println(F("--------------------------------------------------------"));
  stream_.println(F("Firmware: " FW_VERSION ", built " __DATE__ " " __TIME__));
  for (const ConsoleCommand &c : kConsole) {
    if (!c.help) continue;
    for (const char *p = c.help; *p;) {           // one println per help line
      const char *e = strchr(p, '\n');
      const size_t len = e ? (size_t)(e - p) : strlen(p);
      stream_.write((const uint8_t *)p, len);
      stream_.println();
      p += len + (e ? 1 : 0);
    }
  }
  stream_.println(F("  EXIT                    back to the strict protocol"));
  stream_.println(F("--------------------------------------------------------"));
}


void ConsoleCommands::handleLine(String line) {
  line.trim();
  if (line.length() == 0) return;
  Args a;
  int start = 0;
  while (a.n < MAX_TOK && start < (int)line.length()) {
    int sp = line.indexOf(' ', start);
    if (sp < 0) sp = line.length();
    if (sp > start) a.tok[a.n++] = line.substring(start, sp);
    start = sp + 1;
  }
  a.cmd = a.tok[0];
  a.cmd.toUpperCase();
  for (const ConsoleCommand &c : kConsole) {
    if (a.cmd == c.name) { (this->*(c.fn))(a); return; }
  }
  stream_.println(F("ERR unknown command -- HELP"));
}

void ConsoleCommands::cmdHelp(Args &args) {
  String *tok = args.tok; const int n = args.n; const String &cmd = args.cmd;
  LDRVolume *both[2] = {&left_, &right_};
  (void)tok; (void)n; (void)cmd; (void)both;
  printHelp();
}

void ConsoleCommands::cmdCal(Args &args) {
  String *tok = args.tok; const int n = args.n; const String &cmd = args.cmd;
  LDRVolume *both[2] = {&left_, &right_};
  (void)tok; (void)n; (void)cmd; (void)both;
  String rest = "";
  for (int i = 1; i < n; i++) { if (i > 1) rest += " "; rest += tok[i]; }
  rest.toUpperCase();
  CalRequest req;
  const char *why = Commands::validateCal(rest.c_str(), left_, right_, req);   // the same checks as the protocol
  if (why) { stream_.print(F("ERR ")); stream_.println(why); return; }
  if (req.verify) { runVerify(); return; }
  DualCalibration::calibrate(left_, right_, req, stream_, false);
}

void ConsoleCommands::cmdForget(Args &args) {
  String *tok = args.tok; const int n = args.n; const String &cmd = args.cmd;
  LDRVolume *both[2] = {&left_, &right_};
  (void)tok; (void)n; (void)cmd; (void)both;
  String what = n >= 2 ? tok[1] : String("");
  what.toUpperCase();
  const bool cal = (what == "CAL" || what == "ALL"), comp = (what == "COMP" || what == "ALL");
  if (!cal && !comp) {
    stream_.println(F("ERR usage: FORGET CAL | FORGET COMP | FORGET ALL"));
    stream_.println(F("    CAL   both calibrations (+ reference rail, model, environment); next CAL is full"));
    stream_.println(F("    COMP  the thermal learning log; compensation off until relearned"));
    return;
  }
  if (cal) {
    DualCalibration::forgetCalibration(left_, right_);
    stream_.println(F("OK calibrations forgotten -- the next CAL (or boot) runs a full characterization"));
  }
  if (comp) {
    DualCalibration::forgetCompensation();
    stream_.println(F("OK thermal learning forgotten -- compensation off until the next resyncs relearn it"));
  }
}

void ConsoleCommands::cmdVol(Args &args) {
  String *tok = args.tok; const int n = args.n; const String &cmd = args.cmd;
  LDRVolume *both[2] = {&left_, &right_};
  (void)tok; (void)n; (void)cmd; (void)both;
  String a = n >= 2 ? tok[1] : String("");
  a.toUpperCase();
  if (a == "INFO") { printVolumeTable(); return; }
  bool changed = false;
  if (a == "UP" || a == "DOWN") {
    bool ok = true;
    for (uint8_t c = 0; c < 2; c++) if (!(a == "UP" ? both[c]->stepUp() : both[c]->stepDown())) ok = false;
    stream_.println(ok ? F("OK") : F("ERR at the end of the range"));
    changed = ok;
  } else if (a.length() > 0) {
    bool ok[2];
    VolumeRamp::rampTo(both, 2, (uint8_t)a.toInt(), ok);
    stream_.println(ok[0] && ok[1] ? F("OK") : F("ERR step out of range or no calibration"));
    changed = ok[0] && ok[1];
  }
  // Measuring live right after a change would catch the cells mid-slide: give them the
  // accepted settling time first.
  if (changed && left_.relayEnergized() && right_.relayEnergized()) {
    stream_.print(F("  (reading after ")); stream_.print(VERIFY_SETTLE_MS / 1000.0f, 1); stream_.println(F(" s settling)"));
    BusyHook::wait(VERIFY_SETTLE_MS);
  }
  printVolumeDetail(left_, "L");
  printVolumeDetail(right_, "R");
}

void ConsoleCommands::cmdComp(Args &args) {
  String *tok = args.tok; const int n = args.n; const String &cmd = args.cmd;
  LDRVolume *both[2] = {&left_, &right_};
  (void)tok; (void)n; (void)cmd; (void)both;
  if (n >= 2 && tok[1].equalsIgnoreCase("OFF")) ThermalComp::setEnabled(false);
  else if (n >= 2 && tok[1].equalsIgnoreCase("ON")) ThermalComp::setEnabled(true);
  ThermalComp::report(stream_);
}

void ConsoleCommands::cmdLearn(Args &args) {
  String *tok = args.tok; const int n = args.n; const String &cmd = args.cmd;
  LDRVolume *both[2] = {&left_, &right_};
  (void)tok; (void)n; (void)cmd; (void)both;
  if (n >= 2 && tok[1].equalsIgnoreCase("CLEAR")) { stream_.println(F("ERR use FORGET COMP (FORGET ALL also clears the calibrations)")); return; }
  ThermalLog::printLog(stream_);
}

void ConsoleCommands::cmdTemp(Args &args) {
  String *tok = args.tok; const int n = args.n; const String &cmd = args.cmd;
  LDRVolume *both[2] = {&left_, &right_};
  (void)tok; (void)n; (void)cmd; (void)both;
  stream_.println(F("--- temperatures ---"));
  stream_.print(F("  RP2040 die     ")); stream_.print(Board::dieTempC(), 1); stream_.println(F(" C"));
  const float tb = Board::blockTempC(), vr = Board::railVolts();
  stream_.print(F("  driver block   ")); if (isnan(tb)) stream_.println(F("-- (NTC open/shorted)")); else { stream_.print(tb, 2); stream_.println(F(" C (NTC)")); }
  stream_.print(F("  3.3 V rail     ")); if (isnan(vr)) stream_.println(F("-- (TL431 not read)")); else { stream_.print(vr, 3); stream_.println(F(" V (TL431)")); }
  ThermalLog::printSince(stream_);
  RailComp::report(stream_);
  ThermalComp::report(stream_);
  stream_.print(F("  uptime         ")); stream_.print(Board::uptimeSeconds()); stream_.println(F(" s"));
}

void ConsoleCommands::cmdMutes(Args &args) {
  String *tok = args.tok; const int n = args.n; const String &cmd = args.cmd;
  LDRVolume *both[2] = {&left_, &right_};
  (void)tok; (void)n; (void)cmd; (void)both;
  String a = n >= 2 ? tok[1] : String("");
  a.toUpperCase();
  if (a != "ON" && a != "OFF") { stream_.println(F("ERR expected ON or OFF")); return; }
  const bool on = (a == "ON");
  if (cmd == "MUTE") {
    for (uint8_t c = 0; c < 2; c++) { if (on) both[c]->mute(); else both[c]->unmute(); }
    stream_.println(F("OK"));
  } else if (cmd == "AMP_MUTE") {
    Commands::ampMute(board_, on);
    stream_.println(F("OK"));
  } else {
    if (on) { for (uint8_t c = 0; c < 2; c++) both[c]->relayEnergize(true); }
    else LDRVolume::releaseToAudio(both, 2);
    stream_.println(on ? F("OK audio disconnected") : F("OK volume restored, audio reconnected"));
  }
}

void ConsoleCommands::cmdBal(Args &args) {
  String *tok = args.tok; const int n = args.n; const String &cmd = args.cmd;
  LDRVolume *both[2] = {&left_, &right_};
  (void)tok; (void)n; (void)cmd; (void)both;
  if (n >= 2) {
    const long v = tok[1].toInt();
    if (v < -Balance::MAX_UNITS || v > Balance::MAX_UNITS || (v == 0 && tok[1] != "0" && tok[1] != "+0" && tok[1] != "-0")) {
      stream_.println(F("ERR usage: BAL [-16..16]  (0.25 dB units; negative lowers R, positive lowers L)"));
      return;
    }
    Balance::set((int8_t)v, left_, right_);
  }
  stream_.print(F("OK BAL=")); stream_.print(Balance::get());
  stream_.print(F("  (L -")); stream_.print(left_.balanceAttDb(), 2);
  stream_.print(F(" dB, R -")); stream_.print(right_.balanceAttDb(), 2); stream_.println(F(" dB)"));
}

void ConsoleCommands::cmdModel(Args &args) {
  String *tok = args.tok; const int n = args.n; const String &cmd = args.cmd;
  LDRVolume *both[2] = {&left_, &right_};
  (void)tok; (void)n; (void)cmd; (void)both;
  printModel();
}

void ConsoleCommands::cmdVer(Args &args) {
  String *tok = args.tok; const int n = args.n; const String &cmd = args.cmd;
  LDRVolume *both[2] = {&left_, &right_};
  (void)tok; (void)n; (void)cmd; (void)both;
  VersionInfo::printReadable(stream_);
}

void ConsoleCommands::cmdRead(Args &args) {
  String *tok = args.tok; const int n = args.n; const String &cmd = args.cmd;
  LDRVolume *both[2] = {&left_, &right_};
  (void)tok; (void)n; (void)cmd; (void)both;
  for (uint8_t c = 0; c < 2; c++) {
    stream_.print(c ? F("R: ") : F("L: "));
    if (!both[c]->relayEnergized()) { stream_.println(F("relay in audio position -- RELAY ON first")); continue; }
    float vRef, rs, rsh;
    const bool ok = both[c]->adcRead(vRef, rs, rsh);
    stream_.print(F("Vref=")); stream_.print(vRef, 4); stream_.print('V');
    if (!ok) { stream_.println(F("  unreadable (both cells too dark?)")); continue; }
    stream_.print(F("  Rs=")); stream_.print(rs, 1);
    stream_.print(F("  Rsh=")); stream_.println(rsh, 1);
  }
}

// VERIFY: how well the stored table holds, measured -- the cell check, every step up and down,
// the dark-end walk again (compared with the stored one), creep after volume jumps, and the
// health findings. Everything is printed in full and kept for REPORT.
// VERIFY [HISTORY]: the stored runs (a run itself is started by CAL VERIFY).
void ConsoleCommands::cmdVerify(Args &args) {
  if (args.n >= 2 && !args.tok[1].equalsIgnoreCase("HISTORY")) { stream_.println(F("ERR usage: VERIFY [HISTORY] -- a run is CAL VERIFY")); return; }
  verifyHistory();
}

namespace { bool gVerifyRunning = false; }
bool ConsoleCommands::verifyRunning() { return gVerifyRunning; }

// The protocol's VERIFY: the same run, its text going nowhere (the controller's link carries
// protocol lines only); everything is stored in the history for VERIFY RESULT / REPORT.
void ConsoleCommands::runVerifyQuiet() {
  NullStream quiet;
  ConsoleCommands twin(quiet, board_, driver_, left_, right_);
  twin.runVerify();
}

void ConsoleCommands::runVerify() {
  LDRVolume *both[2] = {&left_, &right_};
  const uint32_t t0 = millis();
  gVerifyRunning = true;
  CalProgress::begin();
  CalProgress::setKind(CalProgress::Kind::VERIFY);
  {
    BusyHook::Scope busy;
    Board::CalHold ampHold(board_);
    stream_.println(F("===== VERIFY BEGIN (~8 min; REPORT afterwards gives the summary) ====="));
    Board::reportDieTemp(stream_, F("Die temp at start"));
    // ONE relay session for every measured section: engaged once here, handed back once
    // at the end (both channels together, amp held muted) -- no clicking in between.
    for (uint8_t c = 0; c < 2; c++) both[c]->relayEnergize(true);
    CalProgress::phase(CalProgress::Phase::CHECK);
    verifyCells();
    CalProgress::phase(CalProgress::Phase::STEPS);
    verifySteps();
    CalProgress::phase(CalProgress::Phase::WALK);
    verifyWalk();
    CalProgress::phase(CalProgress::Phase::CREEP);
    verifyCreep(90);
    LDRVolume::releaseToAudio(both, 2);
  }
  VerifyHistory::Record rec;
  buildVerifyRecord(rec);
  const VerifyHistory::Record *prev = VerifyHistory::latest();
  VerifyHistory::Record prevCopy;
  if (prev) prevCopy = *prev;
  VerifyHistory::add(rec);
  CalProgress::end();
  gVerifyRunning = false;
  printHealth();
  Console out(stream_);
  if (prev) printVerifyChanges(out, rec, prevCopy);
  else out.line("CHANGES  -- (the first VERIFY on record; the next one is compared with this)");
  float pick;
  const Commands::Adopt ad = Commands::adoptWalk(rec, left_, right_, pick);
  if (ad == Commands::Adopt::AUTO_WOULD_CHANGE)
    out.line("WALK     %s: %s instead of %s", Commands::adoptText(ad), fx(pick, 0).s, fx(left_.rTotalOhms(), 0).s);
  else out.line("WALK     %s", Commands::adoptText(ad));
  const VerifyHistory::Summary sm = VerifyHistory::summarize(rec);
  out.line("VERDICT  %s  (residual L %s R %s counts, middle steps L %s R %s dB)",
           VerifyAnalysis::verdictName((VerifyAnalysis::Verdict)sm.verdict), fx(sm.residual[0], 1, true).s, fx(sm.residual[1], 1, true).s,
           fx(sm.middleDb[0], 2, true).s, fx(sm.middleDb[1], 2, true).s);
  Board::reportDieTemp(stream_, F("Die temp at the end"), millis() - t0);
  out.line("===== VERIFY END (#%u stored) -- send REPORT for the summary =====", (unsigned)rec.seq);
}

// The finished VERIFY as a history record (from Results and the current state).
void ConsoleCommands::buildVerifyRecord(VerifyHistory::Record &r) {
  using VerifyHistory::STEPS;
  r.seq = VerifyHistory::nextSeq();
  r.boot = Settings::bootNumber();
  r.upS = millis() / 1000;
  snprintf(r.fw, sizeof r.fw, "%s", FW_VERSION_ID);
  r.blockC = ThermalComp::blockAverage();
  r.dieC = Board::dieTempC();
  float k[2], r2[2]; bool usable[2]; ThermalLog::blockSlopes(k, r2, usable);
  for (uint8_t c = 0; c < 2; c++) {
    r.compOff[c] = ThermalComp::sideActive(c) ? ThermalComp::sideOffsetCounts(c) : 0.0f;
    r.compK[c] = k[c];
  }
  r.rTotal = left_.rTotalOhms();
  r.calFull = left_.calMeta().fullCount; r.calResync = left_.calMeta().resyncCount;
  const Results::Verify &v = Results::verify;
  r.n = v.n < STEPS ? v.n : STEPS;
  for (uint8_t i = 0; i < STEPS; i++) {
    const bool in = i < r.n && left_.hasLut() && i < left_.numSteps();
    r.region[i] = in ? VerifyAnalysis::regionOf(left_.lut().step(i).targetRs, r.rTotal) : 0xFF;
    r.target[i] = in ? v.target[i] : NAN;
    for (uint8_t c = 0; c < 2; c++) {
      r.up[c][i] = in ? v.up[c][i] : NAN;  r.dn[c][i] = in ? v.dn[c][i] : NAN;
      r.upSer[c][i] = in ? v.upSer[c][i] : NAN; r.upShu[c][i] = in ? v.upShu[c][i] : NAN;
    }
  }
  const Results::Walk &w = Results::walk;
  for (uint8_t c = 0; c < 2; c++) {
    r.walk[c] = w.p[c];
    for (uint8_t i = 0; i < DarkProfile::MAX_POINTS; i++) { r.peak[c][i] = w.peak[c][i]; r.peakS[c][i] = w.peakS[c][i]; }
    r.trackedShift[c] = w.shift[c]; r.ratioMin[c] = w.ratioMin[c]; r.ratioMax[c] = w.ratioMax[c];
    r.moved[c] = w.moved[c]; r.movedSpread[c] = w.movedSpread[c];
    if (!isnan(w.compOff[c])) r.compOff[c] = w.compOff[c];      // the offset the walk actually ran with
  }
  const Results::Creep &cr = Results::creep;
  for (uint8_t ph = 0; ph < 2; ph++) for (uint8_t c = 0; c < 2; c++) {
    const Results::CreepSummary s = ph == 0 ? Results::summarizeCreep(cr.a[c], cr.nA) : Results::summarizeCreep(cr.b[c], cr.nB);
    r.t95[ph][c] = s.t95; r.creepPeak[ph][c] = s.peak; r.creepPeakT[ph][c] = s.peakT; r.creepEnd[ph][c] = s.end;
  }
  for (uint8_t c = 0; c < 2; c++) {
    r.cellWorst[c] = Results::cells.worst[c]; r.cellOff[c] = Results::cells.nOff[c]; r.cellChanged[c] = Results::cells.changed[c];
  }
}


// What changed between two VERIFY runs: conditions, step errors by region, the walk shift at
// equal effective drive (with its counts per C against the learned slope), creep, cell check,
// and a plain-language reading.
void ConsoleCommands::printVerifyChanges(Console &out, const VerifyHistory::Record &a, const VerifyHistory::Record &b) {
  const float dT = a.blockC - b.blockC;
  const bool sameCal = a.calFull == b.calFull && a.calResync == b.calResync && a.rTotal == b.rTotal;
  out.line("CHANGES  VERIFY #%u vs #%u (%s): block %s vs %s C (%s), compensation L %s vs %s, R %s vs %s",
           (unsigned)a.seq, (unsigned)b.seq, whenOf(b).s, fx(a.blockC, 2).s, fx(b.blockC, 2).s, fx(dT, 2, true).s,
           fx(a.compOff[0], 1, true).s, fx(b.compOff[0], 1, true).s, fx(a.compOff[1], 1, true).s, fx(b.compOff[1], 1, true).s);
  out.line("  calibration: %s", sameCal ? "the same (no CAL in between)"
                                         : "CHANGED in between (a CAL ran): step and walk changes include it");
  // steps by region
  out.line("  step error change, dB (mean per region)   quiet  middle   loud");
  const char *rows[4] = {"L up", "R up", "L dn", "R dn"};
  for (uint8_t k = 0; k < 4; k++) {
    const uint8_t c = k & 1;
    const float *ma = k < 2 ? a.up[c] : a.dn[c], *mb = k < 2 ? b.up[c] : b.dn[c];
    float ra[3], rb[3];
    VerifyAnalysis::regionMeans(ma, a.target, a.region, a.n, ra);
    VerifyAnalysis::regionMeans(mb, b.target, b.region, b.n, rb);
    out.line("    %s                                  %6s %6s %6s", rows[k],
             fx(ra[0] - rb[0], 2, true).s, fx(ra[1] - rb[1], 2, true).s, fx(ra[2] - rb[2], 2, true).s);
  }
  // the walk shift at equal effective drive
  VerifyAnalysis::Shift s[2];
  for (uint8_t c = 0; c < 2; c++) s[c] = VerifyAnalysis::walkShift(b.walk[c], b.compOff[c], a.walk[c], a.compOff[c]);
  out.line("  walk: cells moved L %s (spread %s, %u depths)  R %s (spread %s, %u depths) counts of effective drive",
           fx(s[0].mean, 1, true).s, fx(s[0].n ? s[0].max - s[0].min : NAN, 1).s, (unsigned)s[0].n,
           fx(s[1].mean, 1, true).s, fx(s[1].n ? s[1].max - s[1].min : NAN, 1).s, (unsigned)s[1].n);
  if (fabsf(dT) >= 0.5f)
    out.line("        per C of block change: L %s  R %s   (learned L %s  R %s)",
             fx(s[0].mean / dT, 2, true).s, fx(s[1].mean / dT, 2, true).s, fx(a.compK[0], 2, true).s, fx(a.compK[1], 2, true).s);
  out.line("  creep end (loud->quiet): L x%s -> x%s, R x%s -> x%s | cells worst: L x%s -> x%s, R x%s -> x%s",
           fx(b.creepEnd[0][0], 3).s, fx(a.creepEnd[0][0], 3).s, fx(b.creepEnd[0][1], 3).s, fx(a.creepEnd[0][1], 3).s,
           fx(b.cellWorst[0], 3).s, fx(a.cellWorst[0], 3).s, fx(b.cellWorst[1], 3).s, fx(a.cellWorst[1], 3).s);
  // the reading
  if (s[0].n < 2 || s[1].n < 2) { out.line("  => too few overlapping walk depths to say how the cells moved"); return; }
  const bool uni = VerifyAnalysis::uniform(s[0]) && VerifyAnalysis::uniform(s[1]);
  const bool com = VerifyAnalysis::common(s[0], s[1]);
  const float mean = 0.5f * (s[0].mean + s[1].mean);
  if (fabsf(s[0].mean) < 1.5f && fabsf(s[1].mean) < 1.5f && uni)
    out.line("  => the cells are where they were (within 1.5 counts at every depth)");
  else if (!com)
    out.line("  => L and R moved differently (L %s, R %s counts): a change in one channel -- that cell or its LED driver",
             fx(s[0].mean, 1, true).s, fx(s[1].mean, 1, true).s);
  else if (!uni)
    out.line("  => the cells' SHAPE changed: some depths moved more than others (spread up to %s counts) -- not a plain temperature shift",
             fx((s[0].max - s[0].min) > (s[1].max - s[1].min) ? s[0].max - s[0].min : s[1].max - s[1].min, 1).s);
  else if (fabsf(dT) >= 0.5f) {
    const float meas = mean / dT, learned = 0.5f * (a.compK[0] + a.compK[1]);
    out.line("  => both cells shifted uniformly, %s counts for %s C = %s/C; the learning uses %s/C%s", fx(mean, 1, true).s,
             fx(dT, 2, true).s, fx(meas, 2, true).s, fx(learned, 2, true).s,
             isnan(learned) ? "" : fabsf(meas - learned) <= 0.2f * fabsf(learned) ? " -- consistent" : " -- the compensation is off by more than 20 %");
  } else {
    // A small temperature change still moves the cells: show what the learned slope accounts for.
    const float learned = 0.5f * (a.compK[0] + a.compK[1]);
    const float expect = isnan(learned) ? NAN : learned * dT;
    if (!isnan(expect) && fabsf(mean - expect) <= 1.5f)
      out.line("  => both cells shifted uniformly by %s counts; the %s C temperature change accounts for %s of it (learned %s/C) -- consistent",
               fx(mean, 1, true).s, fx(dT, 2, true).s, fx(expect, 1, true).s, fx(learned, 2, true).s);
    else
      out.line("  => both cells shifted uniformly by %s counts; the %s C temperature change accounts for %s (learned %s/C), leaving %s -- drift, or memory from what played before",
               fx(mean, 1, true).s, fx(dT, 2, true).s, fx(expect, 1, true).s, fx(learned, 2, true).s, fx(mean - expect, 1, true).s);
  }
}

// VERIFY HISTORY: the stored runs, one line each.
void ConsoleCommands::verifyHistory() {
  Console out(stream_);
  const uint8_t n = VerifyHistory::count();
  out.line("VERIFY history: %u run%s kept (newest last)", (unsigned)n, n == 1 ? "" : "s");
  for (int8_t k = (int8_t)n - 1; k >= 0; k--) {
    const VerifyHistory::Record &r = *VerifyHistory::latest((uint8_t)k);
    float rms[2], worst[2]; uint8_t cnt[2];
    Results::errorStats(r.up[0], r.target, r.n, rms[0], worst[0], cnt[0]);
    Results::errorStats(r.up[1], r.target, r.n, rms[1], worst[1], cnt[1]);
    out.line("  #%u (%s) FW %s  block %s C  comp L %s R %s  cal %lu/%lu  Rtotal %s | rms up L %s R %s | moved L %s R %s",
             (unsigned)r.seq, whenOf(r).s, r.fw, fx(r.blockC, 2).s, fx(r.compOff[0], 1, true).s, fx(r.compOff[1], 1, true).s,
             (unsigned long)r.calFull, (unsigned long)r.calResync, fx(r.rTotal, 0).s, fx(rms[0], 2).s, fx(rms[1], 2).s,
             fx(r.moved[0], 1, true).s, fx(r.moved[1], 1, true).s);
  }
}


// DUMP: the stored calibration in full -- firmware, state, both channels' curves and step
// tables, the capabilities per Rtotal. For deep debugging; REPORT is the everyday summary.
void ConsoleCommands::cmdDump(Args &) {
  stream_.println(F("===== DUMP BEGIN ====="));
  dumpSnapshot();
  stream_.println(F("===== DUMP END ====="));
}

// SET has three forms, decided by its second word, in this order (as before): the audio model,
// the AUTO preference, and a raw PWM duty for one channel.
void ConsoleCommands::cmdSet(Args &args) {
  String *tok = args.tok; const int n = args.n; const String &cmd = args.cmd;
  LDRVolume *both[2] = {&left_, &right_};
  (void)tok; (void)n; (void)cmd; (void)both;
  if (n >= 3 && (tok[1].equalsIgnoreCase("SRC") || tok[1].equalsIgnoreCase("LOAD"))) {
    const bool src = tok[1].equalsIgnoreCase("SRC");
    const float ohms = tok[2].toFloat();
    if (!Commands::setModel(src, ohms, left_, right_)) {
      stream_.println(src ? F("ERR SET SRC <ohms>: 0..100000") : F("ERR SET LOAD <ohms>: 0 (unloaded) or 1000..100000000"));
      return;
    }
    printModel();
    return;
  }
  if (n >= 3 && tok[1].equalsIgnoreCase("AUTO")) {
    AutoPref::Pref pr;
    if (!AutoPref::parse(tok[2].c_str(), pr)) { stream_.println(F("ERR usage: SET AUTO SNAPPY | BALANCED | DEEP")); return; }
    const float pick = Commands::setAutoPref(pr, left_, right_);
    const AutoPref::Params pp = AutoPref::now();
    stream_.print(F("OK AUTO preference ")); stream_.print(AutoPref::name(pr));
    stream_.print(F(" (walk to ")); stream_.print(pp.walkTopOhms / 1000.0f, 0); stream_.print(F("k, memory <= x"));
    stream_.print(pp.memMax, 2); stream_.print(F(", at most ")); stream_.print(pp.capOhms / 1000.0f, 0); stream_.println(F("k)"));
    const float reach = DualCalibration::walkReachOhms(left_, right_);
    if (pick > 0.0f) {
      stream_.print(F("  AUTO now picks ")); stream_.print(pick, 0);
      stream_.print(F(" (current ")); stream_.print(left_.rTotalOhms(), 0); stream_.println(F(")"));
    }
    if (reach > 0.0f && pp.capOhms > reach * 1.05f) {
      stream_.print(F("  the stored walk reaches only ")); stream_.print(reach / 1000.0f, 0);
      stream_.println(F("k: for the full depth of this preference, CAL FULL (a characterization walks deeper)"));
    }
    printModel();
    return;
  }
  if (n < 3) { stream_.println(F("ERR usage: SET <SER_L|SER_R|SHUNT_L|SHUNT_R> <duty 0-4095> | SET SRC <ohms> | SET LOAD <ohms>")); return; }
  const int ch = DriverChannels::find(tok[1]);
  const long duty = tok[2].toInt();
  if (ch < 0 || duty < 0 || duty > (long)DriverChannels::WRAP) { stream_.println(F("ERR usage: SET <SER_L|SER_R|SHUNT_L|SHUNT_R> <duty 0-4095>")); return; }
  driver_.setDuty(static_cast<DriverChannels::Channel>(ch), (uint16_t)duty);
  stream_.println(F("OK"));
}
