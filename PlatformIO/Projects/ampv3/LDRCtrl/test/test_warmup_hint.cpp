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
  printf("warm-up hint host test: PASS\n"); return 0; }
