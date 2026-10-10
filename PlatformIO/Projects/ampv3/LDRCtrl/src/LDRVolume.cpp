#include "LDRVolume.hpp"
#include "DualCalibration.hpp"
#include "BusyHook.hpp"
#include "Board.hpp"
#include "Config.hpp"
#include "DividerMath.hpp"
#include "TrimSearch.hpp"
#include "Capabilities.hpp"
#include <math.h>

float LDRVolume::computeMaxRangeDb(float marginDb) const {
  const LdrCurve &sc = cal_.seriesCurve();
  const LdrCurve &hc = cal_.shuntCurve();
  if (sc.empty() || hc.empty()) return 0.0f;

  float rshFloor = hc.point(hc.count() - 1).ohms;  // highest-duty = lowest-R point
  if (rshFloor <= 0.0f) return 0.0f;

  const DividerMath::Load load{AUDIO_SOURCE_OHMS, AMP_INPUT_LOAD_OHMS};
  float rsFloor = sc.point(sc.count() - 1).ohms;
  if (rTotalOhms_ <= rshFloor || rTotalOhms_ <= rsFloor) return 0.0f;
  // Quietest: shunt at its floor. Loudest: series at its floor. Both through
  // the loaded divider, so the range is the real one.
  const float kMin = DividerMath::gain(rTotalOhms_ - rshFloor, rshFloor, load);
  const float kMax = DividerMath::gain(rsFloor, rTotalOhms_ - rsFloor, load);
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

  return cal_.solveLut(steps, range, rTotalOhms_);
}

uint32_t LDRVolume::trimFine(bool wantSeries, float targetOhms, uint32_t guessFine,
                             uint8_t maxProbes, float toleranceFrac) {
  DriverChannels::Channel ch = wantSeries ? seriesCh_ : shuntCh_;
  const LdrCurve &curve = wantSeries ? cal_.seriesCurve() : cal_.shuntCurve();
  bool extrapolated;
  const float slope0 = Capabilities::slopeAt(curve, targetOhms, extrapolated); // ln(ohms)/count; 0 = unknown
  TrimSearch t;
  t.begin(targetOhms, guessFine, maxProbes, toleranceFrac,
          DriverChannels::FINE_PER_COUNT, DriverChannels::FINE_MAX, slope0);
  while (true) {
    driver_.setDutyFine(ch, t.current());
    unsigned long elapsed;
    const float measured = convergeRead(wantSeries, 300, elapsed);
    if (!t.feed(measured)) break;
  }
  return t.best();
}

void LDRVolume::trimLoop(Stream &out) {
  out.println(F("--- CAL: trim each step ---"));

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
    driver_.setDutyFine(seriesCh_, e0.seriesFine);
    driver_.setDutyFine(shuntCh_, e0.shuntFine);
    out.println(F("  settling at step 0 before trimming..."));
    for (uint32_t waited = 0; waited < TRIM_PRECONDITION_MS; waited += 100) {
      BusyHook::wait(100);
    }
    break;
  }
  float lastSeriesTarget = -1.0f;
  uint32_t lastSeriesFine = 0;
  // Series targets at or above the shallowest settled point of the dark-end walk lie on
  // the part of the series curve that WAS measured settled; a quick probe there would
  // read a cell still relaxing and over-correct (the 200k lesson). Those duties stay as
  // solved; the quietest one is then placed by settleQuietPoint().
  const float settledFrom = profile_.okCount() ? profile_.shallowestOkOhms() : 1e30f;
  for (uint8_t i = 0; i < n; i++) {
    const VolumeLut::Entry &e = lut().step(i);
    if (!e.valid) continue;
    if (lut().isTransparent(i)) {
      out.print(F("  step "));
      out.print(i);
      out.println(F(": transparent (series bright, shunt off) -- not trimmed"));
      continue;
    }

    // Capture everything BEFORE the LUT is updated. `e` is a reference
    // INTO the LUT, so once setLutStepDuty() runs it shows the NEW values:
    // the old printout read e.seriesDuty after the update and so printed
    // "new->new" -- it could never show a change, and every trim looked
    // like it had moved nothing.
    const uint32_t oldSeriesFine = e.seriesFine;
    const uint32_t oldShuntFine = e.shuntFine;
    const float targetRs = e.targetRs;
    const float targetRsh = e.targetRsh;

    uint32_t newSeriesFine = oldSeriesFine;
    // Steps 0..~13 all want Rs within a couple of percent of each other
    // (the series cell barely moves while the shunt does the work). Once
    // one of them has been trimmed against a live measurement, the rest
    // are the SAME physical operating point -- reuse that result instead
    // of re-measuring a slow, lag-affected value 14 times over.
    if (targetRs >= settledFrom) {
      newSeriesFine = oldSeriesFine;
    } else if (lastSeriesTarget > 0.0f &&
        fabsf(targetRs - lastSeriesTarget) / lastSeriesTarget < 0.02f) {
      newSeriesFine = lastSeriesFine;
    } else {
      newSeriesFine = trimFine(true, targetRs, oldSeriesFine, 6, 0.015f);
      lastSeriesTarget = targetRs;
      lastSeriesFine = newSeriesFine;
    }
    // The shunt reading below needs the series cell at THIS step's
    // operating point, not wherever the last probe happened to leave it.
    driver_.setDutyFine(seriesCh_, newSeriesFine);

    uint32_t newShuntFine = trimFine(false, targetRsh, oldShuntFine, 6, 0.015f);

    cal_.setLutStepDuty(i, newSeriesFine, newShuntFine);

    out.print(F("  step "));
    out.print(i);
    // Duties print in counts with the dithered fraction (e.g. 581.63).
    const float L = (float)DriverChannels::FINE_PER_COUNT;
    out.print(F(": seriesDuty "));
    out.print(oldSeriesFine / L, 2);
    out.print(F("->"));
    out.print(newSeriesFine / L, 2);
    out.print(F("  shuntDuty "));
    out.print(oldShuntFine / L, 2);
    out.print(F("->"));
    out.print(newShuntFine / L, 2);
    out.println(targetRs >= settledFrom ? F("  (series from the settled curve)") : F(""));
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
      savedSeriesFine_ = lut().step(currentStep_).seriesFine;
      savedShuntFine_ = lut().step(currentStep_).shuntFine;
    }
    return;
  }
  if (currentOk) {
    setStep(currentStep_);
  } else {
    applyDefinedStartupState();
  }
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
    // calibration -- see DualCalibration::calibrate().
    Serial.print(F("No valid saved calibration ("));
    Serial.print(calPath_);
    Serial.println(F(") -- will calibrate once both channels have loaded."));
  }

  applyDefinedStartupState();
}

void LDRVolume::applyDefinedStartupState() {
  // Neither calLoad() nor a calibration actually drives any step's
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

bool LDRVolume::driveLutStep(uint8_t i) {
  if (!hasLut() || i >= numSteps() || !lut().step(i).valid) return false;
  driver_.setDutyFine(seriesCh_, lut().step(i).seriesFine);
  driver_.setDutyFine(shuntCh_, lut().step(i).shuntFine);
  return true;
}

void LDRVolume::driveElementFine(bool series, uint32_t fine) {
  driver_.setDutyFine(series ? seriesCh_ : shuntCh_, fine);
}

int8_t LDRVolume::lowestValidStep() const {
  for (uint8_t i = 0; i < numSteps(); i++) if (lut().step(i).valid) return (int8_t)i;
  return -1;
}

int8_t LDRVolume::highestValidStep() const {
  for (int i = (int)numSteps() - 1; i >= 0; i--) if (lut().step((uint8_t)i).valid) return (int8_t)i;
  return -1;
}

void LDRVolume::releaseRelayToAudio() {
  reapplyCurrentStepOrDefault();
  BusyHook::wait(CAL_RELAX_BEFORE_RECONNECT_MS);
  relayEnergize(false);
}

void LDRVolume::calForget() {
  profile_.clear();
  fingerprint_ = CellFingerprint{};
  meta_ = CalMeta{};
  rAuto_ = true;
  rMinOhms_ = rMaxOhms_ = 0.0f;
  CalStorage::erase(calPath_);
  calReset();
}

void LDRVolume::calReset() {
  cal_.resetCurves();
  currentStep_ = 0;
  releaseRelayToAudio(); // no usable LUT left -> the LDR mute, never leftover duties
}

void LDRVolume::applyDuties(uint32_t seriesFine, uint32_t shuntFine) {
  if (muted_) {
    savedSeriesFine_ = seriesFine;
    savedShuntFine_ = shuntFine;
  } else {
    driver_.setDutyFine(seriesCh_, seriesFine);
    driver_.setDutyFine(shuntCh_, shuntFine);
  }
}

bool LDRVolume::setStep(uint8_t step) {
  if (!hasLut() || step >= numSteps()) return false;
  const VolumeLut::Entry &e = lut().step(step);
  if (!e.valid) return false;
  applyDuties(e.seriesFine, e.shuntFine);
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

uint8_t LDRVolume::calSolveStereo(float capDb, uint8_t steps, Stream &out) {
  rangeCapDb_ = capDb;
  uint8_t valid = calSolve(steps, out);
  rangeCapDb_ = 0.0f; // never leave a cap behind for some later, unrelated solve
  return valid;
}

void LDRVolume::mute() {
  if (muted_) return;
  savedSeriesFine_ = driver_.getDutyFine(seriesCh_);
  savedShuntFine_ = driver_.getDutyFine(shuntCh_);
  muted_ = true;
  driver_.setDuty(seriesCh_, 0);
  driver_.setDuty(shuntCh_, DriverChannels::WRAP);
}

void LDRVolume::unmute() {
  if (!muted_) return;
  muted_ = false;
  driver_.setDutyFine(seriesCh_, savedSeriesFine_);
  driver_.setDutyFine(shuntCh_, savedShuntFine_);
}
