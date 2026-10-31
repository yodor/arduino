// Host test: report summaries. Run all with: python3 test/run_tests.py
// REPORT condenses DIAG ALL's creep (one reading per second) into t95 / peak / end, and the
// verify into rms / worst errors. Checked on the creep phase A of a real DIAG ALL.
#include <cstdio>
#include <cmath>
#include "Results.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
using namespace Results;
int main(){
  // L series, phase A (loudest -> quietest), from the DIAG ALL at 844.5k: 0 s unreadable, then
  // rising to ~x0.65 and drifting to x0.577 at 90 s -- it never comes within 5 % of target.
  float L[91]; L[0] = NAN;
  const float first[12] = {0.297f,0.475f,0.557f,0.603f,0.625f,0.638f,0.644f,0.653f,0.653f,0.654f,0.648f,0.655f};
  for (int k = 1; k <= 12; k++) L[k] = first[k-1];
  for (int k = 13; k <= 90; k++) L[k] = 0.653f - (0.653f - 0.577f) * (k - 12) / 78.0f;
  CreepSummary s = summarizeCreep(L, 91);
  printf("  real L phase A: t95 %d, peak x%.3f at %d s, end x%.3f\n", s.t95, s.peak, s.peakT, s.end);
  CHECK(s.t95 == -1 && fabsf(s.peak - 0.297f) < 1e-4f && s.peakT == 1 && fabsf(s.end - 0.577f) < 1e-3f);
  // a cell that settles: x0.5 -> within 5 % from 6 s, overshoots to x1.04 at 9 s, ends x1.01
  float S[31]; for (int k = 0; k < 31; k++) S[k] = k < 6 ? 0.5f + 0.08f * k : (k == 9 ? 1.04f : 1.01f);
  s = summarizeCreep(S, 31);
  printf("  settling cell:  t95 %d, peak x%.3f at %d s, end x%.3f\n", s.t95, s.peak, s.peakT, s.end);
  CHECK(s.t95 == 6 && fabsf(s.peak - 1.04f) < 1e-5f && s.peakT == 9 && fabsf(s.end - 1.01f) < 1e-5f);
  // a short dip inside the first second within the band does not count (needs 3 readings)
  float D[6] = {1.0f, 0.9f, 1.0f, 1.0f, 1.0f, 1.0f};
  CHECK(summarizeCreep(D, 6).t95 == 2);
  // errors: rms and worst over measured steps only
  float meas[4] = {-10.0f, NAN, -30.5f, -41.0f}, target[4] = {-10.5f, -20.0f, -30.0f, -40.0f};
  float rms, worst; uint8_t n;
  errorStats(meas, target, 4, rms, worst, n);
  CHECK(n == 3 && fabsf(worst - 1.0f) < 1e-5f && fabsf(rms - sqrtf((0.25f + 0.25f + 1.0f) / 3.0f)) < 1e-5f);
  printf("report summaries host test: PASS\n"); return 0; }
