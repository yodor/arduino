// Host test: comp range ntc. Run all with: python3 test/run_tests.py
#include <cstdio>
#include <cmath>
#include <initializer_list>
#include "BoardSensors.hpp"
#include "ThermalComp.hpp"
#include "ThermalFit.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
using namespace BoardSensors;
static float codeFor(float tC){ double R=NTC_R25_OHMS*exp(NTC_B*(1.0/(tC+273.15)-1.0/298.15)); return (float)(ADC_FULL*R/(R+NTC_TOP_OHMS)); }
int main(){
  // 1. NTC states at real ADC codes
  CHECK(ntcState(ADC_FULL)==NtcState::OPEN && ntcState(ADC_FULL-5)==NtcState::OPEN);
  CHECK(ntcState(0)==NtcState::SHORT && ntcState(10)==NtcState::SHORT);
  for(float t: {18.0f,25.0f,40.0f,50.0f}){ float c=codeFor(t); CHECK(ntcState(c)==NtcState::OK && fabsf(ntcCelsius(c)-t)<0.05f); }
  CHECK(ntcState(codeFor(-30.0f))==NtcState::BAD && std::isnan(ntcCelsius(codeFor(-30.0f))));
  printf("  NTC: open %s, short %s, 18/25/40/50 C OK (code %.0f..%.0f), -30 C %s\n",ntcStateName(ntcState(ADC_FULL)),ntcStateName(ntcState(0)),codeFor(50),codeFor(18),ntcStateName(ntcState(codeFor(-30))));
  // 2. span-based offset across 18..50 C, calibrated at 27 C, your slope
  const float k=-5.4f, tCal=27.0f;
  for(float t: {18.0f,50.0f,60.0f}) printf("  block %.0f C (cal %.0f C) -> offset %+.1f counts\n",t,tCal,ThermalComp::offsetCounts(k,t,tCal));
  CHECK(ThermalComp::offsetCounts(k,18.0f,tCal)==k*(18.0f-tCal));            // +48.6: full, no cap
  CHECK(ThermalComp::offsetCounts(k,50.0f,tCal)==k*(50.0f-tCal));            // -124.2: full, no cap
  CHECK(ThermalComp::offsetCounts(k,60.0f,tCal)==k*ThermalComp::MAX_SPAN_C); // beyond 30 C: held at 30 C
  CHECK(ThermalComp::offsetCounts(k,NAN,tCal)==0.0f);                         // faulty NTC: none
  // 3. slope guard
  CHECK(ThermalComp::trusted(true,0.98f,-5.4f) && !ThermalComp::trusted(true,0.98f,-40.0f) && !ThermalComp::trusted(true,0.4f,-5.4f));
  // 4. learned span from your log (failed seeks and catch-ups excluded)
  const float d[20][3]={{-6.75f,-5.75f,0.99f},{8.56f,7.69f,-1.57f},{-5.75f,-5.37f,1.18f},{-9.50f,-8.50f,1.49f},{-6.50f,-5.31f,0.91f},{9.31f,9.31f,-1.83f},{10.31f,10.19f,-1.80f},{0.06f,0.00f,-0.10f},{-13.75f,-13.50f,2.61f},{9.00f,9.00f,-1.80f},{-18.00f,-18.00f,3.57f},{-6.06f,-5.31f,0.91f},{-4.69f,-4.12f,0.52f},{3.31f,2.75f,-0.20f},{0,0,-5.51f},{0,0,3.41f},{0,0,-2.25f},{5.12f,5.94f,1.68f},{-0.19f,-0.62f,0.23f},{0,0,-3.56f}};
  ThermalFit::Entry e[20]; for(int i=0;i<20;i++){ e[i].shift[0]=d[i][0]; e[i].shift[1]=d[i][1]; e[i].dBlock=d[i][2]; e[i].dRailPct=0; }
  ThermalFit::Slope s=ThermalFit::fit(e,20,0,0);
  printf("  learned span from your log: %.2f C (the 5.51 C failed seek is excluded)\n",s.maxAbs);
  CHECK(fabsf(s.maxAbs-3.57f)<1e-4f);
  printf("range / NTC host test: PASS\n"); return 0; }
