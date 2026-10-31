// Host test: verify verdict. Run all with: python3 test/run_tests.py
// A VERIFY run's verdict (OK / RESYNC / CHECK) and the resync hint's WHY=VERIFY.
#include <cstdio>
#include <cmath>
#include "VerifyAnalysis.hpp"
#include "ThermalComp.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
using namespace VerifyAnalysis;
int main(){
  auto v = [](float rl, float rr, float sl, float sr, float ml, float mr) {
    const float res[2] = {rl, rr}, sp[2] = {sl, sr}, mid[2] = {ml, mr}; return verdict(res, sp, mid); };
  // your REPORT: residual -10 / -11 counts, uniform (spread 1.0 / 1.2), middle steps ~ -8 dB
  CHECK(v(-10.0f, -11.0f, 1.0f, 1.2f, -8.0f, -8.1f) == Verdict::RESYNC);
  CHECK(v(0.4f, -0.3f, 0.8f, 1.1f, 0.2f, -0.1f) == Verdict::OK);                 // a table that fits
  CHECK(v(1.0f, 1.2f, 0.5f, 0.6f, -1.4f, -1.3f) == Verdict::RESYNC);             // only the middle steps off
  CHECK(v(-6.0f, 0.5f, 1.0f, 1.0f, -3.0f, 0.1f) == Verdict::CHECK);              // L and R differ by 6.5 counts
  CHECK(v(0.5f, 0.4f, 4.0f, 1.0f, 0.3f, 0.2f) == Verdict::CHECK);                // L changed shape
  CHECK(v(NAN, NAN, NAN, NAN, 0.3f, NAN) == Verdict::OK);                        // nothing measured: no alarm
  // Todor's REPORT of VERIFY #6: residual +2.0/+1.8, uniform, middle steps fine, quiet steps
  // (volume up, step 0 left out) about +1.69 / +1.42 dB -- without the quiet steps it was OK
  { const float res[2] = {2.0f, 1.8f}, sp[2] = {1.1f, 0.6f}, mid[2] = {-0.04f, 0.38f}, quiet[2] = {1.69f, 1.42f};
    CHECK(verdict(res, sp, mid) == Verdict::OK);
    CHECK(verdict(res, sp, mid, quiet) == Verdict::RESYNC);
    const float quietOk[2] = {0.6f, 0.5f};
    CHECK(verdict(res, sp, mid, quietOk) == Verdict::OK); }
  // the hint: VERIFY is a reason of its own, held during warm-up, nothing in standby, SEEK first
  using namespace ThermalComp;
  CHECK(resyncHint(true, 0.2f, 3.0f, false, false, false, true) == Hint::VERIFY);
  CHECK(resyncHint(true, 0.2f, 3.0f, false, false, true, true) == Hint::WARMING);
  CHECK(resyncHint(true, 0.2f, 3.0f, false, true, false, true) == Hint::NONE);
  CHECK(resyncHint(true, 0.2f, 3.0f, true, false, false, true) == Hint::SEEK);
  CHECK(resyncHint(true, 0.2f, 3.0f, false, false, false, false) == Hint::NONE);
  CHECK(hintRecommends(Hint::VERIFY) && !hintRecommends(Hint::WARMING));
  printf("  VERIFY #6 (quiet steps +1.7 dB) -> RESYNC (was OK); WHY=VERIFY held while warming, silent in standby\n");
  printf("verify verdict host test: PASS\n"); return 0; }
