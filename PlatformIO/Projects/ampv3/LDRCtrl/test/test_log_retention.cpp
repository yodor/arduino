// Host test: log retention. Run all with: python3 test/run_tests.py
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include "ThermalFit.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
int main(){
  // block temperature parser, both formats
  CHECK(fabsf(ThermalFit::parseLogBlock("22.000,21.375,-0.520,0.0100,23.080,3.3333,50350")-23.08f)<1e-4f);
  CHECK(fabsf(ThermalFit::parseLogBlock("-6.750,-5.750,0.990,0.370,0.440,-0.1300,23.100,22.500,22.600,3.3330,12345")-23.10f)<1e-4f);
  CHECK(std::isnan(ThermalFit::parseLogBlock("1,2,3")));
  // a simulated year, trimmed after every append the way the firmware does
  const int MAXN=200; static bool usable[MAXN]; static float block[MAXN]; static bool keep[MAXN]; static int id[MAXN];
  int n=0, next=0; srand(1);
  auto add=[&](float t, bool ok){ usable[n]=ok; block[n]=t; id[n]=next++; n++;
    if(n>48){ ThermalFit::selectKeep(usable,block,(uint16_t)n,keep,12,2,48); int m=0;
      for(int i=0;i<n;i++) if(keep[i]){ usable[m]=usable[i]; block[m]=block[i]; id[m]=id[i]; m++; } n=m; } };
  for(int i=0;i<30;i++) add(18.0f+4.0f*rand()/RAND_MAX,true);            // winter 18..22 C
  for(int i=0;i<30;i++) add(22.0f+4.0f*rand()/RAND_MAX,true);            // spring 22..26 C
  add(30.0f,false); add(30.5f,false);                                     // a failed seek + its catch-up
  for(int i=0;i<150;i++) add(28.0f+8.0f*rand()/RAND_MAX,true);           // summer 28..36 C
  int winter=0, spring=0, summer=0, bad=0; float lo=99, hi=-99;
  for(int i=0;i<n;i++){ if(!usable[i]) bad++; if(block[i]<22) winter++; else if(block[i]<26) spring++; else summer++; if(block[i]<lo) lo=block[i]; if(block[i]>hi) hi=block[i]; }
  printf("  after 212 resyncs: %d kept -- winter %d, spring %d, summer %d; block %.1f..%.1f C; failed/catch-up kept: %d\n",n,winter,spring,summer,lo,hi,bad);
  CHECK(n<=48 && winter>=6 && spring>=6 && summer>=12 && bad==0 && lo<19.0f && hi>35.0f);
  for(int i=1;i<n;i++) CHECK(id[i]>id[i-1]);                               // order preserved
  CHECK(id[n-1]==next-1);                                                  // the newest is always kept
  printf("  (the old rule -- the newest 30 lines -- would have kept winter 0, spring 0)\n");
  printf("temperature-spread retention host test: PASS\n"); return 0; }
