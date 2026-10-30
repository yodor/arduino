#include "VerifyHistory.hpp"
#include <Arduino.h>
#include <LittleFS.h>
#include "Results.hpp"
#include "VerifyAnalysis.hpp"

namespace {
constexpr const char *PATH = "/verify_hist.bin";
struct Header { char magic[4]; uint16_t count; uint16_t recSize; };
VerifyHistory::Record gRec[VerifyHistory::KEEP];    // oldest first
uint8_t gN = 0;

void save() {
  File f = LittleFS.open(PATH, "w");
  if (!f) return;
  Header h = {{'V', 'H', 'S', '1'}, gN, (uint16_t)sizeof(VerifyHistory::Record)};
  f.write((const uint8_t *)&h, sizeof h);
  f.write((const uint8_t *)gRec, sizeof(VerifyHistory::Record) * gN);
  f.close();
}
} // namespace

namespace VerifyHistory {

void begin() {
  gN = 0;
  File f = LittleFS.open(PATH, "r");
  if (!f) return;
  Header h;
  const bool ok = f.read((uint8_t *)&h, sizeof h) == (int)sizeof h && memcmp(h.magic, "VHS1", 4) == 0 &&
                  h.recSize == sizeof(Record) && h.count <= KEEP &&
                  f.read((uint8_t *)gRec, sizeof(Record) * h.count) == (int)(sizeof(Record) * h.count);
  f.close();
  if (ok) gN = (uint8_t)h.count;     // a different layout (older firmware): history starts afresh
}

uint8_t count() { return gN; }

const Record *latest(uint8_t back) { return back < gN ? &gRec[gN - 1 - back] : nullptr; }

void add(const Record &r) {
  if (gN == KEEP) { for (uint8_t i = 1; i < KEEP; i++) gRec[i - 1] = gRec[i]; gN--; }
  gRec[gN++] = r;
  save();
}

void restoreResults() {
  const Record *r = latest();
  if (!r) return;
  Results::Verify &v = Results::verify;
  v.n = r->n; v.ms = 1;                               // "recorded", age from the record
  for (uint8_t i = 0; i < Results::MAXS; i++) {
    const bool in = i < r->n && i < STEPS;
    v.target[i] = in ? r->target[i] : NAN;
    for (uint8_t c = 0; c < 2; c++) {
      v.up[c][i] = in ? r->up[c][i] : NAN;  v.dn[c][i] = in ? r->dn[c][i] : NAN;
      v.upSer[c][i] = in ? r->upSer[c][i] : NAN; v.upShu[c][i] = in ? r->upShu[c][i] : NAN;
    }
  }
  Results::Walk &w = Results::walk;
  w.ms = 1; w.fromVerify = true;
  for (uint8_t c = 0; c < 2; c++) {
    w.p[c] = r->walk[c];
    for (uint8_t k = 0; k < DarkProfile::MAX_POINTS; k++) { w.peak[c][k] = r->peak[c][k]; w.peakS[c][k] = r->peakS[c][k]; }
    w.shift[c] = r->trackedShift[c]; w.ratioMin[c] = r->ratioMin[c]; w.ratioMax[c] = r->ratioMax[c];
    w.moved[c] = r->moved[c]; w.movedSpread[c] = r->movedSpread[c]; w.compOff[c] = r->compOff[c];
  }
}

namespace { uint32_t gCalFull = 0xFFFFFFFF, gCalResync = 0xFFFFFFFF; }
void noteCalibration(uint32_t full, uint32_t resync) { gCalFull = full; gCalResync = resync; }

Summary summarize(const Record &r) {
  Summary s;
  for (uint8_t c = 0; c < 2; c++) {
    s.residual[c] = r.trackedShift[c] + r.compOff[c] - r.moved[c];
    s.spread[c] = r.movedSpread[c];
    float reg[3]; VerifyAnalysis::regionMeans(r.up[c], r.target, r.region, r.n, reg);
    s.middleDb[c] = reg[VerifyAnalysis::MIDDLE];
    float worst; uint8_t cnt; Results::errorStats(r.up[c], r.target, r.n, s.rms[c], worst, cnt);
  }
  s.worstLR = NAN;
  for (uint8_t i = 0; i < r.n; i++) {
    if (isnan(r.up[0][i]) || isnan(r.up[1][i])) continue;
    const float d = fabsf(r.up[0][i] - r.up[1][i]);
    if (isnan(s.worstLR) || d > s.worstLR) s.worstLR = d;
  }
  s.verdict = (uint8_t)VerifyAnalysis::verdict(s.residual, s.spread, s.middleDb);
  return s;
}

bool pendingResync() {
  const Record *r = latest();
  if (!r || r->calFull != gCalFull || r->calResync != gCalResync) return false;
  return summarize(*r).verdict == (uint8_t)VerifyAnalysis::Verdict::RESYNC;
}

uint16_t nextSeq() { return gN ? (uint16_t)(gRec[gN - 1].seq + 1) : 1; }

} // namespace VerifyHistory
