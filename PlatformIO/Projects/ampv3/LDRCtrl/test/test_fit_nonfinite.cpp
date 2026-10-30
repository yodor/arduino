// Host test: fit nonfinite. Run all with: python3 test/run_tests.py
#include <cstdio>
#include <cmath>
#include "ThermalFit.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
int main(){
  ThermalFit::Entry e[4]; const float dB[4]={1.0f,-1.5f,0.9f,1.3f};
  for(int i=0;i<4;i++){ e[i].dBlock=dB[i]; e[i].shift[0]=-5.8f*dB[i]; e[i].shift[1]=-5.3f*dB[i]; e[i].dRailPct=INFINITY; }
  ThermalFit::Slope b=ThermalFit::fit(e,4,0,0), r=ThermalFit::fit(e,4,0,1);
  printf("  block fit: k=%.2f R2=%.2f (n=%u) | rail fit with inf entries: n=%u\n",b.k,b.r2,b.n,r.n);
  CHECK(fabsf(b.k+5.8f)<0.01f && b.n==4 && r.n==0 && std::isnan(r.k));
  printf("NaN/inf-entry fit host test: PASS\n"); return 0; }
