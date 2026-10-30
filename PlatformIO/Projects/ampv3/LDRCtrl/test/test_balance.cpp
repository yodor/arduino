// Host test: balance. Run all with: python3 test/run_tests.py
#include <cstdio>
#include <cmath>
#include <initializer_list>
#include "Balance.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
// your L table at 844.5k (targets from the DIAG); step 31 is transparent (Rs 150, shunt open)
static const float DB[32]={-77.99f,-75.47f,-72.96f,-70.44f,-67.93f,-65.41f,-62.90f,-60.38f,-57.86f,-55.35f,-52.83f,-50.32f,-47.80f,-45.28f,-42.77f,-40.25f,
  -37.74f,-35.22f,-32.71f,-30.19f,-27.67f,-25.16f,-22.64f,-20.13f,-17.61f,-15.09f,-12.58f,-10.06f,-7.55f,-5.03f,-2.52f,-0.38f};
static const float RS[32]={844392,844356,844306,844240,844149,844025,843856,843620,843289,842814,842113,841035,839270,836062,829031,806567,
  708328,548068,411970,307194,227935,168304,123539,89972,64817,45973,31860,21292,13379,7455,3020,150};
static const float RSH[32]={108,144,194,260,351,475,644,880,1211,1686,2387,3465,5230,8438,15469,37933,
  136172,296432,432530,537306,616565,676197,720961,754528,779683,798527,812640,823208,831121,837045,841480,1e12f};
static float level(float rs,float rsh){ const float RL=10000,RSRC=300; float p=rsh*RL/(rsh+RL); return 20*log10f(p/(RSRC+rs+p)); }
int main(){
  using namespace Balance;
  CHECK(attenuationDb(4,false)==1.0f && attenuationDb(4,true)==0.0f && attenuationDb(-16,true)==4.0f && attenuationDb(-16,false)==0.0f);
  bool ok[32]; for(int i=0;i<32;i++) ok[i]=true;
  float worst=0; int wi=0; float wa=0;
  for(int step: {31,30,24,16,11,3,1,0}) for(int u=1;u<=16;u++){
    const float att=u*DB_PER_UNIT; uint8_t up,lo; float f; locate(DB,ok,32,(uint8_t)step,att,up,lo,f);
    float rs=RS[up], rsh=RSH[up];
    if(up!=lo) solveBetween(DB[step]-att,RS[up],RSH[up],up==31,RS[lo],RSH[lo],300.0f,10000.0f,rs,rsh);
    const float want = DB[step]-att, got=level(rs,rsh);
    if(want < DB[0]) continue;                                    // below the quietest step: clamped by design
    const float err=fabsf(got-want); if(err>worst){ worst=err; wi=step; wa=att; }
  }
  printf("  worst level error over steps 0..31 and 0.25..4 dB: %.3f dB (step %d, -%.2f dB)\n",worst,wi,wa);
  CHECK(worst<0.05f);
  // below the quietest step: clamped to step 0
  { uint8_t up,lo; float f; locate(DB,ok,32,1,4.0f,up,lo,f); CHECK(up==0 && lo==0); }
  // centre: exactly the step
  { uint8_t up,lo; float f; locate(DB,ok,32,20,0.0f,up,lo,f); CHECK(up==20 && lo==20 && f==0.0f); }
  printf("balance host test (your L table): PASS\n"); return 0; }
