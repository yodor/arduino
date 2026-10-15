#pragma once
// 3.3 V rail compensation. The LED drive is the filtered PWM: duty x rail. A calibration's
// duties are therefore "in units of" the rail its curves were measured at -- the REFERENCE,
// stored at each full characterization (or, if none exists yet, at the next calibration of
// any kind). While listening, the driver writes duty x (reference / rail now): the same drive
// the calibration measured. Resyncs run corrected and keep the reference (their trims stay in
// the same units). The rail is re-measured every few seconds when idle; never mid-calibration.
#include <Arduino.h>
#include <math.h>

class DriverChannels;

namespace RailComp {

// ---- pure arithmetic (host-tested) ----
constexpr float MAX_CORRECTION = 0.03f;   // never correct by more than 3 %: a reading that
                                          // implies more is treated as implausible
constexpr float DEADBAND = 0.0002f;       // 0.02 %: ~0.2 counts at a 1000-count knee
inline float scaleFor(float refVolts, float nowVolts) {
  if (!(refVolts > 0.0f) || !(nowVolts > 0.0f)) return 1.0f;
  const float s = refVolts / nowVolts;
  return (s > 1.0f + MAX_CORRECTION || s < 1.0f - MAX_CORRECTION) ? 1.0f : s;
}
inline bool changed(float current, float next) { return fabsf(next - current) > DEADBAND; }

// ---- runtime ----
void begin(DriverChannels &driver);       // after LittleFS + the ADC are up
void tick();                              // from the idle main loop
void beginFullCharacterization();         // correction off for the sweeps; rail now = pending reference
void onCalibrated(bool full);             // store the reference (full, or the first ever)
void endFullCharacterization();           // however it ended: correction back on (no-op after success)
void report(Stream &out);                 // one line: reference, now, the correction
float referenceVolts();

} // namespace RailComp
