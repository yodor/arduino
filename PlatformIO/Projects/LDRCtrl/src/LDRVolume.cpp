#include "LDRVolume.hpp"
#include "Config.hpp"
#include <math.h>

void LDRVolume::runDiagnosticScan(Stream &out) {
  if (!sensor_.isPresent()) {
    out.println(F("CALSCAN: no ADS1115 detected on this channel's bus -- skipping"));
    return;
  }

  // Window covers a bit either side of every 24-27% observation seen so
  // far across both cells; step=4 raw counts is ~10x finer than CALAUTO's
  // point list, specifically to see whether there's real structure
  // between those samples or whether it's genuinely this abrupt.
  static constexpr uint16_t SCAN_START = 900;
  static constexpr uint16_t SCAN_END   = 1148; // (END-START) is an exact
                                                // multiple of STEP, so
                                                // ascending/descending hit
                                                // identical duty values
  static constexpr uint16_t SCAN_STEP  = 4;
  static constexpr uint8_t  SAMPLES    = 5;
  static constexpr unsigned long SAMPLE_GAP_MS = 60;

  relayEnergize(true); // diagnostic only -- cal_/curves untouched throughout
  bool aborted = false;

  auto sampleAndPrint = [&](uint16_t duty, bool wantSeries) {
    out.print(F("  duty="));
    out.print(duty);
    out.print(wantSeries ? F("  Rs=[") : F("  Rsh=["));
    float mn = 1e12f, mx = -1e12f, sum = 0.0f;
    uint8_t validCount = 0;
    for (uint8_t i = 0; i < SAMPLES; i++) {
      float vref, rs, rsh;
      bool ok = sensor_.read(vref, rs, rsh);
      float v = wantSeries ? rs : rsh;
      if (ok) {
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        sum += v;
        validCount++;
        out.print(v, 1);
      } else {
        out.print(F("ERR"));
      }
      if (i < SAMPLES - 1) out.print(',');
      delay(SAMPLE_GAP_MS);
      if (out.available()) {
        char c = out.read();
        if (c != '\r' && c != '\n') aborted = true;
      }
    }
    out.print(']');
    if (validCount > 0) {
      float mean = sum / validCount;
      float spreadPct = (mean != 0.0f) ? ((mx - mn) / mean * 100.0f) : 0.0f;
      out.print(F("  mean="));
      out.print(mean, 1);
      out.print(F("  spread="));
      out.print(spreadPct, 1);
      out.println('%');
    } else {
      out.println(F("  (no valid samples)"));
    }
  };

  for (uint8_t pass = 0; pass < 2 && !aborted; pass++) {
    bool wantSeries = (pass == 0);
    DriverChannels::Channel scanCh = wantSeries ? seriesCh_ : shuntCh_;
    DriverChannels::Channel holdCh = wantSeries ? shuntCh_ : seriesCh_;
    driver_.setDuty(holdCh, DriverChannels::WRAP);

    out.print(F("--- CALSCAN "));
    out.print(wantSeries ? F("SERIES") : F("SHUNT"));
    out.println(F(" -- ascending ---"));
    for (uint16_t d = SCAN_START; d <= SCAN_END && !aborted; d += SCAN_STEP) {
      driver_.setDuty(scanCh, d);
      sampleAndPrint(d, wantSeries);
    }
    if (aborted) break;

    out.print(F("--- CALSCAN "));
    out.print(wantSeries ? F("SERIES") : F("SHUNT"));
    out.println(F(" -- descending ---"));
    for (uint16_t d = SCAN_END; d >= SCAN_START && !aborted; d -= SCAN_STEP) {
      driver_.setDuty(scanCh, d);
      sampleAndPrint(d, wantSeries);
    }
  }

  relayEnergize(false);
  out.println(aborted
                      ? F("CALSCAN aborted.")
                      : F("CALSCAN done. Existing calibration/LUT untouched -- paste this output back for analysis."));
}

float LDRVolume::computeMaxRangeDb(float marginDb) const {
  const LdrCurve &sc = cal_.seriesCurve();
  const LdrCurve &hc = cal_.shuntCurve();
  if (sc.empty() || hc.empty()) return 0.0f;

  float rsFloor = sc.point(sc.count() - 1).ohms;  // highest-duty = lowest-R point
  float rshFloor = hc.point(hc.count() - 1).ohms;
  if (rshFloor <= 0.0f || rTotalOhms_ <= rshFloor) return 0.0f;

  float kMin = rshFloor / rTotalOhms_;
  float kMax = 1.0f - (rsFloor / rTotalOhms_);
  if (kMax <= kMin || kMax <= 0.0f || kMin <= 0.0f) return 0.0f;

  float rangeDb = 20.0f * log10f(kMax / kMin) - marginDb;
  return (rangeDb > 0.0f) ? rangeDb : 0.0f;
}

uint8_t LDRVolume::calSolve(uint8_t steps, float rangeDb, float rTotalOhms, Stream &out) {
  float effectiveRangeDb = rangeDb;
  if (autoRange_) {
    float computed = computeMaxRangeDb(RANGE_SAFETY_MARGIN_DB);
    if (computed > 0.0f) {
      effectiveRangeDb = computed;
      rangeDb_ = computed; // remember what was actually used
      out.print(F("Auto-range: "));
      out.print(computed, 2);
      out.println(F("dB (from measured floors + Rtotal, minus safety margin)"));
    } else {
      out.println(F("WARN auto-range computation failed -- falling back to stored range"));
    }
  }

  if (mode_ == AttenuationMode::FIXED_SERIES) {
    float rsFixed;
    if (!cal_.seriesCurve().resistanceForDuty(fixedSeriesDuty_, rsFixed)) {
      out.println(F("ERR fixedSeriesDuty falls outside the characterized series curve"));
      out.println(F("    -- characterize first, or pick a duty within the curve's range"));
      return 0;
    }
    return cal_.solveLutFixedSeries(steps, effectiveRangeDb, rsFixed, fixedSeriesDuty_);
  }
  return cal_.solveLut(steps, effectiveRangeDb, rTotalOhms);
}

uint16_t LDRVolume::trimDuty(bool wantSeries, float targetOhms, uint16_t dutyGuess,
                              uint8_t maxIterations, float toleranceFrac) {
  const LdrCurve &curve = wantSeries ? cal_.seriesCurve() : cal_.shuntCurve();
  DriverChannels::Channel ch = wantSeries ? seriesCh_ : shuntCh_;
  uint16_t duty = dutyGuess;

  for (uint8_t iter = 0; iter < maxIterations; iter++) {
    driver_.setDuty(ch, duty);
    unsigned long elapsed;
    float measured = convergeRead(wantSeries, 300, elapsed); // shorter timeout than CALAUTO -- already near target
    if (measured <= 0.0f) break; // can't trim blind -- keep current duty

    float relErr = (measured - targetOhms) / targetOhms;
    if (fabsf(relErr) <= toleranceFrac) break; // close enough

    float curvePredicted;
    if (!curve.resistanceForDuty(duty, curvePredicted) || curvePredicted <= 0.0f) break;
    float driftRatio = measured / curvePredicted; // how far reality has drifted from the curve, right now
    float adjustedTarget = targetOhms / driftRatio;

    uint16_t nextDuty;
    if (!curve.dutyForResistance(adjustedTarget, nextDuty)) break; // adjusted target now outside curve's range
    if (nextDuty == duty) break; // no change -- converged
    duty = nextDuty;
  }
  return duty;
}

void LDRVolume::runTrimPass(Stream &out) {
  if (!hasLut()) {
    out.println(F("CALTRIM: no LUT solved yet -- run CALSOLVE or CALAUTO first"));
    return;
  }
  if (!sensor_.isPresent()) {
    out.println(F("CALTRIM: no ADS1115 detected -- skipping"));
    return;
  }

  relayEnergize(true);
  out.println(F("--- CALTRIM: verify + trim each step ---"));

  uint8_t n = numSteps();
  bool aborted = false;
  for (uint8_t i = 0; i < n && !aborted; i++) {
    const VolumeLut::Entry &e = lut().step(i);
    if (!e.valid) continue;

    uint16_t newSeriesDuty = e.seriesDuty;
    if (mode_ == AttenuationMode::CONSTANT_RTOTAL) {
      newSeriesDuty = trimDuty(true, e.targetRs, e.seriesDuty, 3, 0.03f);
    } // FIXED_SERIES: series stays put, by design -- not trimmed per step

    uint16_t newShuntDuty = trimDuty(false, e.targetRsh, e.shuntDuty, 3, 0.03f);

    cal_.setLutStepDuty(i, newSeriesDuty, newShuntDuty);

    out.print(F("  step "));
    out.print(i);
    out.print(F(": seriesDuty "));
    out.print(e.seriesDuty);
    out.print(F("->"));
    out.print(newSeriesDuty);
    out.print(F("  shuntDuty "));
    out.print(e.shuntDuty);
    out.print(F("->"));
    out.println(newShuntDuty);

    if (out.available()) {
      char c = out.read();
      if (c != '\r' && c != '\n') aborted = true;
    }
  }

  relayEnergize(false);
  if (aborted) {
    out.println(F("CALTRIM aborted -- not saved."));
    return;
  }
  bool saved = calSave();
  out.println(saved ? F("Trimmed calibration saved to flash.") : F("WARN: failed to save."));
}

void LDRVolume::begin() {
  relay_.begin();
  if (calLoad()) {
    Serial.print(F("Loaded saved calibration from "));
    Serial.println(calPath_);
  } else {
    Serial.print(F("No valid saved calibration ("));
    Serial.print(calPath_);
    Serial.println(F(") -- running CALAUTO FULL..."));
    runAutoCalibration(CalMode::FULL);
  }
}

void LDRVolume::relayEnergize(bool on) {
  relay_.energize(on);
  if (on) {
    sensor_.activate();
  } else {
    sensor_.deactivate();
  }
}

void LDRVolume::calBegin() {
  relayEnergize(true);
  cal_.beginCharacterization();
  currentStep_ = 0;
}

void LDRVolume::calReset() {
  relayEnergize(false);
  cal_.resetCurves();
  currentStep_ = 0;
}

void LDRVolume::applyDuties(uint16_t seriesDuty, uint16_t shuntDuty) {
  if (muted_) {
    savedSeriesDuty_ = seriesDuty;
    savedShuntDuty_ = shuntDuty;
  } else {
    driver_.setDuty(seriesCh_, seriesDuty);
    driver_.setDuty(shuntCh_, shuntDuty);
  }
}

bool LDRVolume::setStep(uint8_t step) {
  if (!hasLut() || step >= numSteps()) return false;
  const VolumeLut::Entry &e = lut().step(step);
  if (!e.valid) return false;
  applyDuties(e.seriesDuty, e.shuntDuty);
  currentStep_ = step;
  return true;
}

bool LDRVolume::stepUp() {
  if (!hasLut()) return false;
  uint8_t next = (currentStep_ + 1 < numSteps()) ? currentStep_ + 1 : currentStep_;
  return setStep(next);
}

bool LDRVolume::stepDown() {
  if (!hasLut()) return false;
  uint8_t next = (currentStep_ > 0) ? currentStep_ - 1 : 0;
  return setStep(next);
}

float LDRVolume::convergeRead(bool wantSeries, unsigned long timeoutMs, unsigned long &elapsedOut) {
  float prev = -1.0f;
  float lastGood = -1.0f;
  unsigned long start = millis();
  while (true) {
    float vRef, rs, rsh;
    bool ok = sensor_.read(vRef, rs, rsh);
    float cur = wantSeries ? rs : rsh;
    if (ok) {
      lastGood = cur;
      if (prev > 0.0f && fabsf(cur - prev) / prev < 0.01f) {
        elapsedOut = millis() - start;
        return cur;
      }
      prev = cur;
    }
    if (millis() - start >= timeoutMs) {
      elapsedOut = millis() - start;
      return lastGood; // -1.0f if we never got a single valid reading
    }
    delay(15);
  }
}

void LDRVolume::runAutoCalibration(CalMode mode, Stream &out) {
  if (!sensor_.isPresent()) {
    out.println(F("CALAUTO: no ADS1115 detected on this channel's bus -- skipping"));
    out.println(F("         (no LDR board attached for this channel?)"));
    return; // relay/curves untouched -- nothing was started
  }

  // 1% steps from 19-36%: the steep region can swing an order of
  // magnitude per single percentage point (per the earlier manual bench
  // sweep), so 2%+ steps here risk jumping clean over the range where
  // extreme LUT steps (needing several kOhm) actually live.
  static const uint16_t FULL_PCT[] = {15, 17, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                                       31, 32, 33, 34, 35, 36, 38, 40, 45, 50, 60, 70, 80, 90, 100};
  static const uint16_t FAST_PCT[] = {18, 22, 26, 32, 40, 55, 75, 100};

  const uint16_t *pts = (mode == CalMode::FULL) ? FULL_PCT : FAST_PCT;
  uint8_t n = (mode == CalMode::FULL) ? (sizeof(FULL_PCT) / sizeof(FULL_PCT[0]))
                                       : (sizeof(FAST_PCT) / sizeof(FAST_PCT[0]));
  unsigned long timeoutMs = (mode == CalMode::FULL) ? 500 : 150;

  calBegin(); // energize relay, clear curves, reset step

  out.println(F("--- CALAUTO: series sweep (shunt held bright) ---"));
  driver_.setDuty(shuntCh_, DriverChannels::WRAP);
  for (uint8_t i = 0; i < n; i++) {
    uint16_t duty = (uint16_t)(((uint32_t)pts[i] * DriverChannels::WRAP) / 100);
    driver_.setDuty(seriesCh_, duty);
    unsigned long elapsed;
    float rs = convergeRead(true, timeoutMs, elapsed);
    out.print(F("  duty="));
    out.print(duty);
    out.print(F(" ("));
    out.print(pts[i]);
    out.print(F("%)  "));
    if (rs > 0.0f) {
      bool added = cal_.feedSeriesPoint(duty, rs);
      out.print(F("Rs="));
      out.print(rs, 1);
      out.print(F(" ohm  settle="));
      out.print(elapsed);
      out.println(added ? F("ms") : F("ms  [point rejected, non-increasing duty]"));
    } else {
      out.println(F("ERR no valid reading, skipped"));
    }
    if (out.available()) {
      char c = out.read();
      if (c != '\r' && c != '\n') { out.println(F("CALAUTO aborted.")); return; }
      // else: a stray CR/LF left over from the command that invoked this
      // (e.g. terminal sends \r and \n as separate packets) -- not a
      // real abort request, keep going.
    }
  }

  out.println(F("--- CALAUTO: shunt sweep (series held bright) ---"));
  driver_.setDuty(seriesCh_, DriverChannels::WRAP);
  for (uint8_t i = 0; i < n; i++) {
    uint16_t duty = (uint16_t)(((uint32_t)pts[i] * DriverChannels::WRAP) / 100);
    driver_.setDuty(shuntCh_, duty);
    unsigned long elapsed;
    float rsh = convergeRead(false, timeoutMs, elapsed);
    out.print(F("  duty="));
    out.print(duty);
    out.print(F(" ("));
    out.print(pts[i]);
    out.print(F("%)  "));
    if (rsh > 0.0f) {
      bool added = cal_.feedShuntPoint(duty, rsh);
      out.print(F("Rsh="));
      out.print(rsh, 1);
      out.print(F(" ohm  settle="));
      out.print(elapsed);
      out.println(added ? F("ms") : F("ms  [point rejected, non-increasing duty]"));
    } else {
      out.println(F("ERR no valid reading, skipped"));
    }
    if (out.available()) {
      char c = out.read();
      if (c != '\r' && c != '\n') { out.println(F("CALAUTO aborted.")); return; }
      // else: a stray CR/LF left over from the command that invoked this
      // (e.g. terminal sends \r and \n as separate packets) -- not a
      // real abort request, keep going.
    }
  }

  // A fresh characterization just happened -- always compute range from
  // what was just measured, regardless of any prior fixed/auto setting.
  setAutoRange(true);
  uint8_t valid = calSolve(NUM_VOLUME_STEPS_DEFAULT, rangeDb_, rTotalOhms_, out);
  out.print(F("--- CALAUTO done: solved "));
  out.print(valid);
  out.print('/');
  out.print(NUM_VOLUME_STEPS_DEFAULT);
  if (mode_ == AttenuationMode::FIXED_SERIES) {
    out.print(F(" steps, FIXED_SERIES mode, seriesDuty="));
    out.print(fixedSeriesDuty_);
  } else {
    out.print(F(" steps, CONSTANT_RTOTAL mode, Rtotal="));
    out.print(rTotalOhms_, 0);
    out.print(F(" ohm"));
  }
  out.println(F(". ---"));

  bool saved = calSave();
  out.println(saved ? F("Calibration saved to flash.")
                        : F("WARN: failed to save calibration to flash."));

  relayEnergize(false); // back to normal audio mode -- verify with RELAY ON + ADCREAD if needed
}

void LDRVolume::mute() {
  if (muted_) return;
  savedSeriesDuty_ = driver_.getDuty(seriesCh_);
  savedShuntDuty_ = driver_.getDuty(shuntCh_);
  muted_ = true;
  driver_.setDuty(seriesCh_, 0);
  driver_.setDuty(shuntCh_, DriverChannels::WRAP);
}

void LDRVolume::unmute() {
  if (!muted_) return;
  muted_ = false;
  driver_.setDuty(seriesCh_, savedSeriesDuty_);
  driver_.setDuty(shuntCh_, savedShuntDuty_);
}