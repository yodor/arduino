#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "DriverChannels.hpp"
#include "Relay.hpp"
#include "LdrSensor.hpp"
#include "Calibration.hpp"
#include "DarkProfile.hpp"
#include "CellCheck.hpp"
#include "CalStorage.hpp"
#include "Board.hpp"

// ============================================================================
// LDRVolume
//
// One LDR board's full personality: relay, series/shunt driver channels,
// its ADS1115 (via an owned LdrSensor), characterization curves + solved
// LUT (via an owned CalibrationSession), and the volume-control layer that
// uses the LUT during playback.
//
// relayEnergize() is the single point where relay state and I2C bus state
// are tied together: energizing also activates the I2C bus (the sense
// nodes are only meaningfully connected in this state anyway), and
// de-energizing fully deactivates it (Wire::end(), not just idle) so
// nothing digital is toggling near the LDR network during normal
// playback. calBegin() routes through this same method, so the bench
// RELAY ON/OFF and calibration share identical behavior.
//
// Method names track the original master-board protocol doc deliberately
// (VOL <n> -> setStep, VOL UP/DOWN -> stepUp/stepDown, MUTE ON/OFF ->
// mute/unmute) so wiring that protocol in later is a thin mapping.
// ============================================================================
class LDRVolume {
public:
  // Only used before the first full characterization has measured anything: the boot calibration
  // runs AUTO and replaces it. Never a design value.
  static constexpr float PLACEHOLDER_RTOTAL_OHMS = 50000.0f;

  // calStoragePath: this channel's fixed flash path for its saved
  // calibration (e.g. "/cal_left.bin"). Must be a string literal or other
  // storage with program lifetime -- LDRVolume just keeps the pointer.
  // rTotalOhms: the placeholder Rtotal until a calibration sets the real one
  // (PLACEHOLDER_RTOTAL_OHMS). The
  // attenuation range is not a parameter at all: it is always computed from
  // the measured floors (see computeMaxRangeDb()).
  LDRVolume(DriverChannels &driver, Board &board, uint8_t relayPin,
            DriverChannels::Channel seriesCh, DriverChannels::Channel shuntCh,
            TwoWire &i2cBus, uint8_t sdaPin, uint8_t sclPin,
            const char *calStoragePath, float rRefOhms, float rTotalOhms)
      : driver_(driver), board_(board), seriesCh_(seriesCh), shuntCh_(shuntCh),
        relay_(relayPin), sensor_(i2cBus, sdaPin, sclPin, 0x48, rRefOhms),
        calPath_(calStoragePath), rTotalOhms_(rTotalOhms) {}

  // Configures the relay pin (de-energized/audio mode, i2c bus left
  // inactive), then tries to load a saved calibration from flash
  // (LittleFS.begin() must already have run -- Board::begin() does this).
  // Whether to use it, check the cells or characterize afresh is decided
  // afterwards by DualCalibration::calibrate() (called from setup()).
  void begin();

  // Scans the current LUT for the lowest valid step and applies it, or
  // falls back to mute() if none exist (or no LUT at all) -- unmuting
  // first so the write can never be silently swallowed by a stray
  // muted_ state. Called at the end of begin(), AND at the end of any
  // calibration run (DualCalibration)
  // regardless of whether it was triggered at boot or mid-session: a
  // fresh characterization leaves raw duty at its own last sweep point,
  // not any calibrated step's real target, and currentStep_ does not
  // get reset to reflect that mismatch on its own.
  void applyDefinedStartupState();

  // --- Raw relay + i2c control ----------------------------------------
  // true = energize (calibration mode) + activate the i2c bus.
  // false = de-energize (audio mode) + fully deactivate the i2c bus.
  // Brackets the actual relay transition with a forced amp mute (held for
  // RELAY_POP_SETTLE_MS) regardless of direction -- the JFET buffer's DC
  // bias otherwise pops through the relay contacts on either transition.
  // Restores whatever amp-mute state was in effect before the call, so
  // this never forces an unmute if the amp was already (separately)
  // muted for some other reason.
  void relayEnergize(bool on);
  // The ONLY safe way to hand the audio path back after anything that drove raw
  // duties (a calibration, a diagnostic, a bench RELAY ON + SET...): put this channel back
  // on its current volume step (or the LDR mute if there is no usable LUT) while
  // the relay still has the audio disconnected, give the cells
  // CAL_RELAX_BEFORE_RECONNECT_MS to settle, then release the relay. Releasing the
  // relay with leftover diagnostic duties can connect the audio near 0 dB.
  void releaseRelayToAudio();


  // Calibration / diagnostic helpers. The CALLER energizes the relay (relayEnergize(true)) and must
  // hand the audio back with releaseRelayToAudio() afterwards.
  bool driveLutStep(uint8_t i);   // drive step i's duties now (fine units); false if not a valid step
  void driveElementFine(bool series, uint32_t fine); // one element only, fine units
  int8_t lowestValidStep() const;  // -1 if none
  int8_t highestValidStep() const; // -1 if none
  bool relayEnergized() const { return relay_.isEnergized(); }

  // Bench diagnostic: briefly activates the bus, scans for ACKing
  // addresses, deactivates again. Independent of relay state.

  // --- Calibration --------------------------------------------------------
  // Attenuation law: Rs+Rsh is held at a fixed sum (rTotalOhms()) at every step and
  // both curves are looked up per step. (A FIXED_SERIES mode that held the series
  // cell still and moved only the shunt used to exist; it was removed -- one law is
  // far simpler to calibrate, trim, store and document.)

  void calBegin();                       // relayEnergize(true), clear curves
  void calReset();                       // clear curves + LUT, reset step, then releaseRelayToAudio()
  // Forget everything about these cells: memory AND flash (the next CAL or boot runs a
  // full characterization), Rtotal back to AUTO.
  void calForget();
  bool calFeedSeriesPoint(uint16_t duty, float ohms) { return cal_.feedSeriesPoint(duty, ohms); }
  bool calFeedShuntPoint(uint16_t duty, float ohms) { return cal_.feedShuntPoint(duty, ohms); }

  // Runs VolumeLut::solve() at this channel's rTotalOhms() and a range COMPUTED from the
  // measured floors (optionally capped, see calSolveStereo()). Returns the
  // number of valid steps; 0 with a message if the range can't be computed.
  // out: where progress/errors print -- pass whichever Stream actually invoked this (the calling
  // SerialConsole's own stream), so output lands wherever the command
  // came from rather than always on USB. Defaults to the USB Serial for
  // boot-time auto-characterization, which has no calling console.
  uint8_t calSolve(uint8_t steps, Stream &out = Serial);

  // calSolve(), but with the computed range additionally capped at capDb
  // (ignored if capDb <= 0). This is how two channels with
  // different cell floors end up with the SAME dB at every step: each
  // channel's AUTO range alone would be set by its own floor (R's was
  // 3.2 dB deeper than L's), so the pair uses the smaller of the two.
  // See DualCalibration::solveBoth().
  uint8_t calSolveStereo(float capDb, uint8_t steps = NUM_VOLUME_STEPS_DEFAULT, Stream &out = Serial);
  const LdrCurve &seriesCurve() const { return cal_.seriesCurve(); }
  const LdrCurve &shuntCurve() const { return cal_.shuntCurve(); }
  const VolumeLut &lut() const { return cal_.lut(); }

  // Direct ADS1115 read (Vref/Rs/Rsh). Only valid while relayEnergized()
  // -- callers must check that themselves; this does not auto-energize.
  bool adcRead(float &vRefOut, float &rsOut, float &rshOut) {
    return sensor_.read(vRefOut, rsOut, rshOut);
  }

  // Cheap I2C-address-ACK check, no conversion started. Used by
  // DualCalibration to decide whether a channel can participate in an
  // interleaved sweep before touching its relay at all.
  // Probes the ADS1115: briefly ACTIVATES the I2C bus and then DE-INITS it. Only
  // call it at idle, as part of starting a calibration or answering a CAL
  // command -- never from anything that can run during a calibration (it would
  // shut the bus under the sweep), and not as a periodic poll (the bus is kept
  // inactive during audio on purpose). The result is remembered; see below.
  bool adsPresent() {
    adsSeen_ = sensor_.isPresent() ? 1 : 0;
    return adsSeen_ == 1;
  }
  // What the most recent adsPresent() probe found, WITHOUT touching the bus:
  // 1 present, 0 absent, -1 never probed (e.g. a saved calibration was loaded
  // at boot and nothing has needed the sensor since). STATUS uses this.
  int8_t adsLastSeen() const { return adsSeen_; }

  // Rs+Rsh target the LUT is solved for. Changing it only affects future solves.
  // Changing this invalidates any already-solved LUT (it was computed
  // against the old value) -- curves are unaffected and don't need
  // re-characterizing.
  // Rtotal. `manual` = chosen by someone (CAL R=<ohms>); false = chosen by the
  // calibration itself (AUTO). setRAuto() asks the NEXT full calibration to choose.
  void setRTotalOhms(float ohms, bool manual = true) { rTotalOhms_ = ohms; rAuto_ = !manual; cal_.invalidateLut(); }
  float rTotalOhms() const { return rTotalOhms_; }
  void setRAuto() { rAuto_ = true; }
  bool rAuto() const { return rAuto_; }
  // The Rtotal range the last full calibration measured on this hardware (0/0 = unknown).
  void setRange(float minOhms, float maxOhms) { rMinOhms_ = minOhms; rMaxOhms_ = maxOhms; }
  float rangeMinOhms() const { return rMinOhms_; }
  float rangeMaxOhms() const { return rMaxOhms_; }
  bool rangeKnown() const { return rMaxOhms_ > 0.0f && rMaxOhms_ >= rMinOhms_; }

  // The series cell's measured dark end (DarkProfile.hpp), set by a full characterization and saved
  // with the calibration. adoptDarkProfile() also replaces the sweep's lag-biased dark
  // end of the series curve with these settled points (the LUT is solved after).
  // Cell fingerprint and calibration history (CellCheck.hpp), saved with the calibration.
  const CellFingerprint &fingerprint() const { return fingerprint_; }
  void setFingerprint(const CellFingerprint &f) { fingerprint_ = f; }
  const CalMeta &calMeta() const { return meta_; }
  void setCalMeta(const CalMeta &m) { meta_ = m; }
  const DarkProfile &darkProfile() const { return profile_; }
  void setDarkProfile(const DarkProfile &p) { profile_ = p; }
  bool adoptDarkProfile(const DarkProfile &p) { profile_ = p; return cal_.replaceSeriesDarkEnd(p); }
  void refreshTransparentTop() { cal_.refreshTransparentTop(); }
  // See VolumeLut::applyQuietSeek; the settled part starts at this cell's shallowest
  // settled dark-end point.
  uint8_t applyQuietSeek(uint32_t oldFine, uint32_t newFine) {
    const float from = profile_.okCount() ? profile_.shallowestOkOhms() : 1e30f;
    return cal_.applyQuietSeek(oldFine, newFine, from);
  }
  Board &board() const { return board_; } // the shared Board (calibration mute hold lives there)

  // The attenuation depth (dB from step 0 up to the notional 0 dB top step)
  // the CURRENT LUT was solved with, read back from the LUT itself (step 0's
  // target is -range). 0 if there is no LUT. Report-only: the range is never
  // set by anyone -- every solve computes it from the measured floors.
  float rangeDb() const { return hasLut() ? -lut().step(0).targetDb : 0.0f; }

  // K_min = gain with the shunt at its floor, K_max = gain with the series at its
  // floor (both through the LOADED divider, Rs+Rsh = rTotalOhms_),
  // range = 20*log10(K_max/K_min) - marginDb. Floors are each curve's
  // highest-duty (lowest-resistance) characterized point. Returns 0 if
  // curves are empty or the numbers don't make physical sense (e.g.
  // rTotalOhms_ too small relative to a floor).
  float computeMaxRangeDb(float marginDb) const;


  // The trim loop on its own: ASSUMES the relay is already energized and
  // never touches it. Updates the LUT in RAM only (no save); DualCalibration
  // (characterize / touchUp) handles the relay and the save.
  void trimLoop(Stream &out);

  // Puts this channel's PWM on its current step (or on the lowest valid
  // step if the current one isn't valid) -- or, if the LDR mute is engaged,
  // re-asserts the mute's static duties and refreshes what unmute() will
  // restore. Call it while the relay is still energized, so the audio path
  // reconnects to the real operating point rather than to whatever the
  // last calibration probe left behind.
  void reapplyCurrentStepOrDefault();



  bool calSave() { return CalStorage::save(calPath_, *this); }
  bool calLoad() {
    bool ok = CalStorage::load(calPath_, *this);
    if (ok) currentStep_ = 0;
    return ok;
  }

  // Used only by CalStorage::load -- see CalibrationSession::adoptLoaded.
  void calAdoptLoaded(const LdrCurve &series, const LdrCurve &shunt, const VolumeLut &lut) {
    cal_.adoptLoaded(series, shunt, lut);
  }

  // --- Volume control (consumes the solved LUT) ----------------------------
  // hasLut() only means the LUT ARRAY was populated (numSteps() entries
  // exist) -- it says nothing about whether any of them actually solved.
  // A degenerate calibration (e.g. no ADC data during the sweep) can
  // still produce a full-size LUT with every single entry marked
  // OUT OF RANGE -- confirmed on real hardware. hasLut() alone is NOT
  // sufficient to decide "did this calibration actually succeed" --
  // use hasUsableLut() for that; hasLut() remains for callers that
  // genuinely only care whether the array exists (e.g. before indexing
  // into it at all).
  bool hasLut() const { return lut().numSteps() > 0; }
  bool hasUsableLut() const {
    uint8_t n = lut().numSteps();
    for (uint8_t i = 0; i < n; i++) {
      if (lut().step(i).valid) return true;
    }
    return false;
  }
  uint8_t numSteps() const { return lut().numSteps(); }
  uint8_t currentStep() const { return currentStep_; }

  bool setStep(uint8_t step);
  bool stepUp();
  bool stepDown();

  // --- Mute -----------------------------------------------------------------
  void mute();
  void unmute();
  bool isMuted() const { return muted_; }

private:
  DriverChannels &driver_;
  Board &board_;
  DriverChannels::Channel seriesCh_;
  DriverChannels::Channel shuntCh_;
  Relay relay_;
  LdrSensor sensor_;
  CalibrationSession cal_;
  const char *calPath_;
  float rTotalOhms_;
  bool rAuto_ = true;          // fresh board: the first calibration chooses Rtotal itself
  DarkProfile profile_;        // measured dark end of the series cell (empty until characterized)
  CellFingerprint fingerprint_{}; // bright-end fingerprint of both cells (valid=0 until measured)
  CalMeta meta_{};             // calibration history
  float rMinOhms_ = 0.0f;      // measured range (0/0 = never measured)
  float rMaxOhms_ = 0.0f;
  float rangeCapDb_ = 0.0f; // >0 only while calSolveStereo() is running -- see above
  int8_t adsSeen_ = -1;        // last adsPresent() result: 1 / 0 / -1 = never probed

  uint8_t currentStep_ = 0;
  bool muted_ = false;
  uint32_t savedSeriesFine_ = 0;   // FINE units (see DriverChannels::FINE_PER_COUNT)
  uint32_t savedShuntFine_ = 0;

  void applyDuties(uint32_t seriesFine, uint32_t shuntFine);   // FINE units

  // Polls adcRead() until two consecutive readings of the channel being
  // swept (series if wantSeries, else shunt) agree within 1%, or
  // timeoutMs elapses. Returns the last reading (>0), or -1.0f if no
  // valid reading was ever obtained. elapsedOut reports how long it took.
  float convergeRead(bool wantSeries, unsigned long timeoutMs, unsigned long &elapsedOut);

  // One resistor's verify+trim: a secant search on LIVE measurements --
  // it never consults the characterized curve (a sparse curve can be
  // wildly wrong between its points, and trusting it is exactly how a
  // nudge once overshot by ~100 counts). Probes dutyGuess, then uses the
  // measured slope between successive probes to aim at the target,
  // stopping within toleranceFrac or after maxProbes. Always returns the
  // BEST duty seen, never merely the last one tried (dutyGuess if
  // nothing was ever readable).
  // One step's trim: secant search in FINE units (TrimSearch.hpp) seeded with the
  // curve's own local slope, driving the PWM between counts via the dither engine.
  uint32_t trimFine(bool wantSeries, float targetOhms, uint32_t guessFine,
                    uint8_t maxProbes, float toleranceFrac);
};
