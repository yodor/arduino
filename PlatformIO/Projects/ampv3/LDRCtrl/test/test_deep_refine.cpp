// Host test: deep refine. Run all with: python3 test/run_tests.py
#include <cstdio>
#include <cmath>
#include <initializer_list>
#include "QuietSeek.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
using namespace QuietSeek::DeepRefine;
int main(){
  // your L table's targets (steps 0..30; 31 transparent) and its walk's shallowest point
  const float rs[31]={1430895,1430857,1430804,1430732,1430631,1430492,1430296,1430018,1429616,1429019,1428101,1426601,1423905,1418132,1399932,
                      1275857,974586,719513,527948,386097,281454,204371,147630,105878,75163,52569,35953,23732,14744,8135,3274};
  const float lo=24.9e3f, quiet=1430895.0f;
  bool deep[31]; for(int i=0;i<31;i++) deep[i]= rs[i]>=lo;
  int16_t pr[NPROBE]; chooseProbes(rs,deep,31,lo,quiet,pr);
  printf("  probes: step %d (%.0fk), step %d (%.0fk), step %d (%.0fk)\n",pr[0],rs[pr[0]]/1e3,pr[1],rs[pr[1]]/1e3,pr[2],rs[pr[2]]/1e3);
  CHECK(pr[0]==17 && pr[1]==21 && pr[2]==25);
  // the deviations your corrected DIAG comparison found at those depths, with the curve's local slope
  const float ratio[NPROBE]={1.22f,1.18f,1.13f}, slope[NPROBE]={0.105f,0.090f,0.070f};
  float pRs[NPROBE], pC[NPROBE];
  for(int k=0;k<NPROBE;k++){ pRs[k]=rs[pr[k]]; pC[k]=logf(ratio[k])/slope[k]; }
  printf("  corrections at the probes: %+.2f / %+.2f / %+.2f counts\n",pC[0],pC[1],pC[2]);
  for(int i: {0,9,14,15,16,17,19,21,23,25,26}) printf("    step %2d (%7.0fk): %+.2f counts\n",i,rs[i]/1e3,correctionAt(rs[i],pRs,pC,NPROBE,quiet));
  CHECK(correctionAt(rs[0],pRs,pC,NPROBE,quiet)==0.0f && correctionAt(rs[14],pRs,pC,NPROBE,quiet)==0.0f);  // the quiet point: untouched
  CHECK(fabsf(correctionAt(rs[17],pRs,pC,NPROBE,quiet)-pC[0])<1e-4f && fabsf(correctionAt(rs[25],pRs,pC,NPROBE,quiet)-pC[2])<1e-4f);
  CHECK(correctionAt(rs[26],pRs,pC,NPROBE,quiet)==pC[2]);                           // shallower than the last probe: held
  const float c16=correctionAt(rs[16],pRs,pC,NPROBE,quiet); CHECK(c16>0 && c16<pC[0]);   // between the quiet point and the deepest probe
  // a probe that did not settle (NaN) is simply left out
  float pC2[NPROBE]={pC[0],NAN,pC[2]}; CHECK(isfinite(correctionAt(rs[21],pRs,pC2,NPROBE,quiet)));
  printf("deep-step refinement host test (your L table): PASS\n"); return 0; }
