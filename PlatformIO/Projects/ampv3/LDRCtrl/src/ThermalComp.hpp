#pragma once
// Temperature compensation, stage 2: APPLY what ThermalLog learned. The knee follows the
// driver block's temperature (measured: -4.0 / -4.5 counts per C, R2 0.85 / 0.93; the cells'
// own temperature explained little). While listening, each side's duties get an offset of
// k x (block now - block at the last calibration), from the block's 1-minute average.
// Suspended during every calibration, so each resync still measures the RAW knee shift and
// the learning keeps refining itself. Only active once the fit is trustworthy.
#include <Arduino.h>
#include <math.h>

class DriverChannels;

namespace ThermalComp {

// ---- pure arithmetic (host-tested) ----
constexpr float MIN_R2 = 0.6f;            // the fit must explain most of the drift
// The range: the full correction within MAX_SPAN_C of the calibration temperature (a room at
// 18..40 C puts the block anywhere from ~18 to ~50 C: from any calibration point inside that,
// 30 C covers it). The guard against a bad fit is the slope: only physically plausible slopes
// are used (the driver's drift measured ~5.4 counts/C). Count caps of 15 and then 45 cut a cold
// start short. A faulty NTC (OPEN / SHORT / BAD) gives no offset at all.
constexpr float MAX_SPAN_C = 30.0f;
constexpr float MAX_SLOPE = 15.0f;                       // counts per C
constexpr float MAX_COUNTS = MAX_SLOPE * MAX_SPAN_C;     // the implied backstop
constexpr float DEADBAND_COUNTS = 0.05f;  // re-apply only when the offset moves this much
inline float offsetCounts(float k, float tNow, float tRef) {
  if (isnan(k) || isnan(tNow) || isnan(tRef)) return 0.0f;
  float dT = tNow - tRef;
  if (dT > MAX_SPAN_C) dT = MAX_SPAN_C;
  if (dT < -MAX_SPAN_C) dT = -MAX_SPAN_C;
  float o = k * dT;
  if (o > MAX_COUNTS) o = MAX_COUNTS;
  if (o < -MAX_COUNTS) o = -MAX_COUNTS;
  return o;
}
inline bool trusted(bool usable, float r2, float k = 0.0f) {
  return usable && !isnan(r2) && r2 >= MIN_R2 && !isnan(k) && fabsf(k) <= MAX_SLOPE;
}

// When a resync would help (protocol 14: ENV RESYNC= WHY=; TEMP/REPORT). dT = block now minus at
// the last calibration (NAN if unknown, e.g. a faulty NTC -- ENV NTC= reports that itself);
// spanC = the largest change the fit learned from; compOn = the compensation is applying offsets.
enum class Hint : uint8_t { NONE, SPAN, LIMIT, NOCOMP, SEEK, WARMING, VERIFY };
constexpr float HINT_NOCOMP_C = 1.0f;   // moved this much with nothing correcting it
constexpr float HINT_MIN_SPAN_C = 1.5f; // below this the learned span is too small to judge by
inline Hint resyncHintBase(bool compOn, float dT, float spanC) {
  if (isnan(dT)) return Hint::NONE;
  const float a = fabsf(dT);
  if (a >= MAX_SPAN_C) return Hint::LIMIT;
  if (!compOn) return a > HINT_NOCOMP_C ? Hint::NOCOMP : Hint::NONE;
  const float span = spanC > HINT_MIN_SPAN_C ? spanC : HINT_MIN_SPAN_C;
  return a > span ? Hint::SPAN : Hint::NONE;
}

// standby: the amp is muted by command (nobody listens; the temperature will change once it is
// on). warming: the amp was switched on less than HINT_WARMUP_MS ago, or the block still moves
// faster than HINT_STEADY_C per 10 min: a resync now would be out of date within minutes, so a
// hint that would apply is reported as WARMING (not a recommendation yet). SEEK is never delayed.
constexpr uint32_t HINT_WARMUP_MS = 10UL * 60UL * 1000UL;   // compensation active: after the amp is switched on
constexpr float    HINT_STEADY_C = 0.5f;                     // compensation active: per 10 minutes
constexpr float    HINT_STEADY_LEARNING_C = 1.0f;            // learning: only a fast change is held (per 10 min)

// Whether to hold a recommendation as WARMING. It depends on the state:
//  - LEARNING (no trusted compensation yet): a resync at a new temperature is exactly what the
//    learning needs, so warm-up is NOT held -- only a block moving faster than 1 C per 10 min (a
//    resync then cannot measure cleanly). The trend being unknown (first minutes) is no reason.
//  - COMPENSATION ACTIVE: the drift is being corrected, and a resync mid-warm-up would soon be out
//    of date -- held for 10 min after the amp is switched on, and while the block moves faster than
//    0.5 C per 10 min (or the trend is still unknown).
inline bool warmingHold(bool compOn, uint32_t msSinceAmpOn, float ratePer10Min) {
  if (!compOn) return !isnan(ratePer10Min) && fabsf(ratePer10Min) > HINT_STEADY_LEARNING_C;
  return msSinceAmpOn < HINT_WARMUP_MS || isnan(ratePer10Min) || fabsf(ratePer10Min) > HINT_STEADY_C;
}
// verifyResync: the newest VERIFY found the table off and no calibration ran since (a measured
// finding, but held during warm-up like the temperature reasons: a resync then would not last).
inline Hint resyncHint(bool compOn, float dT, float spanC, bool seekFailed, bool standby = false, bool warming = false,
                       bool verifyResync = false) {
  if (standby) return Hint::NONE;
  if (seekFailed) return Hint::SEEK;
  if (verifyResync) return warming ? Hint::WARMING : Hint::VERIFY;
  const Hint h = resyncHintBase(compOn, dT, spanC);
  return (h != Hint::NONE && warming) ? Hint::WARMING : h;
}
inline bool hintRecommends(Hint h) {
  return h == Hint::SPAN || h == Hint::LIMIT || h == Hint::NOCOMP || h == Hint::SEEK || h == Hint::VERIFY;
}
inline const char *hintName(Hint h) {
  return h == Hint::SPAN ? "SPAN" : h == Hint::LIMIT ? "LIMIT" : h == Hint::NOCOMP ? "NOCOMP" : h == Hint::SEEK ? "SEEK"
       : h == Hint::WARMING ? "WARMING" : h == Hint::VERIFY ? "VERIFY" : "-";
}

// ---- runtime ----
void begin(DriverChannels &driver);   // after LittleFS, RailComp
void tick();                          // from the idle main loop
void suspend();                       // a calibration starts: offsets 0, frozen
void onCalibrated();                  // refit from the log, new reference, resume
void resumeIfSuspended();             // however a calibration ended (no-op after onCalibrated)
void refresh();                       // the log or the reference changed (FORGET): refit, re-apply
void setEnabled(bool on);             // COMP ON|OFF (not stored: boots ON)
bool enabled();
void report(Stream &out);
// For the controller's ENV query.
float blockAverage();                 // the block's 1-minute average, C
float blockAtCalibration();           // the reference, C
bool sideActive(uint8_t side);        // 0 = L, 1 = R: a trusted fit is being applied
float sideOffsetCounts(uint8_t side); // the offset in the driver now
float sideSlope(uint8_t side);        // counts per C (NAN if no fit)
bool ntcOk();                         // the block NTC reads (not OPEN / SHORT / BAD)
Hint hint();                          // whether a resync is recommended now, and why
float blockRatePer10Min();            // the block's change over the last 10 min, C (NAN until known)
void printHint(Print &out);           // the same as a sentence (TEMP / REPORT)

} // namespace ThermalComp
