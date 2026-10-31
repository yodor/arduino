// Host test: seek fix. Run all with: python3 test/run_tests.py
#include <cstdio>
#include <cmath>
#include "ThermalFit.hpp"
#include "QuietSeek.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
int main(){
  // 1. your learning log (#3..#22)
  const float d[20][4]={{-6.75f,-5.75f,0.99f,3},{8.56f,7.69f,-1.57f,4},{-5.75f,-5.37f,1.18f,5},{-9.50f,-8.50f,1.49f,6},{-6.50f,-5.31f,0.91f,7},
    {9.31f,9.31f,-1.83f,8},{10.31f,10.19f,-1.80f,9},{0.06f,0.00f,-0.10f,10},{-13.75f,-13.50f,2.61f,11},{9.00f,9.00f,-1.80f,12},{-18.00f,-18.00f,3.57f,13},
    {-6.06f,-5.31f,0.91f,14},{-4.69f,-4.12f,0.52f,15},{3.31f,2.75f,-0.20f,16},{0,0,-5.51f,17},{0,0,3.41f,18},{0,0,-2.25f,19},{5.12f,5.94f,1.68f,20},{-0.19f,-0.62f,0.23f,21},{0,0,-3.56f,22}};
  ThermalFit::Entry e[20];
  for(int i=0;i<20;i++){ e[i].shift[0]=d[i][0]; e[i].shift[1]=d[i][1]; e[i].dBlock=d[i][2]; e[i].dRailPct=0; }
  ThermalFit::Slope L=ThermalFit::fit(e,20,0,0), R=ThermalFit::fit(e,20,1,0);
  printf("  fit on your log: L %+.2f counts/C R2 %.2f (n=%u) | R %+.2f counts/C R2 %.2f (n=%u)\n",L.k,L.r2,L.n,R.k,R.r2,R.n);
  CHECK(fabsf(L.k+5.40f)<0.02f && L.r2>0.97f && fabsf(R.k+5.21f)<0.02f && R.r2>0.98f && L.n==15);
  // 2. guard on your DIAG's shunt duties (steps 0..30; 31 transparent)
  const double sl[31]={2156.38,1782.81,1557.63,1417.25,1319.25,1250.31,1205.38,1171.88,1146.25,1125.69,1109.63,1095.81,1082.94,1071.81,1061.06,1048.75,1034.69,1020.56,1011.62,1046.25,1027.44,1001.62,1025.44,1017.19,1079.19,1000.94,1020.69,1017.31,1020.94,1020.81,1035.63};
  const double sr[31]={1578.25,1431.88,1341.06,1271.50,1223.94,1185.88,1158.56,1137.06,1119.88,1104.94,1092.88,1081.88,1072.13,1062.44,1052.69,1042.38,1030.25,1016.31,1007.81,1026.94,1025.75,1028.06,1022.00,1018.81,1118.56,996.00,1075.63,995.75,1075.25,994.44,1005.75};
  for(int ch=0;ch<2;ch++){ uint32_t s[31]; bool use[31]; for(int i=0;i<31;i++){ s[i]=(uint32_t)lround((ch?sr:sl)[i]*16); use[i]=true; }
    uint8_t f=QuietSeek::guardShunt(s,use,31);
    printf("  guard %c: %u steps corrected; step 24 %.2f -> %.2f\n",ch?'R':'L',f,(ch?sr:sl)[24],s[24]/16.0);
    for(int i=1;i<31;i++) CHECK(s[i]<=s[i-1]);
    CHECK(s[24]/16.0 < 1012.0); }
  // 3. the seek on a cell 22 counts too dark (R(d) = 500k * exp(-0.105 (d - dTrue)), unreadable above 3.3 MOhm)
  const uint32_t FPC=16; const double dTrue=1000.0;
  auto cell=[&](uint32_t f){ double r=500e3*exp(-0.105*(f/16.0-dTrue)); return r>3.3e6? -1.0f : (float)r; };
  uint32_t f=(uint32_t)((dTrue-22)*FPC); bool ok=false; int probes=0;
  for(;probes<8 && !ok;){ probes++; QuietSeek::Decision q=QuietSeek::decide(f,cell(f),500e3f,0.105f,FPC); ok=q.ok; if(!ok) f=q.next; }
  printf("  new seek, 22 counts too dark: %s after %d probes, duty %.2f (true %.0f)\n",ok?"converged":"NOT converged",probes,f/16.0,dTrue);
  CHECK(ok && fabs(f/16.0-dTrue)<0.3);
  // the old rule: unreadable -> stop; steps clamped to 3 counts
  uint32_t g=(uint32_t)((dTrue-22)*FPC); float r0=cell(g);
  printf("  old seek, same cell: first reading %s -> gave up, shift recorded 0.00\n", r0>0?"readable":"UNREADABLE");
  CHECK(!(r0>0));
  // a channel that never reads (hardware fault): decide() never reports readable
  CHECK(!QuietSeek::decide(f,-1.0f,500e3f,0.105f,FPC).readable);
  printf("seek-fix host test (your learning log, your step table): PASS\n"); return 0; }
