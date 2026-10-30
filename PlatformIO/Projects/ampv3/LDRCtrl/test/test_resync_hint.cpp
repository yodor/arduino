// Host test: resync hint. Run all with: python3 test/run_tests.py
#include <cstdio>
#include <cmath>
#include "ThermalComp.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
using namespace ThermalComp;
int main(){
  const float span=3.57f;
  struct { bool on; float dT; bool seek; Hint want; const char *what; } c[]={
    {true,  0.5f, false, Hint::NONE,   "compensation on, block 0.5 C from calibration"},
    {true,  3.0f, false, Hint::NONE,   "on, 3.0 C (inside the learned 3.57 C)"},
    {true, -5.0f, false, Hint::SPAN,   "on, -5.0 C (cold start beyond the learned span)"},
    {true, 31.0f, false, Hint::LIMIT,  "on, 31 C (beyond the range)"},
    {false, 0.8f, false, Hint::NONE,   "compensation not active, 0.8 C"},
    {false, 1.6f, false, Hint::NOCOMP, "not active, 1.6 C"},
    {true,  NAN,  false, Hint::NONE,   "NTC faulty (ENV NTC= reports it)"},
    {true,  0.2f, true,  Hint::SEEK,   "last seek did not converge"}};
  for(auto &x: c){ Hint h=resyncHint(x.on,x.dT,span,x.seek); printf("  %-50s -> RESYNC=%d WHY=%s\n",x.what,h!=Hint::NONE,hintName(h)); CHECK(h==x.want); }
  CHECK(resyncHint(true,1.4f,0.5f,false)==Hint::NONE && resyncHint(true,1.6f,0.5f,false)==Hint::SPAN);   // tiny learned span: 1.5 C floor
  printf("resync-hint host test: PASS\n"); return 0; }
