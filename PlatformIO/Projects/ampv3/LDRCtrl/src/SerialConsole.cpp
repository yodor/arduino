#include "SerialConsole.hpp"
#include "Config.hpp"
#include "DualCalibration.hpp"
#include "CalArgs.hpp"
#include "BusyHook.hpp"
#include "Capabilities.hpp"
#include "DividerMath.hpp"
#include "VolumeRamp.hpp"

namespace {
// "VOL=8 (-40.04 dB)" -- the same number the strict master protocol uses
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
void creepLine(Stream &out, unsigned long tSec, LDRVolume *const *v, const uint8_t *step) {
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
    if (!ok || rs <= 0.0f || rsh <= 0.0f) { out.print(F("unreadable                    ")); continue; }
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


void SerialConsole::diagCreep(uint16_t quietSeconds) {
  LDRVolume *v[2] = {&left_, &right_};
  for (uint8_t c = 0; c < 2; c++) {
    if (!v[c]->hasLut() || v[c]->lowestValidStep() < 0) { stream_.println(F("  skipped: needs a calibration on both sides")); return; }
  }
  stream_.println(F("--- CREEP: settling after volume jumps ---"));
  stream_.println(F("Ratios are live resistance / LUT target for that step (x1.000 = exactly on target)."));
  for (uint8_t c = 0; c < 2; c++) v[c]->relayEnergize(true);
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
  for (uint16_t k = 0; k <= quietSeconds; k++) {
    while (millis() - t0 < (unsigned long)k * 1000UL) BusyHook::wait(10);
    creepLine(stream_, k, v, quiet);
  }
  // Phase B: quietest -> middle step (series cell BRIGHTENS), 30 s.
  stream_.println(F("--- PHASE B: quietest step -> middle step (series cell BRIGHTENS), logging 30 s ---"));
  t0 = millis();
  for (uint8_t c = 0; c < 2; c++) v[c]->driveLutStep(mid[c]);
  for (uint16_t k = 0; k <= 30; k++) {
    while (millis() - t0 < (unsigned long)k * 1000UL) BusyHook::wait(10);
    creepLine(stream_, k, v, mid);
  }
  for (uint8_t c = 0; c < 2; c++) v[c]->releaseRelayToAudio();
  Board::reportDieTemp(stream_, F("Die temp"));
}

void SerialConsole::diagWalk() {
  // Part of DIAG ALL: the calibration's own settled dark-end walk (DualCalibration::walkDarkEnd), run on
  // both series cells WITHOUT changing the calibration, then compared with the profile the
  // last full characterization stored -- do the cells still behave the way they were calibrated?
  LDRVolume *v[2] = {&left_, &right_};
  for (uint8_t c = 0; c < 2; c++) {
    if (v[c]->seriesCurve().empty() || !v[c]->adsPresent()) {
      stream_.println(F("  skipped: needs curves (a calibration) and an ADS1115 on both sides"));
      return;
    }
  }
  stream_.println(F("--- DARK-END WALK: run again WITHOUT saving, compared with the stored profile ---"));
  for (uint8_t c = 0; c < 2; c++) v[c]->relayEnergize(true);
  DarkProfile now[2];
  const char lab[3] = {'L', 'R', 0};
  DualCalibration::walkDarkEnd(v, 2, lab, now, stream_);
  for (uint8_t c = 0; c < 2; c++) v[c]->releaseRelayToAudio();
  stream_.println(F("--- compared with the stored calibration ---"));
  for (uint8_t c = 0; c < 2; c++) {
    const DarkProfile &st = v[c]->darkProfile();
    stream_.print(F("  ")); stream_.print(lab[c]);
    stream_.print(F(": settles up to ")); stream_.print(now[c].deepestOkOhms() / 1000.0f, 1);
    stream_.print(F("k now, ")); stream_.print(st.deepestOkOhms() / 1000.0f, 1);
    stream_.print(F("k at calibration"));
    if (st.okCount() && now[c].okCount()) {
      // same duty in both? compare resistance there
      for (uint8_t i = 0; i < now[c].okCount(); i++) for (uint8_t k = 0; k < st.okCount(); k++) {
        if (now[c].p[i].duty != st.p[k].duty) continue;
        stream_.print(F(" | duty ")); stream_.print(now[c].p[i].duty);
        stream_.print(F(": x")); stream_.print(now[c].p[i].ohms / st.p[k].ohms, 3);
      }
    }
    stream_.println();
  }
  stream_.println(F("Ratios near x1.00 at the same duties: the stored calibration still fits. A large shift: send CAL."));
}

namespace {
float avgRead(LDRVolume &v, float &rsh) {
  float s = 0, h = 0; uint8_t m = 0;
  for (uint8_t r = 0; r < 3; r++) {
    float vref, rs, sh;
    if (v.adcRead(vref, rs, sh) && rs > 0.0f && sh > 0.0f) { s += rs; h += sh; m++; }
    BusyHook::wait(20);
  }
  rsh = m ? h / m : 0.0f;
  return m ? s / m : 0.0f;
}
} // namespace

// Both channels at once: every LUT step driven, read after VERIFY_SETTLE_MS (the
// accepted settling time), volume up then down. Per channel: the real loaded gain and
// its error, and the live/target ratio of each cell; then L-R and up-minus-down.
void SerialConsole::diagVerify() {
  LDRVolume *v[2] = {&left_, &right_};
  if (!left_.hasLut() || !right_.hasLut()) { stream_.println(F("--- VERIFY: skipped (needs a calibration on both channels) ---")); return; }
  stream_.print(F("--- VERIFY: every step, both channels, read "));
  stream_.print(VERIFY_SETTLE_MS / 1000.0f, 1);
  stream_.println(F(" s after arriving (cells settled) ---"));
  const DividerMath::Load load{AUDIO_SOURCE_OHMS, AMP_INPUT_LOAD_OHMS};
  static float up[2][VolumeLut::MAX_STEPS], dn[2][VolumeLut::MAX_STEPS];
  for (uint8_t c = 0; c < 2; c++) for (uint8_t i = 0; i < VolumeLut::MAX_STEPS; i++) {
    up[c][i] = NAN; dn[c][i] = NAN; lastUpSer_[c][i] = NAN; lastUpShu_[c][i] = NAN;
  }
  for (uint8_t c = 0; c < 2; c++) v[c]->relayEnergize(true);
  const uint8_t n = left_.numSteps() > right_.numSteps() ? left_.numSteps() : right_.numSteps();
  for (uint8_t pass = 0; pass < 2; pass++) {
    const bool down = (pass == 1);
    stream_.println(down ? F("volume DOWN (30 -> 0): step | target dB | L real (err)  ser/shu live:target | R real (err)  ser/shu live:target | L-R | L up-down | R up-down")
                         : F("volume UP (0 -> 30):   step | target dB | L real (err)  ser/shu live:target | R real (err)  ser/shu live:target | L-R"));
    float worst[2] = {0, 0}, sumSq[2] = {0, 0}; uint8_t cnt[2] = {0, 0};
    for (uint8_t k = 0; k < n; k++) {
      const uint8_t i = down ? (uint8_t)(n - 1 - k) : k;
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
        const float rs = avgRead(*v[c], rsh);
        if (rs <= 0.0f || rsh <= 0.0f) { stream_.print(F("unreadable")); continue; }
        db[c] = 20.0f * log10f(DividerMath::gain(rs, rsh, load));
        const float err = db[c] - e.targetDb;
        (down ? dn : up)[c][i] = db[c];
        if (!down) {
          lastUpSer_[c][i] = rs / e.targetRs;
          lastUpShu_[c][i] = e.targetRsh > 0.0f ? rsh / e.targetRsh : NAN;
        }
        stream_.print(db[c], 2);
        stream_.print(F(" ("));
        if (err >= 0) stream_.print('+');
        stream_.print(err, 2);
        stream_.print(F(")  x"));
        stream_.print(rs / e.targetRs, 2);
        if (e.targetRsh > 0.0f) { stream_.print(F("/x")); stream_.print(rsh / e.targetRsh, 2); }
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
  for (uint8_t i = 0; i < VolumeLut::MAX_STEPS; i++) {
    if (!isnan(up[0][i]) && !isnan(up[1][i]) && fabsf(up[0][i] - up[1][i]) > worstBal) worstBal = fabsf(up[0][i] - up[1][i]);
    for (uint8_t c = 0; c < 2; c++)
      if (!isnan(up[c][i]) && !isnan(dn[c][i]) && fabsf(up[c][i] - dn[c][i]) > worstHyst) worstHyst = fabsf(up[c][i] - dn[c][i]);
  }
  stream_.print(F("  worst |L-R| (up) ")); stream_.print(worstBal, 2);
  stream_.print(F(" dB, worst |up-down| ")); stream_.print(worstHyst, 2); stream_.println(F(" dB"));
  for (uint8_t c = 0; c < 2; c++) {
    for (uint8_t i = 0; i < VolumeLut::MAX_STEPS; i++) lastUpDb_[c][i] = up[c][i];
    v[c]->releaseRelayToAudio();
  }
  lastVerifyMs_ = millis() | 1;
}

void SerialConsole::printCurve(const char *label, const LdrCurve &curve) {
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

void SerialConsole::printLut(const VolumeLut &lut) {
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

void SerialConsole::begin() {
  stream_.println();
  stream_.println(F("=== LDR attenuator bench console (DEBUG) -- EXIT returns to the strict protocol ==="));
  printHelp();
}

void SerialConsole::poll() {
  while (stream_.available()) {
    char c = stream_.read();
    if (c == '\r' || c == '\n') {
      if (lineBuf_.length() > 0) {
        stream_.println();
        handleLine(lineBuf_);
        lineBuf_ = "";
      }
    } else {
      stream_.write(c); // local echo
      lineBuf_ += c;
    }
  }
}

void SerialConsole::printHelp() {
  stream_.println(F("--------------------------------------------------------"));
  stream_.println(F("Firmware: " FW_VERSION ", built " __DATE__ " " __TIME__));
  stream_.println(F("  CAL [R=<ohms>|R=AUTO]   the calibration, as the master's CAL: checks the cells,"));
  stream_.println(F("                          then full characterization / resync / re-solve"));
  stream_.println(F("  FORGET                  delete both stored calibrations (next CAL or boot: full)"));
  stream_.println(F("  VOL [<n>|UP|DOWN]       set (0-based) or report the volume, with each channel's"));
  stream_.println(F("                          duties and target Rs/Rsh (live too, after RELAY ON)"));
  stream_.println(F("  VOL INFO                every step as a table, L|R, with the L/R match measured by"));
  stream_.println(F("                          the last DIAG ALL"));
  stream_.println(F("  MUTE ON|OFF             LDR mute"));
  stream_.println(F("  AMP_MUTE ON|OFF         amplifier mute"));
  stream_.println(F("  SET <channel> <duty>    raw duty 0-4095 on SER_L, SER_R, SHUNT_L or SHUNT_R"));
  stream_.println(F("  RELAY ON|OFF            both relays (OFF restores the volume, then reconnects)"));
  stream_.println(F("  READ                    one sensor reading per side (after RELAY ON)"));
  stream_.println(F("  DIAG                    snapshot: firmware, state, stored calibration, capabilities"));
  stream_.println(F("  DIAG ALL                + cell check, verify up/down, dark-end walk, creep (~8-9 min)"));
  stream_.println(F("                          -- send everything from DIAG ALL BEGIN to DIAG ALL END"));
  stream_.println(F("  EXIT                    back to the strict protocol"));
  stream_.println(F("--------------------------------------------------------"));
}

// ---------------------------------------------------------------------------
// DIAG
// ---------------------------------------------------------------------------
void SerialConsole::printState() {
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

void SerialConsole::printStored(LDRVolume &v, const char *label) {
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
  stream_.print(AUDIO_SOURCE_OHMS, 0);
  stream_.print(F(" Rload="));
  stream_.println(AMP_INPUT_LOAD_OHMS, 0);
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
      stream_.print(F("  @")); stream_.print(CellCheck::DUTIES[i]); stream_.print(F(" "));
      stream_.print(e ? f.shu[i] : f.ser[i], 1);
    }
    stream_.println();
  }
  printCurve("Series", v.seriesCurve());
  printCurve("Shunt", v.shuntCurve());
  printLut(v.lut());
}

void SerialConsole::diagSnapshot() {
  stream_.print(F("Firmware: " FW_VERSION ", built " __DATE__ " " __TIME__ "  HW rev "));
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

void SerialConsole::diagCellCheck() {
  if (!left_.fingerprint().valid || !right_.fingerprint().valid) {
    stream_.println(F("--- cell check: skipped (no stored fingerprint) ---"));
    return;
  }
  LDRVolume *v[2] = {&left_, &right_};
  for (uint8_t c = 0; c < 2; c++) v[c]->relayEnergize(true);
  DualCalibration::checkCells(v, 2, "LR", stream_);
  for (uint8_t c = 0; c < 2; c++) v[c]->releaseRelayToAudio();
}

// One line per channel for VOL: the step, its dB, and what each cell is driven to and
// aimed at. With the relay energized (RELAY ON) the cells are also measured live --
// in audio mode the sensors are disconnected.
void SerialConsole::printVolumeDetail(LDRVolume &v, const char *label) {
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
  if (!m) { stream_.println(F("live: unreadable")); return; }
  const float rs = s / m, rsh = h / m;
  const DividerMath::Load load{AUDIO_SOURCE_OHMS, AMP_INPUT_LOAD_OHMS};
  stream_.print(F("live Rs ")); stream_.print(rs, 0);
  stream_.print(F(" (x")); stream_.print(rs / e.targetRs, 2); stream_.print(')');
  stream_.print(F(" Rsh ")); stream_.print(rsh, 0);
  if (e.targetRsh > 0.0f) { stream_.print(F(" (x")); stream_.print(rsh / e.targetRsh, 2); stream_.print(')'); }
  stream_.print(F("  real ")); stream_.print(20.0f * log10f(DividerMath::gain(rs, rsh, load)), 2);
  stream_.println(F(" dB"));
}

// VOL INFO: every step, L and R side by side -- the stored settings (no live check) and,
// from the last DIAG ALL verify, the measured dB per channel and their match:
// match % = the quieter channel's amplitude as a share of the louder's (0.8 dB -> 91%).
void SerialConsole::printVolumeTable() {
  if (!left_.hasLut() || !right_.hasLut()) { stream_.println(F("ERR no calibration")); return; }
  const float FPC = (float)DriverChannels::FINE_PER_COUNT;
  const bool haveVerify = lastVerifyMs_ != 0;
  stream_.print(F("Rtotal ")); stream_.print(left_.rTotalOhms(), 0);
  stream_.print(left_.rAuto() ? F(" (AUTO)") : F(" (manual)"));
  stream_.print(F("  current VOL=")); stream_.print(left_.currentStep());
  if (haveVerify) {
    stream_.print(F("  | measured: last DIAG ALL verify, "));
    stream_.print((millis() - lastVerifyMs_) / 60000UL);
    stream_.println(F(" min ago (volume up, read 3 s after arriving)"));
  } else {
    stream_.println(F("  | measured: none since boot -- run DIAG ALL to fill those columns"));
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
    const float a = haveVerify ? lastUpDb_[0][i] : NAN, b = haveVerify ? lastUpDb_[1][i] : NAN;
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

void SerialConsole::printHealth() {
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
  const char *SUGGEST_SER = "     suggest: CAL once warm (resync). If it comes back: clean/inspect the %c series driver (PNP, 1M bleed, base traces -- flux leaks nA there), then swap the LDR boards between channels (the next boot re-characterizes) to tell cell from driver.";
  char buf[260];
  // 1. series cells off target / apart, from the last verify (quiet and middle steps)
  if (lastVerifyMs_ != 0) {
    float med[2] = {0, 0};
    for (uint8_t c = 0; c < 2; c++) {
      float lnr[VolumeLut::MAX_STEPS]; uint8_t n = 0;
      for (uint8_t i = 0; i < v[c]->numSteps(); i++) {
        const VolumeLut::Entry &e = v[c]->lut().step(i);
        if (!e.valid || v[c]->lut().isTransparent(i) || e.targetRs < v[c]->rTotalOhms() / 3.0f) continue; // series sets the level here
        lnr[n++] = isnan(lastUpSer_[c][i]) || lastUpSer_[c][i] <= 0 ? NAN : logf(lastUpSer_[c][i]);
      }
      med[c] = medianOf(lnr, n);
    }
    const float apart = med[1] - med[0]; // > 0: R's series further above target -> R quieter
    // Both channels off target TOGETHER (e.g. both drivers warmed since calibration):
    // the levels moved, the balance did not -- nothing to fix in either driver.
    if (fabsf(apart) <= logf(PAIR_OFF) && fabsf(med[0]) > logf(SER_OFF) && fabsf(med[1]) > logf(SER_OFF) &&
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
      stream_.print(F(", DIAG ALL ")); stream_.print((millis() - lastVerifyMs_) / 60000UL); stream_.println(F(" min ago"));
      snprintf(buf, sizeof buf, SUGGEST_SER, LAB[c]);
      stream_.println(buf);
    }
    // 2. shunt cells off target where they set the level
    for (uint8_t c = 0; c < 2; c++) {
      float lnr[VolumeLut::MAX_STEPS]; uint8_t n = 0;
      for (uint8_t i = 0; i < v[c]->numSteps(); i++) {
        const VolumeLut::Entry &e = v[c]->lut().step(i);
        if (!e.valid || e.targetRsh <= 0.0f || e.targetRsh > SHU_MATTERS) continue;
        lnr[n++] = isnan(lastUpShu_[c][i]) || lastUpShu_[c][i] <= 0 ? NAN : logf(lastUpShu_[c][i]);
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
    stream_.println(F("  INFO no DIAG ALL since boot: the measured checks (cells on target, L/R balance) need one"));
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
      stream_.println(F("k -- the quiet level sits off for a while after max volume (DIAG ALL creep shows how long)"));
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
    const DividerMath::Load load{AUDIO_SOURCE_OHMS, AMP_INPUT_LOAD_OHMS};
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
void SerialConsole::handleLine(String line) {
  line.trim();
  if (line.length() == 0) return;
  String tok[MAX_TOK];
  int n = 0;
  int start = 0;
  while (n < MAX_TOK && start < (int)line.length()) {
    int sp = line.indexOf(' ', start);
    if (sp < 0) sp = line.length();
    if (sp > start) tok[n++] = line.substring(start, sp);
    start = sp + 1;
  }
  String cmd = tok[0];
  cmd.toUpperCase();
  LDRVolume *both[2] = {&left_, &right_};

  if (cmd == "HELP" || cmd == "?") {
    printHelp();

  } else if (cmd == "CAL") {
    bool hasR = false, rAuto = false;
    long ohms = 0;
    String rest = "";
    for (int i = 1; i < n; i++) { if (i > 1) rest += " "; rest += tok[i]; }
    rest.toUpperCase();
    if (!CalArgs::parseCal(rest.c_str(), hasR, rAuto, ohms)) { stream_.println(F("ERR usage: CAL [R=<ohms>|R=AUTO]")); return; }
    if (hasR && !(left_.rangeKnown() && right_.rangeKnown())) { stream_.println(F("ERR R range unknown -- send CAL first")); return; }
    CalRequest req;
    req.hasR = hasR; req.rAuto = rAuto; req.ohms = ohms;
    DualCalibration::calibrate(left_, right_, req, stream_, false);

  } else if (cmd == "FORGET") {
    left_.calForget();
    right_.calForget();
    stream_.println(F("OK both stored calibrations deleted -- the next CAL (or boot) runs a full characterization"));

  } else if (cmd == "VOL") {
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

  } else if (cmd == "MUTE" || cmd == "AMP_MUTE" || cmd == "RELAY") {
    String a = n >= 2 ? tok[1] : String("");
    a.toUpperCase();
    if (a != "ON" && a != "OFF") { stream_.println(F("ERR expected ON or OFF")); return; }
    const bool on = (a == "ON");
    if (cmd == "MUTE") {
      for (uint8_t c = 0; c < 2; c++) { if (on) both[c]->mute(); else both[c]->unmute(); }
      stream_.println(F("OK"));
    } else if (cmd == "AMP_MUTE") {
      if (board_.setAmpMute(on)) {
        stream_.println(F("OK"));
      } else {
        stream_.print(F("ERR boot mute hold active, "));
        stream_.print(board_.bootHoldRemainingMs());
        stream_.println(F("ms remaining"));
      }
    } else {
      for (uint8_t c = 0; c < 2; c++) { if (on) both[c]->relayEnergize(true); else both[c]->releaseRelayToAudio(); }
      stream_.println(on ? F("OK audio disconnected") : F("OK volume restored, audio reconnected"));
    }

  } else if (cmd == "SET") {
    if (n < 3) { stream_.println(F("ERR usage: SET <SER_L|SER_R|SHUNT_L|SHUNT_R> <duty 0-4095>")); return; }
    const int ch = DriverChannels::find(tok[1]);
    const long duty = tok[2].toInt();
    if (ch < 0 || duty < 0 || duty > (long)DriverChannels::WRAP) { stream_.println(F("ERR usage: SET <SER_L|SER_R|SHUNT_L|SHUNT_R> <duty 0-4095>")); return; }
    driver_.setDuty(static_cast<DriverChannels::Channel>(ch), (uint16_t)duty);
    stream_.println(F("OK"));

  } else if (cmd == "READ") {
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

  } else if (cmd == "DIAG") {
    String a = n >= 2 ? tok[1] : String("");
    a.toUpperCase();
    if (a.length() == 0) {
      stream_.println(F("===== DIAG BEGIN ====="));
      diagSnapshot();
      stream_.println(F("===== DIAG END ====="));
    } else if (a == "ALL") {
      const uint32_t t0 = millis();
      BusyHook::Scope busy;
      Board::CalHold ampHold(board_);
      stream_.println(F("===== DIAG ALL BEGIN (send everything down to DIAG ALL END) ====="));
      diagSnapshot();
      diagCellCheck();
      diagVerify();
      diagWalk();
      diagCreep(90);
      printHealth();
      Board::reportDieTemp(stream_, F("Die temp at the end"), millis() - t0);
      stream_.println(F("===== DIAG ALL END ====="));
    } else {
      stream_.println(F("ERR usage: DIAG [ALL]"));
    }

  } else {
    stream_.println(F("ERR unknown command -- HELP"));
  }
}
