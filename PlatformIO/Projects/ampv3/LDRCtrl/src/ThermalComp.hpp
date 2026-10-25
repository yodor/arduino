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

// When a resync would help (protocol 14: ENV RESYNC= WHY=; TEMP/DIAG). dT = block now minus at
// the last calibration (NAN if unknown, e.g. a faulty NTC -- ENV NTC= reports that itself);
// spanC = the largest change the fit learned from; compOn = the compensation is applying offsets.
enum class Hint : uint8_t { NONE, SPAN, LIMIT, NOCOMP, SEEK };
constexpr float HINT_NOCOMP_C = 1.0f;   // moved this much with nothing correcting it
constexpr float HINT_MIN_SPAN_C = 1.5f; // below this the learned span is too small to judge by
inline Hint resyncHint(bool compOn, float dT, float spanC, bool seekFailed) {
  if (seekFailed) return Hint::SEEK;
  if (isnan(dT)) return Hint::NONE;
  const float a = fabsf(dT);
  if (a >= MAX_SPAN_C) return Hint::LIMIT;
  if (!compOn) return a > HINT_NOCOMP_C ? Hint::NOCOMP : Hint::NONE;
  const float span = spanC > HINT_MIN_SPAN_C ? spanC : HINT_MIN_SPAN_C;
  return a > span ? Hint::SPAN : Hint::NONE;
}
inline const char *hintName(Hint h) {
  return h == Hint::SPAN ? "SPAN" : h == Hint::LIMIT ? "LIMIT" : h == Hint::NOCOMP ? "NOCOMP" : h == Hint::SEEK ? "SEEK" : "-";
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
// For the master's ENV query.
float blockAverage();                 // the block's 1-minute average, C
float blockAtCalibration();           // the reference, C
bool sideActive(uint8_t side);        // 0 = L, 1 = R: a trusted fit is being applied
float sideOffsetCounts(uint8_t side); // the offset in the driver now
float sideSlope(uint8_t side);        // counts per C (NAN if no fit)
bool ntcOk();                         // the block NTC reads (not OPEN / SHORT / BAD)
Hint hint();                          // whether a resync is recommended now, and why
void printHint(Print &out);           // the same as a sentence (TEMP / DIAG)

} // namespace ThermalComp
