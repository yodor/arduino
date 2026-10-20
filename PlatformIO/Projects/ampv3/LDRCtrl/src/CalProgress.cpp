#include "CalProgress.hpp"

namespace {
CalProgress::Kind gKind = CalProgress::Kind::NONE;
CalProgress::Phase gPhase = CalProgress::Phase::IDLE;
char gSide = 0;
uint8_t gPart = 0, gParts = 1;
uint16_t gStep = 0, gSteps = 0;
uint32_t gPhaseStartMs = 0;
} // namespace

namespace CalProgress {

void begin() { gKind = Kind::NONE; phase(Phase::CHECK); }
void setKind(Kind k) { gKind = k; }

void phase(Phase p, char side, uint8_t part, uint8_t parts) {
  gPhase = p; gSide = side; gPart = part; gParts = parts ? parts : 1;
  gStep = 0; gSteps = 0;
  gPhaseStartMs = millis();
}

void step(uint16_t i, uint16_t n) { gStep = i; gSteps = n; }
void end() { gPhase = Phase::DONE; gSide = 0; gSteps = 0; }

void report(Print &out) {
  float frac;
  if (gSteps) frac = ((float)gPart + (float)gStep / gSteps) / gParts;
  else {
    const uint16_t nom = nominal(gKind, gPhase);
    frac = nom ? (millis() - gPhaseStartMs) / (1000.0f * nom) : 0.0f;
    if (gParts > 1) frac = ((float)gPart + (frac > 1 ? 1 : frac)) / gParts;
    if (frac > 0.95f) frac = 0.95f;     // time-based: never claim the phase is over
  }
  uint8_t pct; uint16_t eta;
  estimate(gKind, gPhase, frac, pct, eta);
  out.print(F(" KIND=")); out.print(kindName(gKind));
  out.print(F(" PHASE=")); out.print(phaseName(gPhase));
  if (gSide) { out.print(F(" SIDE=")); out.print(gSide); }
  if (gSteps) { out.print(F(" STEP=")); out.print(gStep); out.print('/'); out.print(gSteps); }
  out.print(F(" PCT=")); out.print(pct);
  out.print(F(" ETA=")); out.print(eta);
}

} // namespace CalProgress
