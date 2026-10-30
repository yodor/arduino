// Host test: warmup hint. Run all with: python3 test/run_tests.py
#include <cstdio>
#include <cmath>
#include "ThermalComp.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
using namespace ThermalComp;
int main(){
  const float span=3.57f;
  struct { bool standby, warming, seek; float dT; Hint want; const char *what; } c[]={
    {true,  false,false,-5.0f, Hint::NONE,    "standby (amp muted), block 5 C cold"},
    {false, true, false,-5.0f, Hint::WARMING, "amp on 5 min ago, block 5 C cold"},
    {false, false,false,-5.0f, Hint::SPAN,    "amp on, block steady, 5 C from calibration"},
    {false, true, false, 0.5f, Hint::NONE,    "warming, but close to the calibration temperature"},
    {false, true, true,  0.5f, Hint::SEEK,    "warming, last seek failed (never delayed)"},
    {true,  false,true,  0.5f, Hint::NONE,    "standby, last seek failed"}};
  for(auto &x: c){ Hint h=resyncHint(true,x.dT,span,x.seek,x.standby,x.warming);
    printf("  %-48s -> RESYNC=%d WHY=%s\n",x.what,hintRecommends(h),hintName(h)); CHECK(h==x.want); }
  CHECK(!hintRecommends(Hint::WARMING) && hintRecommends(Hint::SPAN));
  // the hold depends on the state (Todor's cold start: +4.5 C over ~75 min = ~0.6 C per 10 min)
  CHECK(!warmingHold(false, 0, 0.6f));            // learning: a slow warm-up is NOT held -> NOCOMP shows
  CHECK(!warmingHold(false, 0, NAN));             // learning: unknown trend is no reason either
  CHECK(warmingHold(false, 3600000UL, 1.4f));     // learning: a fast change is held
  CHECK(warmingHold(true, 3600000UL, 0.6f));      // compensation active: 0.6 C/10 min still settling
  CHECK(!warmingHold(true, 3600000UL, 0.3f));     // ... 0.3 C/10 min is steady enough now (was 0.25)
  CHECK(warmingHold(true, 5UL * 60000UL, 0.0f));  // ... and 10 min after the amp is switched on
  CHECK(!warmingHold(true, 11UL * 60000UL, 0.0f));
  // so Todor's first ENV (learning, DBLOCK +4.50, ~0.6 C/10 min) now says RESYNC=1 WHY=NOCOMP
  CHECK(resyncHint(false, 4.50f, 0.0f, false, false, warmingHold(false, 3600000UL, 0.6f)) == Hint::NOCOMP);
  printf("  learning: cold start +4.5 C at 0.6 C/10 min -> RESYNC=1 WHY=NOCOMP (was WARMING)\n");
  printf("warm-up hint host test: PASS\n"); return 0; }
