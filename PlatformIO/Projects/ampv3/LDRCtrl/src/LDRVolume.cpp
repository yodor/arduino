#include "LDRVolume.hpp"
#include "AudioModel.hpp"
#include "CalProgress.hpp"
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

  const DividerMath::Load load = AudioModel::load();
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



bool LDRVolume::readElement(bool wantSeries, float &ohms) {
  float vRef, rs, rsh;
  if (!sensor_.read(vRef, rs, rsh)) return false;
  ohms = wantSeries ? rs : rsh;
  return true;
}

namespace {
// Lockstep secant searches: each active channel probes its own duty, then all of them are
// polled in ONE loop until each reading has converged (two consecutive within 1 %) or
// 300 ms passed, and each search is fed its own reading. Repeats until every search has
// finished. Same rule, tolerance and probe limit per channel as the sequential trim had --
// only the waiting is shared.
constexpr uint8_t  TRIM_MAX_PROBES  = 6;
constexpr float    TRIM_TOLERANCE   = 0.015f;
constexpr uint32_t CONVERGE_MAX_MS  = 300;
constexpr uint32_t CONVERGE_POLL_MS = 15;
} // namespace

void LDRVolume::trimLoopBoth(LDRVolume *ch[], uint8_t n, Stream &out, const char *labels) {
  constexpr uint8_t MAXC = 2;
  if (n == 0) return;
  if (n > MAXC) n = MAXC;
  out.println(F("--- CAL: trim each step (both channels together) ---"));
  const float L = (float)DriverChannels::FINE_PER_COUNT;

  // Start from rest: every channel at its own first valid step, settled there before
  // anything is measured (TRIM_PRECONDITION_MS) -- once, for all channels together.
  for (uint8_t c = 0; c < n; c++) {
    LDRVolume &v = *ch[c];
    for (uint8_t i0 = 0; i0 < v.numSteps(); i0++) {
      const VolumeLut::Entry &e0 = v.lut().step(i0);
      if (!e0.valid) continue;
      v.driver_.setDutyFine(v.seriesCh_, e0.seriesFine);
      v.driver_.setDutyFine(v.shuntCh_, e0.shuntFine);
      break;
    }
  }
  out.println(F("  settling at step 0 before trimming..."));
  for (uint32_t waited = 0; waited < TRIM_PRECONDITION_MS; waited += 100) BusyHook::wait(100);

  // Per channel: the series result to reuse for neighbouring steps that want the same Rs,
  // and the depth from which the series stays as solved (the walk's settled curve).
  float lastSeriesTarget[MAXC], settledFrom[MAXC];
  uint32_t lastSeriesFine[MAXC];
  uint8_t nSteps = 0;
  for (uint8_t c = 0; c < n; c++) {
    lastSeriesTarget[c] = -1.0f; lastSeriesFine[c] = 0;
    settledFrom[c] = ch[c]->profile_.okCount() ? ch[c]->profile_.shallowestOkOhms() : 1e30f;
    if (ch[c]->numSteps() > nSteps) nSteps = ch[c]->numSteps();
  }

  // One lockstep search over the channels with want[c] set: series or shunt element.
  auto searchAll = [&](bool series, const bool want[], const float target[], const uint32_t start[],
                       uint32_t result[]) {
    TrimSearch t[MAXC];
    bool active[MAXC];
    uint8_t nActive = 0;
    for (uint8_t c = 0; c < n; c++) {
      active[c] = want[c];
      result[c] = start[c];
      if (!want[c]) continue;
      LDRVolume &v = *ch[c];
      bool extrap;
      const float slope0 = Capabilities::slopeAt(series ? v.cal_.seriesCurve() : v.cal_.shuntCurve(), target[c], extrap);
      t[c].begin(target[c], start[c], TRIM_MAX_PROBES, TRIM_TOLERANCE,
                 DriverChannels::FINE_PER_COUNT, DriverChannels::FINE_MAX, slope0);
      nActive++;
    }
    while (nActive > 0) {
      for (uint8_t c = 0; c < n; c++)
        if (active[c]) ch[c]->driver_.setDutyFine(series ? ch[c]->seriesCh_ : ch[c]->shuntCh_, t[c].current());
      float prev[MAXC], lastGood[MAXC], meas[MAXC];
      bool conv[MAXC];
      for (uint8_t c = 0; c < n; c++) { prev[c] = -1.0f; lastGood[c] = -1.0f; meas[c] = -1.0f; conv[c] = !active[c]; }
      const uint32_t t0 = millis();
      while (true) {
        bool all = true;
        for (uint8_t c = 0; c < n; c++) {
          if (conv[c]) continue;
          float r;
          if (ch[c]->readElement(series, r)) {
            lastGood[c] = r;
            if (prev[c] > 0.0f && fabsf(r - prev[c]) / prev[c] < 0.01f) { meas[c] = r; conv[c] = true; continue; }
            prev[c] = r;
          }
          all = false;
        }
        if (all) break;
        if (millis() - t0 >= CONVERGE_MAX_MS) {
          for (uint8_t c = 0; c < n; c++) if (!conv[c]) meas[c] = lastGood[c];   // -1 if never readable
          break;
        }
        BusyHook::wait(CONVERGE_POLL_MS);
      }
      for (uint8_t c = 0; c < n; c++) {
        if (!active[c]) continue;
        if (!t[c].feed(meas[c])) { active[c] = false; nActive--; result[c] = t[c].best(); }
      }
    }
  };

  for (uint8_t i = 0; i < nSteps; i++) {
    CalProgress::step(i, nSteps);
    bool use[MAXC], wantSer[MAXC], wantShu[MAXC], fromCurve[MAXC];
    float tRs[MAXC] = {0, 0}, tRsh[MAXC] = {0, 0};
    uint32_t oldSer[MAXC] = {0, 0}, oldShu[MAXC] = {0, 0}, newSer[MAXC] = {0, 0}, newShu[MAXC] = {0, 0};
    for (uint8_t c = 0; c < n; c++) {
      LDRVolume &v = *ch[c];
      use[c] = false; wantSer[c] = false; wantShu[c] = false; fromCurve[c] = false;
      if (i >= v.numSteps()) continue;
      const VolumeLut::Entry &e = v.lut().step(i);
      if (!e.valid) continue;
      if (v.lut().isTransparent(i)) {
        out.print(F("  ")); if (labels) out.print(labels[c]); out.print(F(" step ")); out.print(i);
        out.println(F(": transparent (series bright, shunt off) -- not trimmed"));
        continue;
      }
      // Capture BEFORE the LUT is updated (e is a reference into it).
      use[c] = true;
      oldSer[c] = e.seriesFine; oldShu[c] = e.shuntFine;
      tRs[c] = e.targetRs; tRsh[c] = e.targetRsh;
      newSer[c] = oldSer[c];
      if (tRs[c] >= settledFrom[c]) {
        fromCurve[c] = true;                    // on the walk's settled curve: stays as solved
      } else if (lastSeriesTarget[c] > 0.0f && fabsf(tRs[c] - lastSeriesTarget[c]) / lastSeriesTarget[c] < 0.02f) {
        newSer[c] = lastSeriesFine[c];          // the same operating point as a step already trimmed
      } else {
        wantSer[c] = true;
      }
      wantShu[c] = true;
    }
    // 1. series searches, together -- with the shunt held BRIGHT. The series cell's resistance
    //    does not depend on the shunt (two separate vactrols), but the measuring current does:
    //    with the shunt as dark as the loud steps want (~Rtotal), only a few uA flow and the
    //    readings fail or drift (deep Rtotal: unreadable). The sweeps measure the same way.
    for (uint8_t c = 0; c < n; c++)
      if (wantSer[c]) ch[c]->driver_.setDutyFine(ch[c]->shuntCh_, (uint32_t)CAL_BRIGHT_HOLD_DUTY * DriverChannels::FINE_PER_COUNT);
    uint32_t serResult[MAXC];
    searchAll(true, wantSer, tRs, oldSer, serResult);
    for (uint8_t c = 0; c < n; c++) {
      if (!use[c]) continue;
      if (wantSer[c]) { newSer[c] = serResult[c]; lastSeriesTarget[c] = tRs[c]; lastSeriesFine[c] = newSer[c]; }
      // the shunt reading needs the series at THIS step's operating point
      ch[c]->driver_.setDutyFine(ch[c]->seriesCh_, newSer[c]);
    }
    // 2. shunt searches, together
    searchAll(false, wantShu, tRsh, oldShu, newShu);
    // 3. store and report
    for (uint8_t c = 0; c < n; c++) {
      if (!use[c]) continue;
      ch[c]->cal_.setLutStepDuty(i, newSer[c], newShu[c]);
      out.print(F("  ")); if (labels) out.print(labels[c]); out.print(F(" step ")); out.print(i);
      out.print(F(": seriesDuty ")); out.print(oldSer[c] / L, 2); out.print(F("->")); out.print(newSer[c] / L, 2);
      out.print(F("  shuntDuty ")); out.print(oldShu[c] / L, 2); out.print(F("->")); out.print(newShu[c] / L, 2);
      out.println(fromCurve[c] ? F("  (series from the settled curve)") : F(""));
    }
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
  board_.setAmpMute(true); // force mute across the transition (no-op if it is already muted)
  // Tell Board a relay op is (or isn't) in flight on this channel -- on a real state change
  // only, so re-energizing an energized relay cannot leave the count stuck above zero.
  if (on != relay_.isEnergized()) board_.setRelayActive(on);

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

void LDRVolume::releaseToAudio(LDRVolume *const *ch, uint8_t n) {
  if (n == 0) return;
  Board &board = ch[0]->board_;
  const bool wasMuted = board.ampMuted();
  board.setAmpMute(true); // held across the whole hand-back (no-op if it is already muted)
  for (uint8_t c = 0; c < n; c++) ch[c]->reapplyCurrentStepOrDefault();
  BusyHook::wait(CAL_RELAX_BEFORE_RECONNECT_MS);
  for (uint8_t c = 0; c < n; c++) {
    if (ch[c]->relay_.isEnergized()) ch[c]->board_.setRelayActive(false);
    ch[c]->relay_.energize(false);
    ch[c]->sensor_.deactivate();
  }
  BusyHook::wait(RELAY_POP_SETTLE_MS);
  board.setAmpMute(wasMuted);
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
