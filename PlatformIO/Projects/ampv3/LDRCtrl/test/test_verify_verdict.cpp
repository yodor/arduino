// Host test: verify verdict. Run all with: python3 test/run_tests.py
// A VERIFY run's verdict (OK / RESYNC / CHECK) and the resync hint's WHY=VERIFY.
#include <cstdio>
#include <cmath>
#include "VerifyAnalysis.hpp"
#include "ThermalComp.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
using namespace VerifyAnalysis;
int main(){
  auto v = [](float sl, float sr, float spl, float spr) {
    const float settled[2] = {sl, sr}, sp[2] = {spl, spr}; return verdict(settled, sp); };
  // the verdict rests on the SETTLED quiet point only
  CHECK(v(0.4f, -0.3f, 0.8f, 1.1f) == Verdict::OK);              // a table that fits
  CHECK(v(3.4f, 2.9f, 1.1f, 0.6f) == Verdict::RESYNC);           // driven 3.4 counts too bright
  CHECK(v(-3.2f, -2.0f, 0.4f, 0.3f) == Verdict::RESYNC);         // one beyond 3 counts is enough
  CHECK(v(2.6f, 2.3f, 1.0f, 1.0f) == Verdict::OK);               // within the cells' own wander
  // Todor's VERIFY #14 / #15 / #16: +2.14/+2.39, -1.58/-1.82, +0.66/+0.71 -- the cells wandering
  // +-2 counts with their light history (the same on both channels); none of them is a table error
  CHECK(v(2.14f, 2.39f, 0.8f, 0.5f) == Verdict::OK);
  CHECK(v(-1.58f, -1.82f, 0.2f, 0.3f) == Verdict::OK);
  CHECK(v(0.66f, 0.71f, 0.2f, 0.2f) == Verdict::OK);
  CHECK(v(-3.5f, 0.2f, 1.0f, 1.0f) == Verdict::CHECK);           // the two channels 3.7 counts apart
  CHECK(v(0.5f, 0.4f, 4.0f, 1.0f) == Verdict::CHECK);            // L changed shape
  // Todor's VERIFY #6 / #9 / #11 (older firmware: no settled measurement) -- their quick readings
  // swung between +1.8 and -2.8 dB with the cells' light history; they no longer raise a hint
  CHECK(v(NAN, NAN, 1.1f, 0.6f) == Verdict::OK);
  CHECK(v(NAN, NAN, 0.4f, 0.3f) == Verdict::OK);
  // the hint: VERIFY is a reason of its own, held during warm-up, nothing in standby, SEEK first
  using namespace ThermalComp;
  CHECK(resyncHint(true, 0.2f, 3.0f, false, false, false, true) == Hint::VERIFY);
  CHECK(resyncHint(true, 0.2f, 3.0f, false, false, true, true) == Hint::WARMING);
  CHECK(resyncHint(true, 0.2f, 3.0f, false, true, false, true) == Hint::NONE);
  CHECK(resyncHint(true, 0.2f, 3.0f, true, false, false, true) == Hint::SEEK);
  CHECK(resyncHint(true, 0.2f, 3.0f, false, false, false, false) == Hint::NONE);
  CHECK(hintRecommends(Hint::VERIFY) && !hintRecommends(Hint::WARMING));
  printf("  settled quiet point beyond 3 counts -> RESYNC; the cells' +-2-count wander (VERIFY #14-#16) -> OK\n");
  printf("verify verdict host test: PASS\n"); return 0; }
