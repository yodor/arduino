// Host test: cal args. Run all with: python3 test/run_tests.py
// CAL's grammar (CalArgs::parseCal) and the planner's choice (CellCheck::plan):
//   CAL | CAL R=<ohms> | CAL R=AUTO | CAL FULL; anything else refused.
#include <cstdio>
#include "CalArgs.hpp"
#include "CellCheck.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)
using namespace CellCheck;
int main(){
  bool hasR, rAuto, full, verify; long ohms;
  CHECK(CalArgs::parseCal("", hasR, rAuto, ohms, full, verify) && !hasR && !rAuto && !full);
  CHECK(CalArgs::parseCal("  ", hasR, rAuto, ohms, full, verify) && !hasR && !full);
  CHECK(CalArgs::parseCal("R=1600000", hasR, rAuto, ohms, full, verify) && hasR && ohms == 1600000 && !full);
  CHECK(CalArgs::parseCal("R=AUTO", hasR, rAuto, ohms, full, verify) && rAuto && !hasR);
  CHECK(CalArgs::parseCal("FULL", hasR, rAuto, ohms, full, verify) && full && !hasR && !rAuto);
  CHECK(CalArgs::parseCal("FULL ", hasR, rAuto, ohms, full, verify) && full);
  CHECK(CalArgs::parseCal("VERIFY", hasR, rAuto, ohms, full, verify) && verify && !full && !hasR);
  const char *bad[] = {"FULL R=AUTO", "FULLX", "VERIFY X", "VERIFYX", "VERIFY FULL", "R=", "R=0", "R=-5", "R=12X", "R=99999999", "R=AUTOX", "MODE=FAST", "FAST", "X"};
  for (const char *b : bad) { if (CalArgs::parseCal(b, hasR, rAuto, ohms, full, verify)) { printf("  accepted: '%s'\n", b); return 1; } }
  // planner
  CalRequest r;
  CHECK(plan(CellState::MATCH, r, true) == CalAction::RESYNC);
  CHECK(plan(CellState::CHANGED, r, true) == CalAction::FULL);
  r.full = true;
  CHECK(plan(CellState::MATCH, r, true) == CalAction::FULL);                      // CAL FULL with known cells
  r = CalRequest{}; r.hasR = true; r.ohms = 900000;
  CHECK(plan(CellState::MATCH, r, true) == CalAction::RESOLVE_RESYNC);
  r = CalRequest{}; r.rAuto = true;
  CHECK(plan(CellState::MATCH, r, false) == CalAction::RESOLVE_RESYNC && plan(CellState::MATCH, r, true) == CalAction::RESYNC);
  printf("  7 valid forms accepted, %u malformed refused; planner: FULL / RESYNC / RESOLVE as specified\n", (unsigned)(sizeof bad / sizeof *bad));
  printf("cal args host test: PASS\n"); return 0; }
