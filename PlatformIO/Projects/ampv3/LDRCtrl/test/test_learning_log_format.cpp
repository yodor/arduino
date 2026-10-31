// Host test: learning log format. Run all with: python3 test/run_tests.py
#include <cstdio>
#include <cmath>
#include "ThermalFit.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
int main(){
  ThermalFit::Entry e;
  // old format, sensors fitted (11 fields)
  CHECK(ThermalFit::parseLogLine("-6.750,-5.750,0.990,0.370,0.440,-0.1300,23.100,22.500,22.600,3.3330,12345", e));
  CHECK(e.shift[0]==-6.75f && e.shift[1]==-5.75f && fabsf(e.dBlock-0.99f)<1e-5f && fabsf(e.dRailPct+0.13f)<1e-5f);
  // old format, sensors absent: "nan" in the cell fields (11 fields)
  CHECK(ThermalFit::parseLogLine("-18.000,-18.000,3.570,nan,nan,0.0200,24.10,nan,nan,3.3330,20000", e));
  CHECK(e.shift[0]==-18.0f && fabsf(e.dBlock-3.57f)<1e-5f && fabsf(e.dRailPct-0.02f)<1e-5f);
  // old format, failed seek (exact zeros), CRLF ending
  CHECK(ThermalFit::parseLogLine("0.000,0.000,-5.510,nan,nan,-0.0200,18.20,nan,nan,3.3320,30000\r", e) && ThermalFit::seekFailed(e));
  // new format (7 fields)
  CHECK(ThermalFit::parseLogLine("22.000,21.375,-0.520,0.0100,23.080,3.3333,50350", e));
  CHECK(e.shift[0]==22.0f && fabsf(e.shift[1]-21.375f)<1e-5f && fabsf(e.dBlock+0.52f)<1e-5f && fabsf(e.dRailPct-0.01f)<1e-5f);
  // new format with an unknown shift (non-converged seek)
  CHECK(ThermalFit::parseLogLine("nan,-4.120,0.520,-0.1700,23.5,3.333,60000", e) && ThermalFit::seekFailed(e));
  // not entries: wrong field counts, empty, garbage
  CHECK(!ThermalFit::parseLogLine("1,2,3,4,5,6", e) && !ThermalFit::parseLogLine("", e) && !ThermalFit::parseLogLine("hello", e));
  CHECK(!ThermalFit::parseLogLine("1,2,3,4,5,6,7,8,9", e));
  printf("learning-log format host test (old 11-field and new 7-field lines): PASS\n"); return 0; }
