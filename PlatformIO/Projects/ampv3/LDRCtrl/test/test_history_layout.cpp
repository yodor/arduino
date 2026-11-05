// Host test: history layout. Run all with: python3 test/run_tests.py
// VERIFY records only ever GROW at their end. A record from the previous layout must load into
// the current one intact, its new fields reading "not measured" (VerifyHistory::begin reads
// h.recSize bytes into a fresh Record). This checks that the appended fields really are last.
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cmath>
#include "VerifyHistory.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
using VerifyHistory::Record;
int main(){
  // everything before 'settled' is the previous layout
  const size_t oldSize = offsetof(Record, settled);
  CHECK(oldSize < sizeof(Record));
  CHECK(offsetof(Record, settledHoldS) > offsetof(Record, settled));
  CHECK(offsetof(Record, cellChanged) < oldSize);                  // the last old field is inside the prefix
  // write an "old" record: the prefix of a filled one
  Record full{}; full.seq = 9; full.boot = 3; full.blockC = 28.82f; full.moved[0] = -6.0f; full.cellOff[1] = 2;
  full.settled[0] = 1.5f; full.settled[1] = -0.5f; full.settledHoldS = 120;
  unsigned char disk[sizeof(Record)]; memcpy(disk, &full, oldSize);    // what the older firmware stored
  // load it the way begin() does
  Record r = Record{}; memcpy(&r, disk, oldSize);
  CHECK(r.seq == 9 && r.boot == 3 && r.blockC == 28.82f && r.moved[0] == -6.0f && r.cellOff[1] == 2);
  CHECK(std::isnan(r.settled[0]) && std::isnan(r.settled[1]) && r.settledHoldS == 0);
  // the next step: records from 2026-10-31b / 11-01a end before the settled step probes
  const size_t midSize = offsetof(Record, probeStep);
  CHECK(midSize > oldSize && midSize < sizeof(Record) && offsetof(Record, probeHoldS) > offsetof(Record, probeErr));
  { Record mid = full; unsigned char d2[sizeof(Record)]; memcpy(d2, &mid, midSize);
    Record m = Record{}; memcpy(&m, d2, midSize);
    CHECK(m.settled[0] == 1.5f && m.settledHoldS == 120);                       // its own fields intact
    CHECK(m.probeStep[0] == 0xFF && std::isnan(m.probeSer[1][2]) && std::isnan(m.probeErr[0][0]) && m.probeHoldS[2] == 0); }
  printf("  layouts %zu -> %zu -> %zu bytes: older records load intact, newer fields = not measured\n", oldSize, midSize, sizeof(Record));
  printf("history layout host test: PASS\n"); return 0; }
