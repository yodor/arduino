#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "DriverChannels.hpp"
#include "Relay.hpp"
#include "LdrSensor.hpp"
#include "Calibration.hpp"
#include "CalStorage.hpp"

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
// playback. calBegin()/calEnd() route through this same method, so manual
// bench control (RELAY ON/OFF) and calibration share identical behavior.
//
// Method names track the original master-board protocol doc deliberately
// (VOL <n> -> setStep, VOL UP/DOWN -> stepUp/stepDown, MUTE ON/OFF ->
// mute/unmute) so wiring that protocol in later is a thin mapping.
// ============================================================================
class LDRVolume {
public:
  // calStoragePath: this channel's fixed flash path for its saved
  // calibration (e.g. "/cal_left.bin"). Must be a string literal or other
  // storage with program lifetime -- LDRVolume just keeps the pointer.
  // rTotalOhms/rangeDb: solve-step parameters, required (no default) so
  // the caller always states them explicitly, typically from Config.hpp.
  // Both adjustable afterwards (setRTotalOhms()/CALRTOTAL,
  // setRangeDb()/CALRANGE) without recompiling.
  LDRVolume(DriverChannels &driver, uint8_t relayPin,
            DriverChannels::Channel seriesCh, DriverChannels::Channel shuntCh,
            TwoWire &i2cBus, uint8_t sdaPin, uint8_t sclPin,
            const char *calStoragePath, float rRefOhms, float rTotalOhms, float rangeDb)
      : driver_(driver), seriesCh_(seriesCh), shuntCh_(shuntCh),
        relay_(relayPin), sensor_(i2cBus, sdaPin, sclPin, 0x48, rRefOhms),
        calPath_(calStoragePath), rTotalOhms_(rTotalOhms), rangeDb_(rangeDb) {}

  // Configures the relay pin (de-energized/audio mode, i2c bus left
  // inactive), then tries to load a saved calibration from flash
  // (LittleFS.begin() must already have run -- Board::begin() does this).
  // If none is found (or it fails its CRC check), falls back to running
  // CALAUTO FULL automatically -- which will itself auto-save on success,
  // so this is a one-time cost, not a per-boot one.
  void begin();

  // --- Raw relay + i2c control ----------------------------------------
  // true = energize (calibration mode) + activate the i2c bus.
  // false = de-energize (audio mode) + fully deactivate the i2c bus.
  void relayEnergize(bool on);
  bool relayEnergized() const { return relay_.isEnergized(); }

  // Bench diagnostic: briefly activates the bus, scans for ACKing
  // addresses, deactivates again. Independent of relay state.
  void i2cScan() { sensor_.scanBus(); }

  // --- Calibration --------------------------------------------------------
  // CONSTANT_RTOTAL: existing behavior -- Rs+Rsh held at a fixed sum
  // every step, both curves looked up per step.
  // FIXED_SERIES: series held at fixedSeriesDuty() for every step (never
  // moves during normal volume changes); only the shunt curve is looked
  // up per step. True 0dB is unreachable in this mode -- see
  // VolumeLut::solveFixedSeries. Pick a fixedSeriesDuty from a
  // low-hysteresis part of the series curve (CALSCAN) before using this.
  enum class AttenuationMode : uint8_t { CONSTANT_RTOTAL, FIXED_SERIES };
  // Changing mode invalidates any already-solved LUT -- it was solved
  // under the other mode's math.
  void setMode(AttenuationMode m) { mode_ = m; cal_.invalidateLut(); }
  AttenuationMode mode() const { return mode_; }

  // Changing this invalidates any already-solved LUT in FIXED_SERIES
  // mode (every step's series target depends on it).
  void setFixedSeriesDuty(uint16_t duty) { fixedSeriesDuty_ = duty; cal_.invalidateLut(); }
  uint16_t fixedSeriesDuty() const { return fixedSeriesDuty_; }

  void calBegin();                       // relayEnergize(true), clear curves
  void calEnd() { relayEnergize(false); } // curves/LUT survive -- resumable
  void calReset();                       // relayEnergize(false), clear curves + LUT, reset step
  void calDriveSeries(uint16_t duty) { driver_.setDuty(seriesCh_, duty); }
  void calDriveShunt(uint16_t duty) { driver_.setDuty(shuntCh_, duty); }
  bool calFeedSeriesPoint(uint16_t duty, float ohms) { return cal_.feedSeriesPoint(duty, ohms); }
  bool calFeedShuntPoint(uint16_t duty, float ohms) { return cal_.feedShuntPoint(duty, ohms); }

  // Dispatches to VolumeLut::solve() or solveFixedSeries() depending on
  // mode(). rTotalOhms is ignored in FIXED_SERIES mode.
  // out: where progress/errors print and where an abort keypress is read
  // from -- pass whichever Stream actually invoked this (the calling
  // SerialConsole's own stream), so output lands wherever the command
  // came from rather than always on USB. Defaults to the USB Serial for
  // boot-time auto-characterization, which has no calling console.
  uint8_t calSolve(uint8_t steps, float rangeDb, float rTotalOhms, Stream &out = Serial);
  CalibrationSession::State calState() const { return cal_.state(); }
  const LdrCurve &seriesCurve() const { return cal_.seriesCurve(); }
  const LdrCurve &shuntCurve() const { return cal_.shuntCurve(); }
  const VolumeLut &lut() const { return cal_.lut(); }

  // Direct ADS1115 read (Vref/Rs/Rsh). Only valid while relayEnergized()
  // -- callers must check that themselves; this does not auto-energize.
  bool adcRead(float &vRefOut, float &rsOut, float &rshOut) {
    return sensor_.read(vRefOut, rsOut, rshOut);
  }

  // See LdrSensor::setRRefOhms -- affects future measurements only.
  void setRRefOhms(float ohms) { sensor_.setRRefOhms(ohms); }
  float rRefOhms() const { return sensor_.rRefOhms(); }

  // Constant Rs+Rsh target used by CALSOLVE (when its rTotalOhms arg is
  // omitted) and by CALAUTO. Changing this only affects future solves --
  // re-run CALSOLVE or CALAUTO to apply it to the saved LUT.
  // Changing this invalidates any already-solved LUT (it was computed
  // against the old value) -- curves are unaffected and don't need
  // re-characterizing.
  void setRTotalOhms(float ohms) { rTotalOhms_ = ohms; cal_.invalidateLut(); }
  float rTotalOhms() const { return rTotalOhms_; }

  // Total dB span (quietest to loudest) used by CALSOLVE (when its
  // rangeDb arg is omitted) and by CALAUTO. MUTE is always available for
  // true silence regardless of this range.
  void setRangeDb(float db) { rangeDb_ = db; autoRange_ = false; cal_.invalidateLut(); }
  float rangeDb() const { return rangeDb_; }

  // Auto-range: when enabled, every calSolve() (manual CALSOLVE or via
  // CALAUTO) recomputes the range fresh from the currently-characterized
  // curves' own floors and the current rTotalOhms, via computeMaxRangeDb
  // -- rather than using a fixed Config.hpp/CALRANGE value. Disabled by
  // setRangeDb() (an explicit value means "use exactly this").
  void setAutoRange(bool on) { autoRange_ = on; cal_.invalidateLut(); }
  bool autoRange() const { return autoRange_; }

  // K_min = Rsh_floor/rTotalOhms_, K_max = 1 - Rs_floor/rTotalOhms_,
  // range = 20*log10(K_max/K_min) - marginDb. Floors are each curve's
  // highest-duty (lowest-resistance) characterized point. Returns 0 if
  // curves are empty or the numbers don't make physical sense (e.g.
  // rTotalOhms_ too small relative to a floor).
  float computeMaxRangeDb(float marginDb) const;

  // Automatic characterization + solve, no manual CALDRIVE/CALPOINT
  // needed. FULL: ~18 points, dense through the steep ~15-40% duty
  // region (per the shape every real sweep on this project has shown),
  // sparse above it -- the thorough sweep to store in flash. FAST: 8
  // coarser points -- a quicker re-sweep for periodic temperature-drift
  // touch-ups without redoing the full characterization.
  //
  // Each point is read via a convergence loop (poll until two
  // consecutive readings agree within 1%, bounded by a timeout) rather
  // than a fixed dwell -- and prints how long that took, which
  // doubles as real settling-time data for this specific cell/board,
  // still an open question as of the breadboard testing.
  //
  // Checks the ADS1115 actually ACKs before doing anything else -- if no
  // LDR board is attached to this channel's bus, prints a message and
  // returns immediately without touching the relay or any existing
  // curves. Otherwise energizes the relay (clears existing curves, like
  // calBegin()), sweeps, solves, saves to flash, then de-energizes the
  // relay again -- back to normal audio mode when it returns. Use RELAY
  // <side> ON + ADCREAD to verify a step afterwards if you want to.
  // Blocking -- prints progress to Serial as it goes; send any character
  // to abort between points.
  enum class CalMode : uint8_t { FULL, FAST };
  void runAutoCalibration(CalMode mode, Stream &out = Serial); // auto-saves to flash on success

  // Explicit save/load against this channel's fixed calStoragePath, for
  // manual testing from the console without needing a reboot.
  // Verify + trim pass: for every valid LUT step, measures what its
  // looked-up duty actually produces right now and nudges it toward the
  // true target using the characterized curve's own local shape (a far
  // better local model of "which way and how far" than a fixed step
  // size, even when the curve's absolute values have drifted since
  // characterization). In FIXED_SERIES mode only shunt is trimmed --
  // series stays at its one fixed duty, by design. Updates and re-saves
  // the LUT. Energizes relay for the duration, de-energizes when done.
  // Send any character to abort between steps.
  void runTrimPass(Stream &out = Serial);

  // Diagnostic-only: fine-resolution (every 4 raw duty counts) scan of
  // the critical ~22-28% duty window, both series and shunt, each in
  // BOTH directions (ascending then descending) so up-vs-down hysteresis
  // is directly visible, with 5 raw (non-converged) back-to-back samples
  // per point so the actual settling trajectory is visible too, not a
  // pre-filtered single number. Does NOT touch existing curves/LUT/saved
  // flash data at all -- purely prints to Serial. De-energizes relay
  // when done. Send any character to abort between points.
  void runDiagnosticScan(Stream &out = Serial);

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
  bool hasLut() const { return lut().numSteps() > 0; }
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
  DriverChannels::Channel seriesCh_;
  DriverChannels::Channel shuntCh_;
  Relay relay_;
  LdrSensor sensor_;
  CalibrationSession cal_;
  const char *calPath_;
  float rTotalOhms_;
  float rangeDb_;
  bool autoRange_ = false;
  AttenuationMode mode_ = AttenuationMode::CONSTANT_RTOTAL;
  uint16_t fixedSeriesDuty_ = DriverChannels::WRAP; // default: fully bright -- pick a better
                                                     // value from CALSCAN data before using
                                                     // FIXED_SERIES mode for real

  uint8_t currentStep_ = 0;
  bool muted_ = false;
  uint16_t savedSeriesDuty_ = 0;
  uint16_t savedShuntDuty_ = 0;

  void applyDuties(uint16_t seriesDuty, uint16_t shuntDuty);

  // Polls adcRead() until two consecutive readings of the channel being
  // swept (series if wantSeries, else shunt) agree within 1%, or
  // timeoutMs elapses. Returns the last reading (>0), or -1.0f if no
  // valid reading was ever obtained. elapsedOut reports how long it took.
  float convergeRead(bool wantSeries, unsigned long timeoutMs, unsigned long &elapsedOut);

  // One resistor's verify+trim: drives dutyGuess, measures, and if off
  // target by more than toleranceFrac, uses the curve's local shape to
  // propose a better duty and repeats (up to maxIterations). Returns the
  // best duty found (the original dutyGuess if measurement/lookup fails
  // at any point, rather than guessing blind).
  uint16_t trimDuty(bool wantSeries, float targetOhms, uint16_t dutyGuess,
                    uint8_t maxIterations, float toleranceFrac);
};