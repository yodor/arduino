// Host test: walk memory. Run all with: python3 test/run_tests.py
#include <cstdio>
#include <cmath>
#include "DarkProfile.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
static const double P[][2]={{1046,18.8e3},{1044,21.1e3},{1038,30.9e3},{1032,46.5e3},{1026,72.9e3},{1020,117.3e3},{1014,194.4e3},{1008,337.7e3}};
static double R(int d){ for(int i=0;i<7;i++) if(d<=P[i][0]&&d>=P[i+1][0]){ double f=(P[i][0]-d)/(P[i][0]-P[i+1][0]); return exp(log(P[i][1])+f*(log(P[i+1][1])-log(P[i][1]))); }
  double k=log(P[7][1]/P[6][1])/(P[6][0]-P[7][0]); return P[7][1]*exp(k*(P[7][0]-d)); }
int main(){
  using namespace DarkWalk;
  // 1. R's calibration cell through the firmware's nextDuty(): reach and points
  int32_t d=1046, pd=0; float pr=0; bool hp=false; int n=0; float last=0; int memPts=0;
  for(;n<DarkProfile::MAX_POINTS;){ float r=(float)R(d); n++; last=r; if(r>=MEMORY_FROM_OHMS) memPts++;
    printf("  %d:%.0fk%s",d,r/1e3,r>=MEMORY_FROM_OHMS?"*":""); if(r>=TOP_OHMS) break;
    int32_t nx=nextDuty(d,r,pd,pr,hp); pd=d; pr=r; hp=true; d=nx; }
  printf("\n  -> %d points, reach %.0fk, memory tests at %d of them\n",n,last/1e3,memPts);
  CHECK(last>=TOP_OHMS && n<=DarkProfile::MAX_POINTS && memPts<n);
  // 2. memoryAt(): only measured points count; shallow side conservative; beyond -> unknown
  DarkProfile p; p.clear();
  const float oh[]={19e3,21e3,41e3,85e3,179e3,370e3,773e3}, me[]={0,0,0,1.019f,1.026f,1.033f,1.060f};
  for(int i=0;i<7;i++){ DarkPoint q{}; q.duty=(uint16_t)(1046-i*7); q.ok=1; q.settleS=3; q.ohms=oh[i]; q.memory=me[i]; p.add(q); }
  float h;
  CHECK(p.memoryAt(30e3,h) && fabsf(h-1.019f)<1e-4);          // shallower than the first measured: its value
  CHECK(p.memoryAt(85e3,h) && fabsf(h-1.019f)<1e-4);
  CHECK(p.memoryAt(270e3,h) && h>1.026f && h<1.033f);           // between measured points: interpolated
  CHECK(p.memoryAt(773e3,h) && fabsf(h-1.060f)<1e-4);
  CHECK(!p.memoryAt(900e3,h));                                  // deeper than the walk: unknown
  DarkProfile z; z.clear(); for(int i=0;i<3;i++){ DarkPoint q{}; q.duty=(uint16_t)(1040-i*5); q.ok=1; q.ohms=oh[i]; q.memory=0; z.add(q); }
  CHECK(!z.memoryAt(30e3,h));                                   // nothing measured: unknown, never "0"
  printf("walk + memoryAt host test (R's calibration cell): PASS\n"); return 0; }
