#pragma once
// The results store: what the measuring code found, kept in RAM so REPORT can print a compact
// summary afterwards (instead of sending the full VERIFY / calibration logs). One global set,
// shared by both serial links. ms = millis() when recorded, 0 = nothing recorded since boot.
#include <stdint.h>
#include <math.h>
#include "DarkProfile.hpp"

namespace Results {

constexpr uint8_t MAXS = 64;          // volume steps: VolumeLut::MAX_STEPS (checked where used)
constexpr uint16_t CREEP_A = 121;     // phase A seconds kept (0..120)
constexpr uint16_t CREEP_B = 31;      // phase B seconds kept (0..30)

// VERIFY's verify: measured level per step, volume up and down; up pass: live/target ratios.
struct Verify {
  uint32_t ms = 0;
  uint8_t n = 0;
  float target[MAXS];
  float up[2][MAXS], dn[2][MAXS];     // dB, NAN = not measured
  float upSer[2][MAXS], upShu[2][MAXS];
};
// VERIFY's creep: the series cell's live/target ratio once a second.
//   phase A: loudest -> quietest step (the cell darkens), phase B: quietest -> middle (brightens)
struct Creep {
  uint32_t ms = 0;
  uint16_t nA = 0, nB = 0;
  float a[2][CREEP_A], b[2][CREEP_B];
};
// The last dark-end walk (a full characterization's or VERIFY's): points and memory peaks.
struct Walk {
  uint32_t ms = 0;
  bool fromVerify = false;
  DarkProfile p[2];
  float peak[2][DarkProfile::MAX_POINTS];   // memory peak (0 = not measured)
  uint8_t peakS[2][DarkProfile::MAX_POINTS];
  // VERIFY only: compared with the stored walk
  float shift[2] = {NAN, NAN};
  float ratioMin[2] = {NAN, NAN}, ratioMax[2] = {NAN, NAN};
  float moved[2] = {NAN, NAN}, movedSpread[2] = {NAN, NAN};   // cells vs the stored walk (effective counts)
  float compOff[2] = {NAN, NAN};                              // compensation offset while it measured
};
// The last cell check (power-up, CAL, VERIFY).
struct Cells {
  uint32_t ms = 0;
  float worst[2] = {0, 0}, tol[2] = {0, 0};
  uint8_t nOff[2] = {0, 0};
  bool changed[2] = {false, false};
};
// The last calibration (full, resync, re-solve).
struct Cal {
  uint32_t ms = 0;
  uint8_t kind = 0;                   // CalKind
  uint32_t durS = 0;
  float shift[2] = {NAN, NAN};        // quiet-point seek, counts (NAN = did not converge)
  float matchDb = NAN;                // R minus L at the quiet point
  uint16_t holdS = 0;
  float refineMin[2] = {NAN, NAN}, refineMax[2] = {NAN, NAN};
  uint8_t refineN[2] = {0, 0};
  uint8_t guardShunt[2] = {0, 0}, guardSeries[2] = {0, 0};
};

extern Verify verify;
extern Creep creep;
extern Walk walk;
extern Cells cells;
extern Cal cal;

// ---- pure summaries (host-tested) ----
// A creep log condensed: the first second the ratio is within +-band of 1 and stays there for
// 3 readings (t95, -1 = never), the largest deviation after that (peak, at peakT), and the last
// reading (end). Unreadable seconds are NAN and skipped.
struct CreepSummary { int16_t t95 = -1; float peak = NAN; int16_t peakT = -1; float end = NAN; };
inline CreepSummary summarizeCreep(const float *r, uint16_t n, float band = 0.05f) {
  CreepSummary s;
  for (uint16_t k = 0; k + 2 < n && s.t95 < 0; k++) {
    bool in = true;
    for (uint16_t j = k; j < k + 3; j++) if (isnan(r[j]) || fabsf(r[j] - 1.0f) > band) in = false;
    if (in) s.t95 = (int16_t)k;
  }
  for (uint16_t k = (s.t95 >= 0 ? (uint16_t)s.t95 : 0); k < n; k++) {
    if (isnan(r[k])) continue;
    if (isnan(s.peak) || fabsf(r[k] - 1.0f) > fabsf(s.peak - 1.0f)) { s.peak = r[k]; s.peakT = (int16_t)k; }
  }
  for (int32_t k = (int32_t)n - 1; k >= 0; k--) if (!isnan(r[k])) { s.end = r[k]; break; }
  return s;
}
// Errors of measured levels against targets: rms and worst |error| over the measured steps.
inline void errorStats(const float *meas, const float *target, uint8_t n, float &rms, float &worst, uint8_t &count) {
  double sq = 0; worst = 0; count = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (isnan(meas[i]) || isnan(target[i])) continue;
    const float e = meas[i] - target[i];
    sq += (double)e * e; if (fabsf(e) > worst) worst = fabsf(e); count++;
  }
  rms = count ? (float)sqrt(sq / count) : NAN;
}

} // namespace Results
