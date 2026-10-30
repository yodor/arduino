// Host test: walk comparison. Run all with: python3 test/run_tests.py
#include <cstdio>
#include <cmath>
#include <initializer_list>
#include "DarkProfile.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
static DarkProfile make(std::initializer_list<std::pair<int,double>> pts){
  DarkProfile p; p.clear(); for(auto &x: pts){ DarkPoint d{}; d.duty=(uint16_t)x.first; d.ohms=(float)x.second; d.ok=1; d.settleS=3; d.memory=1.02f; p.add(d); } return p; }
int main(){
  // your L: stored walk (full characterization) and the fresh walk from this DIAG
  DarkProfile st=make({{1002,24.9e3},{1000,27.9e3},{990,54.1e3},{980,116.9e3},{971,268.4e3},{963,626.5e3},{956,1538.6e3}});
  const float quietDuty=972.50f, quietTarget=1430895.0f;
  const float dStored=DarkProfileMath::dutyAt(st,quietTarget), shift=quietDuty-dStored;
  printf("  L: stored walk puts %.0f k at duty %.2f; the table has it at %.2f -> tracked shift %+.1f counts\n",quietTarget/1e3,dStored,quietDuty,shift);
  CHECK(fabsf(shift-15.9f)<0.5f);
  const int dNow[5]={1002,1000,992,984,977}; const double rNow[5]={83.6e3,100.0e3,199.2e3,441.7e3,992.4e3};
  float worst=1;
  for(int i=0;i<5;i++){ float e=DarkProfileMath::ohmsAt(st,dNow[i]-shift), r=(float)(rNow[i]/e); float dev=r>=1?r:1/r; if(dev>worst) worst=dev;
    printf("    duty %d: now %.1fk, stored at duty %.1f: %.1fk -> x%.2f   (old comparison at the same duty: x%.2f)\n",dNow[i],rNow[i]/1e3,dNow[i]-shift,e/1e3,r,dNow[i]==1002?83.6/24.9:dNow[i]==1000?100.0/27.9:NAN);
  }
  printf("  worst deviation x%.2f -> %s\n",worst,worst<=1.25f?"the table still fits":"re-measure");
  CHECK(worst<1.25f);
  printf("walk-comparison host test (your L walks): PASS\n"); return 0; }
