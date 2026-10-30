// Host test: series guard. Run all with: python3 test/run_tests.py
#include <cstdio>
#include <cmath>
#include "QuietSeek.hpp"
#include "guard_data.h"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
static int run(const char *lab, int n, const double *ser, const double *cur, int watch, double lo, double hi){
  uint32_t s[64], c[64]; bool use[64];
  for(int i=0;i<n;i++){ s[i]=(uint32_t)lround(ser[i]*16); c[i]=cur[i]>0?(uint32_t)lround(cur[i]*16):0; use[i]=true; }
  uint8_t f=QuietSeek::guardSeries(s,use,c,(uint8_t)n);
  printf("  %s: %u step(s) corrected; step %d: %.2f -> %.2f  (curve %.2f)\n",lab,f,watch,ser[watch],s[watch]/16.0,cur[watch]);
  for(int i=1;i<n;i++) if(s[i]<s[i-1]){ printf("  not monotonic at %d\n",i); return 1; }
  if(!(s[watch]/16.0>lo && s[watch]/16.0<hi)) return 1;
  return 0; }
int main(){
  CHECK(N_L==31 && N_R==31);
  CHECK(run("L",N_L,SER_L,CUR_L,29,1050.0,1065.0)==0);   // was 1031.44, ~step 26's resistance
  CHECK(run("R",N_R,SER_R,CUR_R,30,1090.0,1110.0)==0);   // was 1071.13, darker than step 29
  printf("series-guard host test (your two step tables and curves): PASS\n"); return 0; }
