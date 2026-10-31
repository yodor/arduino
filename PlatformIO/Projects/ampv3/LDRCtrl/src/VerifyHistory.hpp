#pragma once
// The last VERIFY runs, kept in flash (/verify_hist.bin) so they survive restarts: each run's
// conditions, step errors, cell ratios, walk, creep summary and cell check. VERIFY compares the
// newest run with the one before (VerifyAnalysis); REPORT shows both; after a restart the newest
// run is restored into Results, so REPORT, VOL INFO and HEALTH keep working.
#include <stdint.h>
#include "DarkProfile.hpp"

namespace VerifyHistory {

constexpr uint8_t KEEP = 4;          // runs kept
constexpr uint8_t STEPS = 32;        // steps stored per run

struct Record {
  uint16_t seq = 0;                  // VERIFY number, counting up
  uint16_t boot = 0;                 // power-up number it ran in
  uint32_t upS = 0;                  // uptime then (s)
  char fw[16] = {0};
  float blockC = NAN, dieC = NAN;
  float compOff[2] = {NAN, NAN};     // compensation offset (counts) while it measured
  float compK[2] = {NAN, NAN};       // learned slope then (counts/C)
  float rTotal = NAN;
  uint32_t calFull = 0, calResync = 0;   // the calibration it measured (CalMeta counts)
  uint8_t n = 0;
  uint8_t region[STEPS];             // VerifyAnalysis::QUIET / MIDDLE / LOUD
  float target[STEPS];
  float up[2][STEPS], dn[2][STEPS];  // measured level, dB (NAN = not measured)
  float upSer[2][STEPS], upShu[2][STEPS];
  DarkProfile walk[2];
  float peak[2][DarkProfile::MAX_POINTS];
  uint8_t peakS[2][DarkProfile::MAX_POINTS];
  float trackedShift[2] = {NAN, NAN};    // the table's shift since the characterization (counts)
  float ratioMin[2] = {NAN, NAN}, ratioMax[2] = {NAN, NAN};
  float moved[2] = {NAN, NAN}, movedSpread[2] = {NAN, NAN};   // cells vs the stored walk, effective counts
  int16_t t95[2][2];                 // creep [phase A/B][L/R]: s (-1 = never within 5 %)
  float creepPeak[2][2], creepEnd[2][2];
  int16_t creepPeakT[2][2];
  float cellWorst[2] = {0, 0};
  uint8_t cellOff[2] = {0, 0}, cellChanged[2] = {0, 0};
};

void begin();                        // load; call after LittleFS is mounted
uint8_t count();
const Record *latest(uint8_t back = 0);   // 0 = newest; nullptr if not there
void add(const Record &r);           // append (dropping the oldest), save
uint16_t nextSeq();
void restoreResults();               // the newest run back into Results (after a restart)

// The calibration in use (CalMeta counts): set at power-up and after every calibration, so the
// hint knows whether the newest run still describes the table.
void noteCalibration(uint32_t full, uint32_t resync);
// A stored run's figures and verdict (VerifyAnalysis::verdict).
struct Summary { float residual[2], spread[2], middleDb[2], rms[2], worstLR; uint8_t verdict; };
Summary summarize(const Record &r);
// The newest run said RESYNC and no calibration has run since (the hint's WHY=VERIFY).
bool pendingResync();

} // namespace VerifyHistory
