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
constexpr float MAX_COUNTS = 15.0f;       // safety bound on the offset
constexpr float DEADBAND_COUNTS = 0.05f;  // re-apply only when the offset moves this much
inline float offsetCounts(float k, float tNow, float tRef) {
  if (isnan(k) || isnan(tNow) || isnan(tRef)) return 0.0f;
  float o = k * (tNow - tRef);
  if (o > MAX_COUNTS) o = MAX_COUNTS;
  if (o < -MAX_COUNTS) o = -MAX_COUNTS;
  return o;
}
inline bool trusted(bool usable, float r2) { return usable && !isnan(r2) && r2 >= MIN_R2; }

// ---- runtime ----
void begin(DriverChannels &driver);   // after LittleFS, RailComp
void tick();                          // from the idle main loop
void suspend();                       // a calibration starts: offsets 0, frozen
void onCalibrated();                  // refit from the log, new reference, resume
void resumeIfSuspended();             // however a calibration ended (no-op after onCalibrated)
void setEnabled(bool on);             // COMP ON|OFF (not stored: boots ON)
bool enabled();
void report(Stream &out);
// For the master's ENV query.
float blockAverage();                 // the block's 1-minute average, C
float blockAtCalibration();           // the reference, C
bool sideActive(uint8_t side);        // 0 = L, 1 = R: a trusted fit is being applied
float sideOffsetCounts(uint8_t side); // the offset in the driver now
float sideSlope(uint8_t side);        // counts per C (NAN if no fit)

} // namespace ThermalComp
