#include "DualCalibration.hpp"
#include "DriverChannels.hpp"
#include "Config.hpp"
#include "BusyHook.hpp"
#include "Board.hpp"
#include "SweepLadder.hpp"
#include "Capabilities.hpp"
#include <math.h>

namespace DualCalibration {

namespace {

// Coarse point lists, as percentages of full duty.
constexpr uint16_t FULL_PCT[] = {15, 17, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                                  31, 32, 33, 34, 35, 36, 38, 40, 45, 50, 60, 70, 80, 90, 100};

// Knee refinement. A gap between neighbouring points is split when either
// channel's resistance changes by more than REFINE_MIN_RATIO across it and
// the gap is wider than REFINE_MIN_GAP duty counts. REFINE_MAX_OHMS keeps
// it out of the deep-dark region where readings are lag-dominated noise.
constexpr float         REFINE_MIN_RATIO     = 1.4f;
constexpr float         REFINE_MAX_OHMS      = 3.0e6f;
constexpr uint16_t      REFINE_MIN_GAP       = 6;
constexpr uint8_t       REFINE_MAX_PASSES    = 4;
// Each refinement pass starts from a relaxed dark state so its points are
// reached the same way the coarse sweep reached its own (ascending).
constexpr unsigned long REFINE_DARK_DWELL_MS = 1200;

constexpr uint8_t MAX_CH = 2;
constexpr uint8_t MAX_SWEEP_POINTS = 96;

struct Sample {
  uint16_t duty;
  float ohms[MAX_CH]; // <= 0 means no valid reading on that channel
};

// File-scope rather than on the stack: ~3.5 KB that would otherwise sit
// on top of the console/protocol call chain.
// How far into the dark the sweep must reach (its low-end extension and dark-edge
// bisection): enough to give the settled dark-end walk a starting point well past
// DarkWalk::START_OHMS, whatever Rtotal was requested. An algorithm constant, not a
// hardware value: the walk, not the sweep, measures what the cells can really do.
constexpr float SWEEP_DARK_REF_OHMS = 150000.0f;
float darkRefOhms(const LDRVolume *v) {
  return v->rTotalOhms() > SWEEP_DARK_REF_OHMS ? v->rTotalOhms() : SWEEP_DARK_REF_OHMS;
}

// Poll every active channel's series reading once a second until each one's last three
// readings agree within DarkWalk::SETTLE_BAND (or SETTLE_MAX_MS passes). Returns, per
// channel, the mean of the last three readings (0 = unreadable) and the seconds taken.
void settleRead(LDRVolume *const *ch, uint8_t na, const bool *active, float *ohms, uint8_t *secs, bool *settled) {
  float h[MAX_CH][3];
  uint8_t got[MAX_CH] = {0, 0};
  bool done[MAX_CH] = {false, false};
  for (uint8_t c = 0; c < na; c++) { ohms[c] = 0.0f; secs[c] = (uint8_t)(DarkWalk::SETTLE_MAX_MS / 1000); settled[c] = false; if (!active[c]) done[c] = true; }
  for (uint32_t t = 1; t * DarkWalk::SETTLE_POLL_MS <= DarkWalk::SETTLE_MAX_MS; t++) {
    BusyHook::wait(DarkWalk::SETTLE_POLL_MS);
    bool all = true;
    for (uint8_t c = 0; c < na; c++) {
      if (done[c]) continue;
      float vref, rs, rsh;
      if (!ch[c]->adcRead(vref, rs, rsh) || rs <= 0.0f) { done[c] = true; ohms[c] = 0.0f; continue; }
      h[c][got[c] % 3] = rs; got[c]++;
      ohms[c] = rs;
      if (got[c] >= 3) {
        float lo = h[c][0], hi = h[c][0];
        for (uint8_t k = 1; k < 3; k++) { if (h[c][k] < lo) lo = h[c][k]; if (h[c][k] > hi) hi = h[c][k]; }
        if (hi <= lo * DarkWalk::SETTLE_BAND) {
          ohms[c] = (h[c][0] + h[c][1] + h[c][2]) / 3.0f;
          secs[c] = (uint8_t)t; settled[c] = true; done[c] = true;
        }
      }
      if (!done[c]) all = false;
    }
    if (all) break;
  }
}

float readAvg3(LDRVolume *v) {
  float sum = 0; uint8_t m = 0;
  for (uint8_t r = 0; r < 3; r++) {
    float vref, rs, rsh;
    if (v->adcRead(vref, rs, rsh) && rs > 0.0f) { sum += rs; m++; }
    BusyHook::wait(20);
  }
  return m ? sum / m : 0.0f;
}

void readAvg3Pair(LDRVolume *v, float &rsOut, float &rshOut) {
  float s = 0, h = 0; uint8_t m = 0;
  for (uint8_t r = 0; r < 3; r++) {
    float vref, rs, rsh;
    if (v->adcRead(vref, rs, rsh) && rs > 0.0f && rsh > 0.0f) { s += rs; h += rsh; m++; }
    BusyHook::wait(20);
  }
  rsOut = m ? s / m : 0.0f;
  rshOut = m ? h / m : 0.0f;
}

} // namespace (helpers)

void takeFingerprint(LDRVolume *const *ch, uint8_t na, CellFingerprint *fp);
void walkDarkEnd(LDRVolume *const *ch, uint8_t na, const char *labels, DarkProfile *prof, Stream &out);
void settleQuietPoint(LDRVolume *const *ch, uint8_t na, const char *labels, Stream &out);

namespace {

Sample gSamples[MAX_SWEEP_POINTS];
Sample gAdded[MAX_SWEEP_POINTS];
uint16_t gEdgeDuty[MAX_SWEEP_POINTS]; // one channel's view of gSamples, for SweepLadder::darkEdgeMid
float gEdgeOhms[MAX_SWEEP_POINTS];
Sample gMerged[MAX_SWEEP_POINTS];

struct ConvState {
  float prev = -1.0f;
  float lastGood = -1.0f;
  unsigned long start = 0;
  bool done = false;
};

// One non-blocking poll. Same convergence rule as LDRVolume::convergeRead
// (two consecutive readings within 1%, or timeout).
void pollOne(LDRVolume &v, bool wantSeries, unsigned long timeoutMs, ConvState &st) {
  if (st.done) return;
  float vRef, rs, rsh;
  bool ok = v.adcRead(vRef, rs, rsh);
  float cur = wantSeries ? rs : rsh;
  if (ok) {
    st.lastGood = cur;
    if (st.prev > 0.0f && fabsf(cur - st.prev) / st.prev < 0.01f) {
      st.done = true;
      return;
    }
    st.prev = cur;
  }
  if (millis() - st.start >= timeoutMs) st.done = true;
}

void driveSwept(LDRVolume *ch[], uint8_t na, bool wantSeries, uint16_t duty) {
  const uint32_t FPC = DriverChannels::FINE_PER_COUNT;
  for (uint8_t c = 0; c < na; c++) ch[c]->driveElementFine(wantSeries, (uint32_t)duty * FPC);
}

// Drive one duty on every channel and wait until all have converged (or
// timed out), polling them in alternation.
void measurePoint(LDRVolume *ch[], uint8_t na, bool wantSeries, uint16_t duty,
                  unsigned long timeoutMs, Sample &out, unsigned long &elapsedMs) {
  driveSwept(ch, na, wantSeries, duty);
  ConvState st[MAX_CH];
  unsigned long t0 = millis();
  for (uint8_t c = 0; c < na; c++) st[c].start = t0;
  bool allDone = false;
  while (!allDone) {
    allDone = true;
    for (uint8_t c = 0; c < na; c++) {
      pollOne(*ch[c], wantSeries, timeoutMs, st[c]);
      if (!st[c].done) allDone = false;
    }
    BusyHook::wait(15);
  }
  out.duty = duty;
  for (uint8_t c = 0; c < MAX_CH; c++) out.ohms[c] = (c < na) ? st[c].lastGood : -1.0f;
  elapsedMs = millis() - t0;
}

void printPoint(Stream &out, const Sample &s, uint8_t na, const uint8_t *idx,
                const char *labels, bool wantSeries, unsigned long elapsedMs, bool refined) {
  out.print(refined ? F("  ~duty=") : F("  duty="));
  out.print(s.duty);
  for (uint8_t c = 0; c < na; c++) {
    out.print(F("  "));
    if (labels) { out.print(labels[idx[c]]); out.print('='); }
    else        { out.print(wantSeries ? F("Rs=") : F("Rsh=")); }
    if (s.ohms[c] > 0.0f) out.print(s.ohms[c], 1); else out.print(F("ERR"));
  }
  out.print(F("  ("));
  out.print(elapsedMs);
  out.println(F("ms)"));
}

bool needsRefine(const Sample &a, const Sample &b, uint8_t na) {
  if ((uint16_t)(b.duty - a.duty) <= REFINE_MIN_GAP) return false;
  for (uint8_t c = 0; c < na; c++) {
    if (a.ohms[c] > 0.0f && b.ohms[c] > 0.0f && a.ohms[c] < REFINE_MAX_OHMS &&
        a.ohms[c] / b.ohms[c] > REFINE_MIN_RATIO) {
      return true;
    }
  }
  return false;
}

// One sweep of the swept element (series or shunt) across all channels,
// the other element held bright. Measures a coarse list, refines across
// the knee, then feeds every point to the curves in ascending duty order.
void sweepPhase(LDRVolume *ch[], uint8_t na, const uint8_t *idx, const char *labels,
                bool wantSeries, const uint16_t *pts, uint8_t n,
                unsigned long timeoutMs, Stream &out) {
  const uint32_t FPC = DriverChannels::FINE_PER_COUNT;
  for (uint8_t c = 0; c < na; c++) {
    // The element that is NOT being swept sits at CAL_BRIGHT_HOLD_DUTY, not full
    // scale: it only needs to be near its floor, and more current just heats it.
    ch[c]->driveElementFine(!wantSeries, (uint32_t)CAL_BRIGHT_HOLD_DUTY * FPC);
  }

  uint8_t ns = 0;
  for (uint8_t i = 0; i < n && ns < MAX_SWEEP_POINTS; i++) {
    uint16_t duty = (uint16_t)(((uint32_t)pts[i] * DriverChannels::WRAP) / 100);
    unsigned long el;
    measurePoint(ch, na, wantSeries, duty, timeoutMs, gSamples[ns], el);
    printPoint(out, gSamples[ns], na, idx, labels, wantSeries, el, false);
    ns++;
  }

  // Low-end extension (see SweepLadder.hpp): if the lowest point already reads
  // low-resistance on any channel, the knee is below the coarse list -- walk
  // downward until the curve has a dark end. Does nothing on hardware whose
  // knee is inside the coarse list.
  for (uint8_t round = 0; round < SweepLadder::MAX_ROUNDS && ns > 0; round++) {
    bool need = false;
    for (uint8_t c = 0; c < na; c++) {
      if (SweepLadder::tooLow(gSamples[0].ohms[c], darkRefOhms(ch[c]))) need = true;
    }
    if (!need) break;

    uint16_t ladder[SweepLadder::POINTS];
    const uint8_t nl = SweepLadder::build(gSamples[0].duty, round, DriverChannels::WRAP, ladder);
    if (nl == 0 || (uint16_t)ns + nl > MAX_SWEEP_POINTS) break;

    out.print(F("  -- extending low end, round "));
    out.print(round + 1);
    out.print(F(": "));
    out.print(nl);
    out.println(F(" points below the knee --"));

    driveSwept(ch, na, wantSeries, ladder[0]); // relax at the darkest point, then climb
    BusyHook::wait(REFINE_DARK_DWELL_MS);

    for (uint8_t k = 0; k < nl; k++) {
      unsigned long el;
      measurePoint(ch, na, wantSeries, ladder[k], timeoutMs, gAdded[k], el);
      printPoint(out, gAdded[k], na, idx, labels, wantSeries, el, true);
    }
    for (int i = (int)ns - 1; i >= 0; i--) gSamples[i + nl] = gSamples[i];
    for (uint8_t k = 0; k < nl; k++) gSamples[k] = gAdded[k];
    ns = (uint8_t)(ns + nl);
  }

  // Dark-edge bisection (see SweepLadder::darkEdgeMid): where a channel jumps from
  // UNREADABLE straight to a reading that is still too low-resistance for its Rtotal,
  // split that gap until the curve reaches far enough into the dark (or the gap is one
  // count). Without it a steep knee leaves the loudest steps unsolvable.
  for (uint8_t pass = 0; pass < SweepLadder::EDGE_MAX_PASSES; pass++) {
    uint16_t mids[MAX_CH];
    uint8_t nm = 0;
    for (uint8_t c = 0; c < na; c++) {
      for (uint8_t i = 0; i < ns; i++) { gEdgeDuty[i] = gSamples[i].duty; gEdgeOhms[i] = gSamples[i].ohms[c]; }
      uint16_t m;
      if (!SweepLadder::darkEdgeMid(gEdgeDuty, gEdgeOhms, ns, darkRefOhms(ch[c]), m)) continue;
      bool dup = false;
      for (uint8_t k = 0; k < nm; k++) if (mids[k] == m) dup = true;
      if (!dup) mids[nm++] = m;
    }
    if (nm == 0 || (uint16_t)ns + nm > MAX_SWEEP_POINTS) break;
    if (nm == 2 && mids[1] < mids[0]) { uint16_t t = mids[0]; mids[0] = mids[1]; mids[1] = t; }

    out.print(F("  -- dark edge, pass "));
    out.print(pass + 1);
    out.print(F(": "));
    out.print(nm);
    out.println(F(" point(s) between unreadable and the first reading --"));

    driveSwept(ch, na, wantSeries, mids[0]); // approach from the dark side, like every other point
    BusyHook::wait(REFINE_DARK_DWELL_MS);
    for (uint8_t k = 0; k < nm; k++) {
      unsigned long el;
      measurePoint(ch, na, wantSeries, mids[k], timeoutMs, gAdded[k], el);
      printPoint(out, gAdded[k], na, idx, labels, wantSeries, el, true);
      // insert in duty order
      uint8_t at = 0;
      while (at < ns && gSamples[at].duty < gAdded[k].duty) at++;
      for (int j = (int)ns; j > (int)at; j--) gSamples[j] = gSamples[j - 1];
      gSamples[at] = gAdded[k];
      ns++;
    }
  }

  for (uint8_t pass = 0; pass < REFINE_MAX_PASSES; pass++) {
    uint16_t mids[MAX_SWEEP_POINTS];
    uint8_t nm = 0;
    for (uint8_t i = 0; i + 1 < ns; i++) {
      if ((uint8_t)(ns + nm) >= MAX_SWEEP_POINTS) break;
      if (needsRefine(gSamples[i], gSamples[i + 1], na)) {
        mids[nm++] = (uint16_t)(((uint32_t)gSamples[i].duty + gSamples[i + 1].duty) / 2);
      }
    }
    if (nm == 0) break;

    out.print(F("  -- refining knee, pass "));
    out.print(pass + 1);
    out.print(F(": "));
    out.print(nm);
    out.println(F(" points --"));

    driveSwept(ch, na, wantSeries, gSamples[0].duty); // relax to dark first
    BusyHook::wait(REFINE_DARK_DWELL_MS);

    for (uint8_t k = 0; k < nm; k++) {
      unsigned long el;
      measurePoint(ch, na, wantSeries, mids[k], timeoutMs, gAdded[k], el);
      printPoint(out, gAdded[k], na, idx, labels, wantSeries, el, true);
    }

    uint8_t a = 0, b = 0, m = 0;
    while (a < ns || b < nm) {
      if (b >= nm || (a < ns && gSamples[a].duty < gAdded[b].duty)) gMerged[m++] = gSamples[a++];
      else                                                           gMerged[m++] = gAdded[b++];
    }
    for (uint8_t i = 0; i < m; i++) gSamples[i] = gMerged[i];
    ns = m;
  }

  uint8_t accepted[MAX_CH] = {0, 0};
  uint8_t rejected[MAX_CH] = {0, 0};
  for (uint8_t c = 0; c < na; c++) {
    for (uint8_t i = 0; i < ns; i++) {
      if (gSamples[i].ohms[c] <= 0.0f) continue;
      bool ok = wantSeries ? ch[c]->calFeedSeriesPoint(gSamples[i].duty, gSamples[i].ohms[c])
                           : ch[c]->calFeedShuntPoint(gSamples[i].duty, gSamples[i].ohms[c]);
      if (ok) accepted[c]++; else rejected[c]++;
    }
  }
  out.print(F("  curve points kept/rejected:"));
  for (uint8_t c = 0; c < na; c++) {
    out.print(F("  "));
    if (labels) { out.print(labels[idx[c]]); out.print('='); }
    out.print(accepted[c]);
    out.print('/');
    out.print(rejected[c]);
  }
  out.println();
}

} // namespace

void solveBoth(LDRVolume &left, LDRVolume &right, Stream &out,
               uint8_t &validLeft, uint8_t &validRight, uint8_t steps) {
  float cap = 0.0f;
  float a = left.computeMaxRangeDb(RANGE_SAFETY_MARGIN_DB);
  float b = right.computeMaxRangeDb(RANGE_SAFETY_MARGIN_DB);
  if (a > 0.0f && b > 0.0f) cap = (a < b) ? a : b;
  validLeft = left.calSolveStereo(cap, steps, out);
  validRight = right.calSolveStereo(cap, steps, out);
}

// ---------------------------------------------------------------------------
// The settled dark-end walk (DarkProfile.hpp): both channels at once, shunt held
// bright. Fills prof[c]; prints one line per point. The relay must be energized.
// ---------------------------------------------------------------------------
void walkDarkEnd(LDRVolume *const *ch, uint8_t na, const char *labels, DarkProfile *prof, Stream &out) {
  using namespace DarkWalk;
  const uint32_t FPC = DriverChannels::FINE_PER_COUNT;
  int32_t duty[MAX_CH], prevDuty[MAX_CH];
  float prevOhms[MAX_CH];
  bool active[MAX_CH], havePrev[MAX_CH];
  uint32_t loudSer[MAX_CH], loudShu[MAX_CH]; // the loud-passage state, per channel
  out.println(F("--- CAL: dark-end walk (series settled from the dark side; memory = 5 s after the top step) ---"));
  for (uint8_t c = 0; c < na; c++) {
    prof[c].clear();
    uint16_t d0 = 0;
    active[c] = ch[c]->seriesCurve().dutyForResistance(START_OHMS, d0) && d0 > APPROACH_COUNTS + 1;
    duty[c] = d0; havePrev[c] = false; prevDuty[c] = 0; prevOhms[c] = 0.0f;
    // The loudest real state: the transparent top step (series at TRANSPARENT_SERIES_OHMS,
    // shunt LED off).
    if (!ch[c]->seriesCurve().dutyForResistanceFine(TRANSPARENT_SERIES_OHMS, loudSer[c])) loudSer[c] = (uint32_t)CAL_BRIGHT_HOLD_DUTY * FPC;
    loudShu[c] = 0;
    ch[c]->driveElementFine(false, (uint32_t)CAL_BRIGHT_HOLD_DUTY * FPC);
    if (!active[c]) { out.print(F("  ")); out.print(labels ? labels[c] : '?'); out.println(F(": no starting point on the series curve -- walk skipped")); }
  }
  for (;;) {
    bool any = false;
    for (uint8_t c = 0; c < na; c++) any = any || active[c];
    if (!any) break;
    // 1. arrive from the dark side
    for (uint8_t c = 0; c < na; c++) if (active[c]) ch[c]->driveElementFine(true, (uint32_t)(duty[c] - APPROACH_COUNTS) * FPC);
    BusyHook::wait(APPROACH_MS);
    for (uint8_t c = 0; c < na; c++) if (active[c]) ch[c]->driveElementFine(true, (uint32_t)duty[c] * FPC);
    float ra[MAX_CH]; uint8_t secs[MAX_CH]; bool settled[MAX_CH];
    settleRead(ch, na, active, ra, secs, settled);
    // 2. memory: a loud passage, back, read once the accepted settling time has passed
    for (uint8_t c = 0; c < na; c++) {
      if (!active[c]) continue;
      ch[c]->driveElementFine(true, loudSer[c]);
      ch[c]->driveElementFine(false, loudShu[c]);
    }
    BusyHook::wait(LOUD_MS);
    for (uint8_t c = 0; c < na; c++) {
      if (!active[c]) continue;
      ch[c]->driveElementFine(false, (uint32_t)CAL_BRIGHT_HOLD_DUTY * FPC);
      ch[c]->driveElementFine(true, (uint32_t)duty[c] * FPC);
    }
    BusyHook::wait(RETURN_MS);
    // 3. record, decide, print
    for (uint8_t c = 0; c < na; c++) {
      if (!active[c]) continue;
      const float rb = readAvg3(ch[c]);
      DarkPoint d{};
      d.duty = (uint16_t)duty[c];
      d.settleS = secs[c];
      d.ohms = ra[c];
      d.memory = (ra[c] > 0.0f && rb > 0.0f) ? (ra[c] >= rb ? ra[c] / rb : rb / ra[c]) : 0.0f;
      const bool darker = !havePrev[c] || ra[c] > prevOhms[c] * MIN_RISE;
      d.ok = (settled[c] && ra[c] > 0.0f && rb > 0.0f && darker) ? 1 : 0;
      prof[c].add(d);
      out.print(F("  ")); out.print(labels ? labels[c] : '?');
      out.print(F(" duty ")); out.print(duty[c]);
      out.print(F(": ")); out.print(ra[c] / 1000.0f, 1);
      out.print(F("k settled in ")); out.print(secs[c]);
      out.print(F(" s, memory x")); out.print(d.memory, 3);
      if (!d.ok) out.print(settled[c] ? (darker ? F("  -- unreadable: end of walk") : F("  -- not darker than the last point: end of walk"))
                                      : F("  -- did not settle: end of walk"));
      out.println();
      if (!d.ok || ra[c] >= TOP_OHMS || prof[c].n >= DarkProfile::MAX_POINTS) { active[c] = false; continue; }
      const int32_t next = nextDuty(duty[c], ra[c], prevDuty[c], prevOhms[c], havePrev[c]);
      prevDuty[c] = duty[c]; prevOhms[c] = ra[c]; havePrev[c] = true;
      duty[c] = next;
      if (duty[c] <= (int32_t)APPROACH_COUNTS + 1) active[c] = false;
    }
  }
  for (uint8_t c = 0; c < na; c++) {
    out.print(F("  ")); out.print(labels ? labels[c] : '?');
    out.print(F(": settles reliably up to "));
    out.print(prof[c].deepestOkOhms() / 1000.0f, 1);
    out.println(F("k"));
  }
}

// ---------------------------------------------------------------------------
// The quiet-end series point, placed with SETTLED readings: every step whose series
// duty equals the quietest step's (they share one operating point) gets the result.
// Each probe arrives from one count darker; secant on the curve's settled slope.
// ---------------------------------------------------------------------------
void settleQuietPoint(LDRVolume *const *ch, uint8_t na, const char *labels, Stream &out) {
  const uint32_t FPC = DriverChannels::FINE_PER_COUNT;
  constexpr uint8_t MAX_PROBES = 6;
  constexpr float TOL = 1.015f;        // each channel within 1.5% of its target ...
  constexpr float PAIR_TOL = 1.015f;   // ... and the two within 1.5% of each other (~0.13 dB)
  constexpr uint32_t QUIET_HOLD_MS = 60000;
  int8_t s0[MAX_CH]; uint32_t oldF[MAX_CH], f[MAX_CH]; float target[MAX_CH], perCount[MAX_CH], got[MAX_CH], err[MAX_CH];
  bool active[MAX_CH], ok[MAX_CH]; uint8_t probes[MAX_CH] = {0, 0};
  out.println(F("--- quiet-end series point (settled seek, stereo-matched) ---"));
  for (uint8_t c = 0; c < na; c++) {
    s0[c] = ch[c]->lowestValidStep();
    active[c] = s0[c] >= 0;
    ok[c] = false; err[c] = 0.0f;
    if (!active[c]) continue;
    const VolumeLut::Entry &e = ch[c]->lut().step((uint8_t)s0[c]);
    oldF[c] = f[c] = e.seriesFine; target[c] = e.targetRs; got[c] = 0.0f;
    bool ex; perCount[c] = Capabilities::slopeAt(ch[c]->seriesCurve(), target[c], ex);
    if (!(perCount[c] > 0.01f)) perCount[c] = 0.25f;
    ch[c]->driveElementFine(false, e.shuntFine);
    ch[c]->driveElementFine(true, e.seriesFine);
  }
  // The trim ends at the loud steps; a cell needs a while to recover from that (R's
  // series cell well over a minute on the bench). Measure the quiet point as it is in
  // listening, not seconds after a loud passage.
  out.print(F("  holding the quiet step ")); out.print(QUIET_HOLD_MS / 1000); out.println(F(" s first"));
  BusyHook::wait(QUIET_HOLD_MS);
  for (uint8_t it = 0; it < MAX_PROBES; it++) {
    bool any = false;
    for (uint8_t c = 0; c < na; c++) any = any || active[c];
    if (!any) break;
    for (uint8_t c = 0; c < na; c++) if (active[c]) ch[c]->driveElementFine(true, f[c] > FPC ? f[c] - FPC : 0);
    BusyHook::wait(DarkWalk::APPROACH_MS);
    for (uint8_t c = 0; c < na; c++) if (active[c]) ch[c]->driveElementFine(true, f[c]);
    float r[MAX_CH]; uint8_t secs[MAX_CH]; bool st[MAX_CH];
    settleRead(ch, na, active, r, secs, st);
    uint32_t next[MAX_CH];
    for (uint8_t c = 0; c < na; c++) {
      next[c] = f[c];
      if (!active[c]) continue;
      probes[c]++;
      if (r[c] <= 0.0f) { active[c] = false; continue; }
      got[c] = r[c];
      err[c] = logf(r[c] / target[c]);
      ok[c] = fabsf(err[c]) <= logf(TOL);
      float step = err[c] / perCount[c];              // > 0: too dark -> brighter
      if (step > 3.0f) step = 3.0f;
      if (step < -3.0f) step = -3.0f;
      const int32_t nf = (int32_t)f[c] + (int32_t)lroundf(step * (float)FPC);
      next[c] = nf > (int32_t)FPC ? (uint32_t)nf : FPC;
    }
    // Done when every channel is within TOL and, as a pair, within PAIR_TOL of each
    // other: stereo matching first. Otherwise correct the channel(s) still off -- or,
    // when both are inside TOL but apart, the one further from its target.
    const bool pair = (na == 2 && s0[0] >= 0 && s0[1] >= 0);
    const bool allOk = (!pair || (ok[0] && ok[1])) && (pair || ok[0]);
    const bool matched = !pair || fabsf(err[0] - err[1]) <= logf(PAIR_TOL);
    if (allOk && matched) break;
    for (uint8_t c = 0; c < na; c++) {
      if (!active[c]) continue;
      const bool moveThis = !ok[c] || (allOk && !matched && fabsf(err[c]) >= fabsf(err[1 - c]));
      if (moveThis) f[c] = next[c];
    }
  }
  const float Lc = (float)FPC;
  for (uint8_t c = 0; c < na; c++) {
    if (s0[c] < 0) continue;
    out.print(F("  ")); out.print(labels ? labels[c] : '?');
    out.print(F(": seriesDuty ")); out.print(oldF[c] / Lc, 2); out.print(F("->")); out.print(f[c] / Lc, 2);
    out.print(F("  Rs ")); out.print(got[c] / 1000.0f, 1); out.print(F("k (target ")); out.print(target[c] / 1000.0f, 1);
    out.print(F("k, ")); out.print(probes[c]); out.print(F(" settled probes)"));
    const uint8_t shifted = ch[c]->applyQuietSeek(oldF[c], f[c]);
    if (shifted) {
      out.print(F("; knee shift "));
      out.print(((float)f[c] - (float)oldF[c]) / Lc, 2);
      out.print(F(" counts applied to "));
      out.print(shifted);
      out.print(F(" deeper-curve steps"));
    }
    out.println();
  }
  if (na == 2 && got[0] > 0.0f && got[1] > 0.0f) {
    // At the quiet steps the gain goes as 1/Rs: the series errors give the balance.
    const float rMinusL = 8.6859f * (err[1] - err[0]);
    out.print(F("  stereo match at the quiet point: R is "));
    out.print(fabsf(rMinusL), 2);
    out.println(rMinusL >= 0 ? F(" dB quieter than L") : F(" dB louder than L"));
  }
}

// ---------------------------------------------------------------------------
// Bright-end fingerprint of all four cells (CellCheck.hpp), both channels at once.
// The relay must be energized.
// ---------------------------------------------------------------------------
void takeFingerprint(LDRVolume *const *ch, uint8_t na, CellFingerprint *fp) {
  const uint32_t FPC = DriverChannels::FINE_PER_COUNT;
  const uint32_t bright = (uint32_t)CAL_BRIGHT_HOLD_DUTY * FPC;
  for (uint8_t c = 0; c < na; c++) fp[c].valid = 1;
  for (uint8_t e = 0; e < 2; e++) {
    const bool series = (e == 0);
    for (uint8_t c = 0; c < na; c++) ch[c]->driveElementFine(!series, bright);
    for (uint8_t i = 0; i < CellCheck::NFP; i++) {
      for (uint8_t c = 0; c < na; c++) ch[c]->driveElementFine(series, (uint32_t)CellCheck::DUTIES[i] * FPC);
      BusyHook::wait(CellCheck::SETTLE_MS);
      for (uint8_t c = 0; c < na; c++) {
        float rs, rsh;
        readAvg3Pair(ch[c], rs, rsh);
        const float v = series ? rs : rsh;
        if (series) fp[c].ser[i] = v; else fp[c].shu[i] = v;
        if (!(v > 0.0f)) fp[c].valid = 0;
      }
    }
  }
  const float t = Board::dieTempC();
  for (uint8_t c = 0; c < na; c++) fp[c].tempC = t;
}

// The cell check: a fresh fingerprint against the stored one, per channel.
CellState checkCells(LDRVolume *const *ch, uint8_t na, const char *labels, Stream &out) {
  CellFingerprint now[MAX_CH];
  takeFingerprint(ch, na, now);
  CellState all = CellState::MATCH;
  out.println(F("--- cell check: bright-end fingerprint vs the stored calibration ---"));
  for (uint8_t c = 0; c < na; c++) {
    const CellFingerprint &st = ch[c]->fingerprint();
    uint8_t e = 0, i = 0;
    const float w = CellCheck::worst(st, now[c], e, i);
    const CellState s = CellCheck::judge(true, st, now[c]);
    if (s != CellState::MATCH) all = CellState::CHANGED;
    out.print(F("  ")); out.print(labels ? labels[c] : '?');
    if (w <= 0.0f) {
      out.print(F(": a reading failed"));
    } else {
      out.print(F(": worst x")); out.print(w, 3);
      out.print(e ? F(" (shunt @") : F(" (series @")); out.print(CellCheck::DUTIES[i]); out.print(')');
    }
    out.print(F(", die ")); out.print(now[c].tempC, 1); out.print(F(" C now / ")); out.print(st.tempC, 1);
    out.print(F(" C at calibration -> "));
    out.println(s == CellState::MATCH ? F("MATCH") : F("CHANGED"));
  }
  return all;
}

// ---------------------------------------------------------------------------
// The single CAL (master, console, boot). See CellCheck.hpp for the plan.
// ---------------------------------------------------------------------------
CellState calibrate(LDRVolume &left, LDRVolume &right, const CalRequest &req, Stream &out, bool boot) {
  BusyHook::Scope busy;
  Board::CalHold ampHold(left.board());
  LDRVolume *ch[2] = {&left, &right};
  char lab[3] = {'L', 'R', 0};
  const bool haveData = left.hasUsableLut() && right.hasUsableLut() &&
                        left.fingerprint().valid && right.fingerprint().valid &&
                        left.darkProfile().okCount() && right.darkProfile().okCount();
  CellState cs = CellState::NO_DATA;
  if (haveData) {
    for (uint8_t c = 0; c < 2; c++) ch[c]->relayEnergize(true);
    cs = checkCells(ch, 2, lab, out);
  } else {
    out.println(F("CAL: no complete stored calibration (curves, dark-end profile, fingerprint) on both channels"));
  }
  // Same cells, but would the CURRENT firmware choose a different Rtotal from the stored
  // measurements (AUTO), or is a manual R now outside the range? Then re-solve -- the
  // measurements are still good, only the policy changed; no full scan needed.
  float policyR = 0.0f;
  if (cs == CellState::MATCH && !req.hasR && !req.rAuto) {
    using namespace Capabilities;
    const DividerMath::Load load{AUDIO_SOURCE_OHMS, AMP_INPUT_LOAD_OHMS};
    ChannelCaps cc[2];
    const LdrCurve *ser[2];
    for (uint8_t c = 0; c < 2; c++) {
      cc[c] = measure(ch[c]->seriesCurve(), ch[c]->shuntCurve(), &ch[c]->darkProfile());
      ser[c] = &ch[c]->seriesCurve();
    }
    const RtotalChoice rc = chooseRtotal(cc, ser, 2, left.rAuto() && right.rAuto(), left.rTotalOhms(), load,
                                         RANGE_SAFETY_MARGIN_DB, CAPS_DITHER_LEVELS);
    if (rc.rangeOk && fabsf(rc.chosen - left.rTotalOhms()) > 0.005f * left.rTotalOhms()) {
      policyR = rc.chosen;
      out.print(F("CAL: Rtotal "));
      out.print(left.rTotalOhms(), 0);
      out.print(F(" -> "));
      out.print(rc.chosen, 0);
      out.println(left.rAuto() ? F(" (AUTO, under this firmware's limits)") : F(" (manual R clamped into the range)"));
    }
  }
  if (boot && cs == CellState::MATCH && policyR <= 0.0f) {
    out.println(F("Boot: the cells match the stored calibration -- using it as saved."));
    for (uint8_t c = 0; c < 2; c++) ch[c]->releaseRelayToAudio();
    return cs;
  }
  CalAction a = CellCheck::plan(cs, req, left.rAuto() && right.rAuto());
  if (policyR > 0.0f) {
    a = CalAction::RESOLVE_RESYNC;
    const bool manual = !(left.rAuto() && right.rAuto());
    for (uint8_t c = 0; c < 2; c++) ch[c]->setRTotalOhms(policyR, manual);
  }
  if (req.hasR) {
    for (uint8_t c = 0; c < 2; c++) ch[c]->setRTotalOhms((float)req.ohms, true);
  } else if (req.rAuto || a == CalAction::FULL) {
    // A full characterization reveals RMIN/RMAX/AUTO afresh: use AUTO unless an R was asked for.
    for (uint8_t c = 0; c < 2; c++) ch[c]->setRAuto();
  }
  switch (a) {
    case CalAction::FULL:
      out.println(cs == CellState::NO_DATA ? F("CAL: full characterization (nothing usable stored)")
                                           : F("CAL: full characterization (the cells have CHANGED)"));
      characterizeBoth(left, right, out);
      break;
    case CalAction::RESYNC:
      out.println(F("CAL: cells match -- resync (trim + settled quiet point)"));
      touchUp(ch, 2, out, lab, CalKind::RESYNC);
      break;
    case CalAction::RESOLVE_RESYNC: {
      out.println(F("CAL: cells match -- new Rtotal, re-solved from the stored measurements, then resync"));
      if (req.rAuto) {
        using namespace Capabilities;
        const DividerMath::Load load{AUDIO_SOURCE_OHMS, AMP_INPUT_LOAD_OHMS};
        ChannelCaps cc[2];
        const LdrCurve *ser[2];
        for (uint8_t c = 0; c < 2; c++) {
          cc[c] = measure(ch[c]->seriesCurve(), ch[c]->shuntCurve(), &ch[c]->darkProfile());
          ser[c] = &ch[c]->seriesCurve();
        }
        const RtotalChoice rc = chooseRtotal(cc, ser, 2, true, left.rTotalOhms(), load, RANGE_SAFETY_MARGIN_DB, CAPS_DITHER_LEVELS);
        if (rc.rangeOk) for (uint8_t c = 0; c < 2; c++) { ch[c]->setRange(rc.rMin, rc.rMax); ch[c]->setRTotalOhms(rc.chosen, false); }
      }
      out.print(F("Rtotal used: ")); out.print(left.rTotalOhms(), 0);
      out.println(left.rAuto() ? F("  (AUTO)") : F("  (MANUAL)"));
      uint8_t vl = 0, vr = 0;
      solveBoth(left, right, out, vl, vr);
      touchUp(ch, 2, out, lab, CalKind::RESOLVE);
      break;
    }
  }
  return cs;
}

void touchUp(LDRVolume *ch[], uint8_t n, Stream &out, const char *labels, CalKind kind) {
  BusyHook::Scope busy;
  if (n == 0) return;
  Board::CalHold ampHold(ch[0]->board()); // amp muted for the whole run, restored after
  // Read at idle, before the relays/PWM start working, so the number is the
  // board's resting temperature rather than the die's self-heating.
  const uint32_t t0 = millis();
  Board::reportDieTemp(out, F("Die temp at start"));
  for (uint8_t c = 0; c < n; c++) ch[c]->relayEnergize(true);

  for (uint8_t c = 0; c < n; c++) {
    if (labels) { out.print(F("Channel ")); out.println(labels[c]); }
    ch[c]->trimLoop(out);
  }
  settleQuietPoint(ch, n, labels, out);
  for (uint8_t c = 0; c < n; c++) {
    CalMeta m = ch[c]->calMeta();
    m.resyncCount++;
    m.tempAtLast = Board::dieTempC();
    m.lastKind = (uint8_t)kind;
    ch[c]->setCalMeta(m);
  }
  for (uint8_t c = 0; c < n; c++) ch[c]->calSave();
  out.println(F("Trimmed calibration saved to flash."));

  // Back to each channel's CURRENT volume while the audio is still
  // disconnected, let the cells relax, then reconnect.
  for (uint8_t c = 0; c < n; c++) ch[c]->reapplyCurrentStepOrDefault();
  BusyHook::wait(CAL_RELAX_BEFORE_RECONNECT_MS);
  for (uint8_t c = 0; c < n; c++) ch[c]->relayEnergize(false);
  Board::reportDieTemp(out, F("Die temp at end"), millis() - t0);
}

void characterize(LDRVolume *channels[], uint8_t count, Stream &out, const char *labels) {
  BusyHook::Scope busy; // a long operation: keep the links answering "busy" throughout
  if (count == 0) return;
  Board::CalHold ampHold(channels[0]->board()); // amp muted for the whole calibration (boot one included), restored after
  LDRVolume *act[MAX_CH];
  uint8_t idx[MAX_CH];
  uint8_t na = 0;
  for (uint8_t c = 0; c < count && c < MAX_CH; c++) {
    if (channels[c]->adsPresent()) {
      act[na] = channels[c];
      idx[na] = c;
      na++;
    } else {
      out.print(F("CAL: no ADS1115 detected on "));
      if (labels) { out.print(labels[c]); out.print(F(" channel")); }
      else        { out.print(F("this channel's bus")); }
      out.println(F(" -- skipping it"));
    }
  }
  if (na == 0) return; // nothing touched

  const uint16_t *pts = FULL_PCT;
  const uint8_t n = (uint8_t)(sizeof(FULL_PCT) / sizeof(FULL_PCT[0]));
  const unsigned long timeoutMs = 500;

  const uint32_t t0 = millis();
  Board::reportDieTemp(out, F("Die temp at start"));
  for (uint8_t c = 0; c < na; c++) act[c]->calBegin(); // energize relay, clear curves

  out.println(F("--- CAL: series sweep (shunt held bright) ---"));
  sweepPhase(act, na, idx, labels, true, pts, n, timeoutMs, out);
  out.println(F("--- CAL: shunt sweep (series held bright) ---"));
  sweepPhase(act, na, idx, labels, false, pts, n, timeoutMs, out);

  // Labels by position, for the routines below.
  char lab[MAX_CH + 1] = {0, 0, 0};
  for (uint8_t c = 0; c < na; c++) lab[c] = labels ? labels[idx[c]] : '?';

  // The settled dark-end walk: what the series cells can REALLY do deep in the dark.
  // Its points replace the sweep's lag-biased dark end, so everything after this --
  // the range, the AUTO choice, the LUT solve, DIAG, the range recomputed at boot --
  // works from settled measurements.
  {
    DarkProfile prof[MAX_CH];
    walkDarkEnd(act, na, lab, prof, out);
    for (uint8_t c = 0; c < na; c++) {
      if (!act[c]->adoptDarkProfile(prof[c])) {
        act[c]->setDarkProfile(prof[c]);
        out.print(F("WARN ")); out.print(lab[c]);
        out.println(F(": dark-end walk unusable -- series curve kept as swept; no Rtotal will qualify for AUTO"));
      }
    }
  }
  // The cells' bright-end fingerprint, taken the same way a later CAL checks it.
  {
    CellFingerprint fp[MAX_CH];
    takeFingerprint(act, na, fp);
    for (uint8_t c = 0; c < na; c++) act[c]->setFingerprint(fp[c]);
    out.println(F("--- cell fingerprint stored (bright end, for the next CAL's cell check) ---"));
  }

  // Rtotal. The curves just measured do not depend on Rtotal, so this is where the
  // range this hardware supports is known -- and where AUTO picks its value (or a
  // manual R that was valid when requested gets clamped if the cells moved since).
  {
    using namespace Capabilities;
    const DividerMath::Load load{AUDIO_SOURCE_OHMS, AMP_INPUT_LOAD_OHMS};
    ChannelCaps cc[MAX_CH];
    const LdrCurve *ser[MAX_CH];
    bool autoMode = false;
    for (uint8_t c = 0; c < na; c++) {
      cc[c] = measure(act[c]->seriesCurve(), act[c]->shuntCurve(), &act[c]->darkProfile());
      ser[c] = &act[c]->seriesCurve();
      if (act[c]->rAuto()) autoMode = true;
    }
    const RtotalChoice rc = chooseRtotal(cc, ser, na, autoMode, act[0]->rTotalOhms(), load,
                                         RANGE_SAFETY_MARGIN_DB, CAPS_DITHER_LEVELS);
    if (rc.rangeOk) {
      out.print(F("Rtotal range on this hardware: RMIN="));
      out.print(rc.rMin, 0);
      out.print(F(" RMAX="));
      out.print(rc.rMax, 0);
      out.print(F("  (curves reach "));
      out.print(rc.reach, 0);
      out.print(F("; AUTO pick "));
      out.print(rc.autoPick, 0);
      out.println(F(")"));
      out.print(F("Rtotal used: "));
      out.print(rc.chosen, 0);
      if (autoMode) out.println(rc.fallback ? F("  (AUTO: nothing met the limits -- fell back to RMIN)") : F("  (AUTO)"));
      else out.println(rc.clamped ? F("  (MANUAL, clamped into the newly measured range)") : F("  (MANUAL)"));
      for (uint8_t c = 0; c < na; c++) {
        act[c]->setRange(rc.rMin, rc.rMax);
        act[c]->setRTotalOhms(rc.chosen, !autoMode);
      }
    } else {
      out.print(F("WARN: the curves do not support a usable Rtotal range -- keeping Rtotal="));
      out.println(act[0]->rTotalOhms(), 0);
    }
  }

  uint8_t valid[MAX_CH] = {0, 0};
  if (na == 2) {
    solveBoth(*act[0], *act[1], out, valid[0], valid[1]);
  } else {
    valid[0] = act[0]->calSolve(NUM_VOLUME_STEPS_DEFAULT, out);
  }

  out.print(F("--- CAL: solved "));
  for (uint8_t c = 0; c < na; c++) {
    if (labels) { out.print(labels[idx[c]]); out.print(' '); }
    out.print(valid[c]);
    out.print('/');
    out.print(NUM_VOLUME_STEPS_DEFAULT);
    if (c + 1 < na) out.print(F(", "));
  }
  out.println(F(" ---"));

  // Trim. The sweep approaches every point from the dark side, so its LUT
  // is systematically 2-3 duty counts (~25% in resistance at the knee)
  // off; a calibration isn't finished until it has been trimmed against
  // live measurements. The relay is still energized here. Saved once, after
  // the trim: flash keeps the previous calibration until then.
  for (uint8_t c = 0; c < na; c++) {
    if (labels) { out.print(F("Channel ")); out.println(labels[idx[c]]); }
    act[c]->trimLoop(out);
  }
  // The quiet-end series point (shared by the quietest steps), placed with settled
  // readings -- the quick trim above leaves the deep series duties alone.
  settleQuietPoint(act, na, lab, out);
  for (uint8_t c = 0; c < na; c++) {
    CalMeta m = act[c]->calMeta();
    m.fullCount++;
    m.resyncCount = 0;
    m.tempAtFull = m.tempAtLast = Board::dieTempC();
    m.lastKind = (uint8_t)CalKind::FULL;
    act[c]->setCalMeta(m);
  }
  for (uint8_t c = 0; c < na; c++) {
    bool saved = act[c]->calSave();
    out.print(saved ? F("Saved") : F("WARN: failed to save"));
    if (labels) { out.print(F(" ")); out.print(labels[idx[c]]); }
    out.println();
  }

  // Land on a defined step and let the cells relax from the sweep's bright
  // end WHILE the audio path is still disconnected, then reconnect.
  for (uint8_t c = 0; c < na; c++) act[c]->applyDefinedStartupState();
  BusyHook::wait(CAL_RELAX_BEFORE_RECONNECT_MS);
  for (uint8_t c = 0; c < na; c++) act[c]->relayEnergize(false);
  Board::reportDieTemp(out, F("Die temp at end"), millis() - t0);
}

void characterizeBoth(LDRVolume &left, LDRVolume &right, Stream &out) {
  LDRVolume *both[2] = {&left, &right};
  characterize(both, 2, out, "LR");
}

} // namespace DualCalibration
