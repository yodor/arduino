// Host test: auto pref cell check. Run all with: python3 test/run_tests.py
#include <cstdio>
#include <cmath>
#include <initializer_list>
#include "CellCheck.hpp"
#include "AutoPref.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
using namespace CellCheck;
static CellFingerprint fp(float t, const float *ser, const float *shu){ CellFingerprint f{}; for(int i=0;i<NFP;i++){ f.duty[i]=(uint16_t)(1392+i*628); f.ser[i]=ser[i]; f.shu[i]=shu[i]; } f.tempC=t; f.valid=1; return f; }
int main(){
  const float s0[4]={191.1f,94.4f,72.3f,61.9f}, h0[4]={268.8f,121.2f,90.4f,76.3f};
  CellFingerprint stored=fp(24.4f,s0,h0);
  float s1[4], h1[4]; for(int i=0;i<4;i++){ s1[i]=s0[i]*(i<2?1.065f:1.02f); h1[i]=h0[i]*1.02f; }
  CellState warm=judge(true,stored,fp(29.0f,s1,h1)), same=judge(true,stored,fp(24.4f,s1,h1));
  printf("  two duties x1.065: die +4.6 C -> %s (tolerance x%.3f) | same temperature -> %s (x%.3f)\n",
         warm==CellState::MATCH?"MATCH":"CHANGED",matchTol(4.6f),same==CellState::MATCH?"MATCH":"CHANGED",matchTol(0));
  CHECK(warm==CellState::MATCH && same==CellState::CHANGED);
  float s2[4], h2[4]; for(int i=0;i<4;i++){ s2[i]=s0[i]*1.25f; h2[i]=h0[i]*0.8f; }
  CHECK(judge(true,stored,fp(29.0f,s2,h2))==CellState::CHANGED);             // a real swap, even 4.6 C warmer
  CHECK(fabsf(matchTol(30.0f)-MATCH_TOL_MAX)<1e-6f && fabsf(matchTol(NAN)-MATCH_TOL)<1e-6f);
  printf("  a swapped cell (x1.25 / x0.80 at every duty), 4.6 C warmer -> CHANGED\n");
  // preference
  AutoPref::Pref p;
  CHECK(AutoPref::parse("deep",p) && p==AutoPref::Pref::DEEP && AutoPref::parse("Snappy",p) && p==AutoPref::Pref::SNAPPY && !AutoPref::parse("fast",p));
  for(AutoPref::Pref q: {AutoPref::Pref::SNAPPY,AutoPref::Pref::BALANCED,AutoPref::Pref::DEEP}){ auto a=AutoPref::params(q);
    printf("  %-8s walk to %4.0fk, memory <= x%.2f, slope <= %2.0f %%/ct, at most %4.0fk\n",AutoPref::name(q),a.walkTopOhms/1e3,a.memMax,a.slopeMax*100,a.capOhms/1e3); }
  CHECK(AutoPref::params(AutoPref::Pref::BALANCED).memMax==1.12f);            // BALANCED = today's limit
  printf("preference + temperature-aware cell check host test: PASS\n"); return 0; }
