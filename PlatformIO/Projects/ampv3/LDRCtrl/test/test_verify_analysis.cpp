// Host test: verify analysis. Run all with: python3 test/run_tests.py
// The walk shift between two VERIFY runs at equal EFFECTIVE drive (commanded duty + that run's
// compensation offset); uniform vs shape change; common vs one channel; step regions.
#include <cstdio>
#include <cmath>
#include "VerifyAnalysis.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
using namespace VerifyAnalysis;
static DarkProfile walk(const int *duty, const float *ohms, int n) {
  DarkProfile p; p.clear();
  for (int i = 0; i < n; i++) { DarkPoint d{}; d.duty = (uint16_t)duty[i]; d.ohms = ohms[i]; d.ok = 1; d.settleS = 3; p.add(d); }
  return p;
}
int main(){
  // your L walk from the characterization (2698k reach): duty -> settled ohms
  const int  dRef[] = {1016, 1014, 1004, 994, 985, 977, 970, 964};
  const float rRef[] = {22.6e3f, 25.3e3f, 48.7e3f, 104.1e3f, 229.0e3f, 513.4e3f, 1175.6e3f, 2836.4e3f};
  const DarkProfile ref = walk(dRef, rRef, 8);
  // 1) the same cells 2.1 C warmer: they need 14 counts LESS drive for the same R at every depth.
  //    Measured with compensation offset -11.8 (commanded duties 11.8 higher than effective).
  int dNow[8]; for (int i = 0; i < 8; i++) dNow[i] = dRef[i] - 14 + 12;   // effective = cmd - 11.8 ~ ref - 14
  const DarkProfile now = walk(dNow, rRef, 8);
  Shift s = walkShift(ref, 0.0f, now, -11.8f);
  printf("  uniform shift: mean %+.2f counts (%.2f..%.2f, %u points) -> %+.2f counts/C for +2.1 C\n", s.mean, s.min, s.max, s.n, s.mean / 2.1f);
  CHECK(s.n == 6 && fabsf(s.mean + 13.8f) < 0.3f && uniform(s));          // 40k..2.8M: 6 points
  // 2) ignoring the offsets would have reported the wrong shift
  Shift naive = walkShift(ref, 0.0f, now, 0.0f);
  CHECK(fabsf(naive.mean - s.mean) > 10.0f);
  // 3) shape change: the deep points moved 6 counts more than the shallow ones
  int dShape[8]; for (int i = 0; i < 8; i++) dShape[i] = dRef[i] - (i < 5 ? 2 : 8);
  Shift sh = walkShift(ref, 0.0f, walk(dShape, rRef, 8), 0.0f);
  printf("  shape change:  mean %+.2f, spread %.2f counts -> uniform: %s\n", sh.mean, sh.max - sh.min, uniform(sh) ? "yes" : "no");
  CHECK(!uniform(sh) && sh.max - sh.min > 5.0f);
  // 4) common vs one channel
  CHECK(common(s, s) && !common(s, sh));
  // 5) points outside the reference walk's range are not used
  const float rDeep[] = {5e6f, 6e6f}; const int dDeep[] = {950, 948};
  CHECK(walkShift(ref, 0, walk(dDeep, rDeep, 2), 0).n == 0);
  // regions and their mean errors
  CHECK(regionOf(1.59e6f, 1.6e6f) == QUIET && regionOf(300e3f, 1.6e6f) == MIDDLE && regionOf(5e3f, 1.6e6f) == LOUD);
  const float meas[5] = {-80.0f, -60.5f, -30.0f, -29.0f, -5.0f}, target[5] = {-81.0f, -61.0f, -31.0f, -31.0f, -5.5f};
  const uint8_t reg[5] = {QUIET, QUIET, MIDDLE, MIDDLE, LOUD};
  float m[3]; regionMeans(meas, target, reg, 5, m);
  CHECK(fabsf(m[0] - 0.75f) < 1e-4f && fabsf(m[1] - 1.5f) < 1e-4f && fabsf(m[2] - 0.5f) < 1e-4f);
  printf("verify analysis host test: PASS\n"); return 0; }
