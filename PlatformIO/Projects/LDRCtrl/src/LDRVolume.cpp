#include "LDRVolume.hpp"
#include "DualCalibration.hpp"
#include "BusyHook.hpp"
#include "Board.hpp"
#include "Config.hpp"
#include "DividerMath.hpp"
#include <math.h>

void LDRVolume::runDiagnosticScan(Stream &out, uint16_t startDuty, uint16_t endDuty) {
  BusyHook::Scope busy;
  Board::CalHold ampHold(board_); // amp muted for the whole run, restored after
  if (!sensor_.isPresent()) {
    out.println(F("CALSCAN: no ADS1115 detected on this channel's bus -- skipping"));
    return;
  }

  // Default window (see the header) covers a bit either side of every 24-27%
  // observation seen so far across both cells with the original 100k bleeds;
  // step=4 raw counts is ~10x finer than CALAUTO's point list, specifically to
  // see whether there's real structure between those samples or whether it's
  // genuinely this abrupt.
  static constexpr uint16_t SCAN_STEP  = 4;
  // Step-aligned and never below one step, so the descending loop cannot
  // wrap past zero and both directions hit identical duty values.
  const uint16_t SCAN_START = (startDuty < SCAN_STEP) ? SCAN_STEP : startDuty;
  uint16_t       SCAN_END   = (endDuty > DriverChannels::WRAP) ? DriverChannels::WRAP : endDuty;
  if (SCAN_END < SCAN_START + SCAN_STEP) SCAN_END = SCAN_START + SCAN_STEP;
  SCAN_END = SCAN_START + (uint16_t)(((SCAN_END - SCAN_START) / SCAN_STEP) * SCAN_STEP);

  static constexpr uint8_t  SAMPLES    = 5;
  static constexpr unsigned long SAMPLE_GAP_MS = 60;

  relayEnergize(true); // diagnostic only -- cal_/curves untouched throughout

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
      BusyHook::wait(SAMPLE_GAP_MS);
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

  for (uint8_t pass = 0; pass < 2; pass++) {
    bool wantSeries = (pass == 0);
    DriverChannels::Channel scanCh = wantSeries ? seriesCh_ : shuntCh_;
    DriverChannels::Channel holdCh = wantSeries ? shuntCh_ : seriesCh_;
    driver_.setDuty(holdCh, CAL_BRIGHT_HOLD_DUTY);

    out.print(F("--- CALSCAN "));
    out.print(wantSeries ? F("SERIES") : F("SHUNT"));
    out.println(F(" -- ascending ---"));
    for (uint16_t d = SCAN_START; d <= SCAN_END; d += SCAN_STEP) {
      driver_.setDuty(scanCh, d);
      sampleAndPrint(d, wantSeries);
    }

    out.print(F("--- CALSCAN "));
    out.print(wantSeries ? F("SERIES") : F("SHUNT"));
    out.println(F(" -- descending ---"));
    for (uint16_t d = SCAN_END; d >= SCAN_START; d -= SCAN_STEP) {
      driver_.setDuty(scanCh, d);
      sampleAndPrint(d, wantSeries);
    }
  }

  relayEnergize(false);
  out.println(F("CALSCAN done. Existing calibration/LUT untouched -- paste this output back for analysis."));
}

bool LDRVolume::effectiveFixedSeriesDuty(uint16_t &duty) const {
  if (!fixedFromOhms_) {
    duty = fixedSeriesDuty_;
    return true;
  }
  return cal_.seriesCurve().dutyForResistance(rTotalOhms_, duty);
}

float LDRVolume::computeMaxRangeDb(float marginDb) const {
  const LdrCurve &sc = cal_.seriesCurve();
  const LdrCurve &hc = cal_.shuntCurve();
  if (sc.empty() || hc.empty()) return 0.0f;

  float rshFloor = hc.point(hc.count() - 1).ohms;  // highest-duty = lowest-R point
  if (rshFloor <= 0.0f) return 0.0f;

  const DividerMath::Load load{AUDIO_SOURCE_OHMS, AMP_INPUT_LOAD_OHMS};
  float kMin, kMax;
  if (mode_ == AttenuationMode::FIXED_SERIES) {
    uint16_t fixedDuty;
    float rsFixed;
    if (!effectiveFixedSeriesDuty(fixedDuty) ||
        !sc.resistanceForDuty(fixedDuty, rsFixed) || rsFixed <= 0.0f) return 0.0f;
    kMin = DividerMath::gain(rsFixed, rshFloor, load);
    // Rsh -> infinity. Unloaded that is 1.0; with the amp's load it tops out at
    // Rload/(Rsrc+Rs+Rload). The top step itself is OUT OF RANGE by design.
    kMax = (load.loadOhms > 0.0f) ? load.loadOhms / (load.srcOhms + rsFixed + load.loadOhms) : 1.0f;
  } else {
    float rsFloor = sc.point(sc.count() - 1).ohms;
    if (rTotalOhms_ <= rshFloor || rTotalOhms_ <= rsFloor) return 0.0f;
    // Quietest: shunt at its floor. Loudest: series at its floor. Both through
    // the loaded divider, so the range is the real one.
    kMin = DividerMath::gain(rTotalOhms_ - rshFloor, rshFloor, load);
    kMax = DividerMath::gain(rsFloor, rTotalOhms_ - rsFloor, load);
  }
  if (kMax <= kMin || kMax <= 0.0f || kMin <= 0.0f) return 0.0f;

  float rangeDb = 20.0f * log10f(kMax / kMin) - marginDb;
  return (rangeDb > 0.0f) ? rangeDb : 0.0f;
}

uint8_t LDRVolume::calSolve(uint8_t steps, Stream &out) {
  // The range is never a setting: computed fresh from the measured floors
  // at every solve, then (for a stereo pair) capped at the smaller of the
  // two channels' values so L and R get identical dB per step.
  float range = computeMaxRangeDb(RANGE_SAFETY_MARGIN_DB);
  if (range <= 0.0f) {
    out.println(F("ERR cannot compute the attenuation range: curves missing, or Rtotal too small for the measured floors"));
    return 0;
  }
  if (rangeCapDb_ > 0.0f && rangeCapDb_ < range) {
    out.print(F("Range: "));
    out.print(rangeCapDb_, 2);
    out.print(F("dB (stereo-common; this channel alone could reach "));
    out.print(range, 2);
    out.println(F("dB)"));
    range = rangeCapDb_;
  } else {
    out.print(F("Range: "));
    out.print(range, 2);
    out.println(F("dB (from measured floors, minus safety margin)"));
  }

  if (mode_ == AttenuationMode::FIXED_SERIES) {
    uint16_t fixedDuty;
    float rsFixed;
    if (!effectiveFixedSeriesDuty(fixedDuty) ||
        !cal_.seriesCurve().resistanceForDuty(fixedDuty, rsFixed)) {
      out.println(F("ERR the fixed series resistance/duty falls outside the characterized series curve"));
      out.println(F("    -- characterize first, or pick a value within the curve's range"));
      return 0;
    }
    fixedSeriesDuty_ = fixedDuty; // remember what was actually used: this is what gets saved
    return cal_.solveLutFixedSeries(steps, range, rsFixed, fixedDuty);
  }
  return cal_.solveLut(steps, range, rTotalOhms_);
}

uint16_t LDRVolume::trimDuty(bool wantSeries, float targetOhms, uint16_t dutyGuess,
                              uint8_t maxProbes, float toleranceFrac) {
  DriverChannels::Channel ch = wantSeries ? seriesCh_ : shuntCh_;
  const uint16_t maxDuty = DriverChannels::WRAP;
  const int32_t MAX_STEP_STEEP = 40;    // per-probe cap while the MEASURED slope is steep (the knee)
  const int32_t MAX_STEP_FLAT  = 150;   // ...and while it is flat (bright end): tiny slope => big steps are legitimate
  const float    FLAT_SLOPE    = 3e-3f; // |d ln(ohms)/d duty| below this counts as flat
  const float    ASSUMED_SLOPE = 0.03f; // ln(ohms) per duty count, ONLY for the first probe

  const float lnTarget = logf(targetOhms);
  uint16_t duty = dutyGuess;
  uint16_t bestDuty = dutyGuess;
  float bestErr = 1e9f;                 // |relative error|; 1e9 = nothing readable yet
  bool havePrev = false;
  uint16_t prevDuty = 0;
  float prevLn = 0.0f;
  uint8_t probesUsed = 0;   // readable probes only
  uint8_t darkSteps = 0;    // unreadable probes, budgeted separately

  while (probesUsed < maxProbes) {
    driver_.setDuty(ch, duty);
    unsigned long elapsed;
    float measured = convergeRead(wantSeries, 300, elapsed);

    if (measured <= 0.0f) {
      // Unreadable. Before anything has been read this is almost always
      // "too dark for the ADC" -- step brighter and try again. That
      // walk-in is NOT charged against maxProbes (a start 50 counts too
      // dark would otherwise burn the whole budget before one reading).
      // After a good reading, losing it means we've wandered somewhere
      // odd: stop and keep the best duty seen rather than guess blind.
      if (bestErr < 1e8f) break;
      if (++darkSteps > 12) break;
      uint32_t brighter = (uint32_t)duty + 12;
      if (brighter > maxDuty) break;
      duty = (uint16_t)brighter;
      havePrev = false;
      continue;
    }
    probesUsed++;

    float relErr = (measured - targetOhms) / targetOhms;
    float absErr = fabsf(relErr);
    if (absErr < bestErr) { bestErr = absErr; bestDuty = duty; }
    if (absErr <= toleranceFrac) break;

    float lnMeasured = logf(measured);
    int32_t step;
    int32_t maxStep = MAX_STEP_STEEP;
    if (havePrev && duty != prevDuty) {
      float slope = (lnMeasured - prevLn) / ((float)duty - (float)prevDuty);
      if (slope < -1e-4f) {
        if (-slope < FLAT_SLOPE) maxStep = MAX_STEP_FLAT;
        // Resistance falls as duty rises, as it must: aim the secant at
        // the target.
        step = (int32_t)lroundf((lnTarget - lnMeasured) / slope);
      } else {
        // Flat or backwards between probes (noise, or cell lag): the
        // secant is meaningless -- take a modest step the right way.
        step = (measured > targetOhms) ? 4 : -4;
      }
    } else {
      // First probe: no slope measured yet. A conservative assumed slope
      // sizes the step; the next probe replaces it with a measured one.
      step = (int32_t)lroundf((lnMeasured - lnTarget) / ASSUMED_SLOPE);
      if (step > 12) step = 12;
      if (step < -12) step = -12;
    }
    if (step == 0) step = (measured > targetOhms) ? 1 : -1;
    if (step > maxStep) step = maxStep;
    if (step < -maxStep) step = -maxStep;

    int32_t next = (int32_t)duty + step;
    if (next < 0) next = 0;
    if (next > (int32_t)maxDuty) next = maxDuty;
    if ((uint16_t)next == duty) break;

    prevDuty = duty;
    prevLn = lnMeasured;
    havePrev = true;
    duty = (uint16_t)next;
  }
  return bestDuty;
}

void LDRVolume::trimLoop(Stream &out) {
  out.println(F("--- CALTRIM: verify + trim each step ---"));

  uint8_t n = numSteps();

  // Start from rest: drive this channel to its own first valid step and let
  // the cells settle there before measuring anything (see
  // TRIM_PRECONDITION_MS). Without this the first series measurement comes
  // straight after a sweep or a louder volume, from a bright cell that reads
  // low while it relaxes, and the search over-corrects toward too little
  // light -- worst on the slower channel.
  for (uint8_t i0 = 0; i0 < n; i0++) {
    const VolumeLut::Entry &e0 = lut().step(i0);
    if (!e0.valid) continue;
    driver_.setDuty(seriesCh_, e0.seriesDuty);
    driver_.setDuty(shuntCh_, e0.shuntDuty);
    out.println(F("  settling at step 0 before trimming..."));
    for (uint32_t waited = 0; waited < TRIM_PRECONDITION_MS; waited += 100) {
      BusyHook::wait(100);
    }
    break;
  }
  float lastSeriesTarget = -1.0f;
  uint16_t lastSeriesDuty = 0;
  for (uint8_t i = 0; i < n; i++) {
    const VolumeLut::Entry &e = lut().step(i);
    if (!e.valid) continue;

    // Capture everything BEFORE the LUT is updated. `e` is a reference
    // INTO the LUT, so once setLutStepDuty() runs it shows the NEW values:
    // the old printout read e.seriesDuty after the update and so printed
    // "new->new" -- it could never show a change, and every trim looked
    // like it had moved nothing.
    const uint16_t oldSeriesDuty = e.seriesDuty;
    const uint16_t oldShuntDuty = e.shuntDuty;
    const float targetRs = e.targetRs;
    const float targetRsh = e.targetRsh;

    uint16_t newSeriesDuty = oldSeriesDuty;
    if (mode_ == AttenuationMode::CONSTANT_RTOTAL) {
      // Steps 0..~13 all want Rs within a couple of percent of each other
      // (the series cell barely moves while the shunt does the work). Once
      // one of them has been trimmed against a live measurement, the rest
      // are the SAME physical operating point -- reuse that result instead
      // of re-measuring a slow, lag-affected value 14 times over.
      if (lastSeriesTarget > 0.0f &&
          fabsf(targetRs - lastSeriesTarget) / lastSeriesTarget < 0.02f) {
        newSeriesDuty = lastSeriesDuty;
      } else {
        newSeriesDuty = trimDuty(true, targetRs, oldSeriesDuty, 6, 0.03f);
        lastSeriesTarget = targetRs;
        lastSeriesDuty = newSeriesDuty;
      }
      // The shunt reading below needs the series cell at THIS step's
      // operating point, not wherever the last probe happened to leave it.
      driver_.setDuty(seriesCh_, newSeriesDuty);
    } // FIXED_SERIES: series stays put, by design -- not trimmed per step

    uint16_t newShuntDuty = trimDuty(false, targetRsh, oldShuntDuty, 6, 0.03f);

    cal_.setLutStepDuty(i, newSeriesDuty, newShuntDuty);

    out.print(F("  step "));
    out.print(i);
    out.print(F(": seriesDuty "));
    out.print(oldSeriesDuty);
    out.print(F("->"));
    out.print(newSeriesDuty);
    out.print(F("  shuntDuty "));
    out.print(oldShuntDuty);
    out.print(F("->"));
    out.println(newShuntDuty);
  }
}

void LDRVolume::reapplyCurrentStepOrDefault() {
  bool currentOk = hasLut() && currentStep_ < numSteps() && lut().step(currentStep_).valid;
  if (muted_) {
    // The user's LDR mute stays in force: put its static duties back (the
    // trim/sweep drove raw duty past it) and refresh what unmute() restores.
    driver_.setDuty(seriesCh_, 0);
    driver_.setDuty(shuntCh_, DriverChannels::WRAP);
    if (currentOk) {
      savedSeriesDuty_ = lut().step(currentStep_).seriesDuty;
      savedShuntDuty_ = lut().step(currentStep_).shuntDuty;
    }
    return;
  }
  if (currentOk) {
    setStep(currentStep_);
  } else {
    applyDefinedStartupState();
  }
}

void LDRVolume::runTrimPass(Stream &out) {
  BusyHook::Scope busy;
  Board::CalHold ampHold(board_); // amp muted for the whole run, restored after
  if (!hasLut()) {
    out.println(F("CALTRIM: no LUT solved yet -- run CALSOLVE or CALAUTO first"));
    return;
  }
  if (!sensor_.isPresent()) {
    out.println(F("CALTRIM: no ADS1115 detected -- skipping"));
    return;
  }

  const uint32_t t0 = millis();
  Board::reportDieTemp(out, F("Die temp at start"));
  relayEnergize(true);
  trimLoop(out);

  // Put this channel back on its real operating point WHILE the relay still
  // has the audio path disconnected, then give the cells a moment to relax
  // from the loudest probe. Without this the PWM is left wherever the last
  // probe put it (step 30, the loudest) and the audio reconnects to that.
  reapplyCurrentStepOrDefault();
  BusyHook::wait(CAL_RELAX_BEFORE_RECONNECT_MS);
  relayEnergize(false);
  Board::reportDieTemp(out, F("Die temp at end"), millis() - t0);

  bool saved = calSave();
  out.println(saved ? F("Trimmed calibration saved to flash.") : F("WARN: failed to save."));
}

void LDRVolume::begin() {
  relay_.begin();
  if (calLoad()) {
    Serial.print(F("Loaded saved calibration from "));
    Serial.println(calPath_);
  } else {
    // Deliberately NOT calibrating here. A per-channel boot calibration
    // gave each channel its own range (L and R ended up 3 dB apart) and ran
    // the two sweeps back to back. main.cpp now checks both channels once
    // they have loaded and, if either needs it, runs ONE interleaved stereo
    // calibration -- see DualCalibration::runBoth().
    Serial.print(F("No valid saved calibration ("));
    Serial.print(calPath_);
    Serial.println(F(") -- will calibrate once both channels have loaded."));
  }

  applyDefinedStartupState();
}

void LDRVolume::applyDefinedStartupState() {
  // Neither calLoad() nor runAutoCalibration() actually drives any step's
  // duties to hardware -- the latter leaves the PWM wherever the last
  // characterization point put it (typically near full brightness, not
  // any step's real target). Without this, GET VOL/currentStep() would
  // report success while the real hardware state doesn't match it at
  // all, until the next VOL command happens to differ from currentStep_
  // by coincidence.
  //
  // applyDuties() (which setStep() goes through) silently no-ops on
  // actual hardware writes if muted_ is already true -- it just
  // remembers the target for later instead of driving it, by design, so
  // a volume change made while muted takes effect instantly on unmute.
  // That's correct for normal operation, but it's exactly the wrong
  // behavior to inherit here: if muted_ is somehow already true at this
  // point (stray state from anywhere), setStep() below would silently
  // fail to drive hardware while still reporting success. Force a known,
  // clean starting state so that can't happen.
  unmute();

  // Step 0 isn't guaranteed to be VALID -- the auto-computed range
  // deliberately sits right at the edge of the measurable floor on both
  // ends, so the one step that failed to solve can just as easily be the
  // quietest one as the loudest. setStep() silently no-ops on an invalid
  // step (by design, so a bad GET/VOL request never drives garbage) --
  // which means hardcoding setStep(0) here would silently fail exactly
  // when that happens, reproducing the undefined-state problem via a
  // different route. Scan for the lowest step that's actually valid and
  // apply that instead, so this always lands on a real, defined,
  // intentional volume.
  if (hasLut()) {
    uint8_t n = numSteps();
    uint8_t startStep = 0;
    while (startStep < n && !lut().step(startStep).valid) startStep++;
    if (startStep < n) {
      setStep(startStep);
    } else {
      // Every step came back invalid (a degenerate calibration) -- no
      // real volume to offer. Fall back to the LDR network's own
      // always-available mute primitive (series->0, shunt->max) instead
      // of leaving hardware at an undefined leftover duty.
      mute();
    }
  } else {
    // No calibration at all. Same fallback -- mute() works regardless
    // of whether a LUT exists.
    mute();
  }
}

void LDRVolume::relayEnergize(bool on) {
  bool wasMuted = board_.ampMuted();
  board_.setAmpMute(true); // force mute across the transition (no-op if boot hold already has it muted)
  board_.setRelayActive(on); // tell Board a calibration/relay op is (or isn't) in flight on this channel

  relay_.energize(on);
  if (on) {
    sensor_.activate();
  } else {
    sensor_.deactivate();
  }

  BusyHook::wait(RELAY_POP_SETTLE_MS);
  board_.setAmpMute(wasMuted); // restore -- don't force-unmute if it was already muted beforehand
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
    BusyHook::wait(15);
  }
}

void LDRVolume::runAutoCalibration(CalMode mode, Stream &out) {
  // One sweep implementation for one channel or two (see DualCalibration):
  // adaptive knee refinement and dark-end handling live
  // there, so a single-channel run and the interleaved run can never
  // drift apart again.
  LDRVolume *one[1] = {this};
  DualCalibration::runChannels(one, 1, mode, out, nullptr);
}

uint8_t LDRVolume::calSolveStereo(float capDb, uint8_t steps, Stream &out) {
  rangeCapDb_ = capDb;
  uint8_t valid = calSolve(steps, out);
  rangeCapDb_ = 0.0f; // never leave a cap behind for some later, unrelated solve
  return valid;
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