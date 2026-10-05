#include "DualCalibration.hpp"
#include "DriverChannels.hpp"
#include "Config.hpp"
#include "BusyHook.hpp"
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
Sample gSamples[MAX_SWEEP_POINTS];
Sample gAdded[MAX_SWEEP_POINTS];
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
  for (uint8_t c = 0; c < na; c++) {
    if (wantSeries) ch[c]->calDriveSeries(duty); else ch[c]->calDriveShunt(duty);
  }
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

// A stray CR/LF left over from the command that started this is not an
// abort request; any other character is.
bool abortRequested(Stream &out) {
  if (!out.available()) return false;
  char c = out.read();
  return (c != '\r' && c != '\n');
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
// Returns false if the user aborted.
bool sweepPhase(LDRVolume *ch[], uint8_t na, const uint8_t *idx, const char *labels,
                bool wantSeries, const uint16_t *pts, uint8_t n,
                unsigned long timeoutMs, Stream &out) {
  for (uint8_t c = 0; c < na; c++) {
    if (wantSeries) ch[c]->calDriveShunt(DriverChannels::WRAP);
    else            ch[c]->calDriveSeries(DriverChannels::WRAP);
  }

  uint8_t ns = 0;
  for (uint8_t i = 0; i < n && ns < MAX_SWEEP_POINTS; i++) {
    uint16_t duty = (uint16_t)(((uint32_t)pts[i] * DriverChannels::WRAP) / 100);
    unsigned long el;
    measurePoint(ch, na, wantSeries, duty, timeoutMs, gSamples[ns], el);
    printPoint(out, gSamples[ns], na, idx, labels, wantSeries, el, false);
    ns++;
    if (abortRequested(out)) { out.println(F("CALAUTO aborted.")); return false; }
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
      if (abortRequested(out)) { out.println(F("CALAUTO aborted.")); return false; }
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
  return true;
}

void finishAborted(LDRVolume *ch[], uint8_t na) {
  // A partial sweep still drove raw duty before bailing out. Land each
  // channel on a defined step and let the cells relax WHILE the audio path
  // is still disconnected, then reconnect (the old order reconnected first
  // and fixed the duty afterwards).
  for (uint8_t c = 0; c < na; c++) ch[c]->applyDefinedStartupState();
  BusyHook::wait(CAL_RELAX_BEFORE_RECONNECT_MS);
  for (uint8_t c = 0; c < na; c++) ch[c]->relayEnergize(false);
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

void touchUp(LDRVolume *ch[], uint8_t n, Stream &out, const char *labels) {
  for (uint8_t c = 0; c < n; c++) ch[c]->relayEnergize(true);

  bool completed = true;
  for (uint8_t c = 0; c < n && completed; c++) {
    if (labels) { out.print(F("Channel ")); out.println(labels[c]); }
    completed = ch[c]->trimLoop(out);
  }
  if (completed) {
    for (uint8_t c = 0; c < n; c++) ch[c]->calSave();
    out.println(F("Trimmed calibration saved to flash."));
  } else {
    // Keep RAM and flash consistent: drop the half-trimmed LUT.
    for (uint8_t c = 0; c < n; c++) ch[c]->calLoad();
    out.println(F("Trim aborted -- keeping the previously saved calibration."));
  }

  // Back to each channel's CURRENT volume while the audio is still
  // disconnected, let the cells relax, then reconnect.
  for (uint8_t c = 0; c < n; c++) ch[c]->reapplyCurrentStepOrDefault();
  BusyHook::wait(CAL_RELAX_BEFORE_RECONNECT_MS);
  for (uint8_t c = 0; c < n; c++) ch[c]->relayEnergize(false);
}

void equalizeRanges(LDRVolume &left, LDRVolume &right, Stream &out) {
  bool haveL = !left.seriesCurve().empty() && !left.shuntCurve().empty();
  bool haveR = !right.seriesCurve().empty() && !right.shuntCurve().empty();
  if (!haveL || !haveR) return;

  float a = left.computeMaxRangeDb(RANGE_SAFETY_MARGIN_DB);
  float b = right.computeMaxRangeDb(RANGE_SAFETY_MARGIN_DB);
  if (a <= 0.0f || b <= 0.0f) return;
  float cap = (a < b) ? a : b;

  // Only re-solve a channel whose range actually differs: a re-solve throws
  // away that channel's trimmed duties.
  LDRVolume *redo[2];
  uint8_t nr = 0;
  if (fabsf(left.rangeDb() - cap) > 0.05f)  { left.calSolveStereo(cap, NUM_VOLUME_STEPS_DEFAULT, out);  left.calSave();  redo[nr++] = &left; }
  if (fabsf(right.rangeDb() - cap) > 0.05f) { right.calSolveStereo(cap, NUM_VOLUME_STEPS_DEFAULT, out); right.calSave(); redo[nr++] = &right; }
  if (nr == 0) return;

  LDRVolume *trimSet[2];
  uint8_t nt = 0;
  for (uint8_t i = 0; i < nr; i++) {
    if (redo[i]->adsPresent()) trimSet[nt++] = redo[i];
    else redo[i]->reapplyCurrentStepOrDefault();
  }
  if (nt) {
    out.println(F("Ranges re-equalized -- trimming the re-solved channel(s)."));
    touchUp(trimSet, nt, out, nullptr);
  }
}

void runChannels(LDRVolume *channels[], uint8_t count, LDRVolume::CalMode mode,
                 Stream &out, const char *labels) {
  LDRVolume *act[MAX_CH];
  uint8_t idx[MAX_CH];
  uint8_t na = 0;
  for (uint8_t c = 0; c < count && c < MAX_CH; c++) {
    if (channels[c]->adsPresent()) {
      act[na] = channels[c];
      idx[na] = c;
      na++;
    } else {
      out.print(F("CALAUTO: no ADS1115 detected on "));
      if (labels) { out.print(labels[c]); out.print(F(" channel")); }
      else        { out.print(F("this channel's bus")); }
      out.println(F(" -- skipping it"));
    }
  }
  if (na == 0) return; // nothing touched

  if (mode == LDRVolume::CalMode::FAST) {
    bool allUsable = true;
    for (uint8_t c = 0; c < na; c++) {
      if (!act[c]->hasUsableLut()) allUsable = false;
    }
    if (allUsable) {
      out.println(F("--- CAL FAST: re-measuring and trimming the existing calibration (no new sweep) ---"));
      // touchUp() labels by position, so hand it letters in the same order.
      char lab[MAX_CH + 1] = {0, 0, 0};
      for (uint8_t c = 0; c < na; c++) lab[c] = labels ? labels[idx[c]] : '?';
      touchUp(act, na, out, labels ? lab : nullptr);
      return;
    }
    out.println(F("CAL FAST: no usable calibration on at least one channel -- running a FULL calibration instead"));
  }

  const uint16_t *pts = FULL_PCT;
  const uint8_t n = (uint8_t)(sizeof(FULL_PCT) / sizeof(FULL_PCT[0]));
  const unsigned long timeoutMs = 500;

  for (uint8_t c = 0; c < na; c++) act[c]->calBegin(); // energize relay, clear curves

  out.println(F("--- CALAUTO: series sweep (shunt held bright) ---"));
  if (!sweepPhase(act, na, idx, labels, true, pts, n, timeoutMs, out)) {
    finishAborted(act, na);
    return;
  }
  out.println(F("--- CALAUTO: shunt sweep (series held bright) ---"));
  if (!sweepPhase(act, na, idx, labels, false, pts, n, timeoutMs, out)) {
    finishAborted(act, na);
    return;
  }

  uint8_t valid[MAX_CH] = {0, 0};
  if (na == 2) {
    solveBoth(*act[0], *act[1], out, valid[0], valid[1]);
  } else {
    valid[0] = act[0]->calSolve(NUM_VOLUME_STEPS_DEFAULT, out);
  }

  out.print(F("--- CALAUTO done: solved "));
  for (uint8_t c = 0; c < na; c++) {
    if (labels) { out.print(labels[idx[c]]); out.print(' '); }
    out.print(valid[c]);
    out.print('/');
    out.print(NUM_VOLUME_STEPS_DEFAULT);
    if (c + 1 < na) out.print(F(", "));
  }
  out.println(F(" ---"));

  // Save the solved LUT first, so an abort during the trim below still
  // leaves a usable calibration on flash.
  for (uint8_t c = 0; c < na; c++) {
    bool saved = act[c]->calSave();
    out.print(saved ? F("Saved") : F("WARN: failed to save"));
    if (labels) { out.print(F(" ")); out.print(labels[idx[c]]); }
    out.println();
  }

  // Trim. The sweep approaches every point from the dark side, so its LUT
  // is systematically 2-3 duty counts (~25% in resistance at the knee)
  // off; a calibration isn't finished until it has been trimmed against
  // live measurements. The relay is still energized here.
  bool trimmed = true;
  for (uint8_t c = 0; c < na && trimmed; c++) {
    if (labels) { out.print(F("Channel ")); out.println(labels[idx[c]]); }
    trimmed = act[c]->trimLoop(out);
  }
  if (trimmed) {
    for (uint8_t c = 0; c < na; c++) act[c]->calSave();
    out.println(F("Trimmed calibration saved to flash."));
  } else {
    for (uint8_t c = 0; c < na; c++) act[c]->calLoad(); // RAM back in step with flash
    out.println(F("Trim aborted -- keeping the untrimmed (saved) calibration."));
  }

  // Land on a defined step and let the cells relax from the sweep's bright
  // end WHILE the audio path is still disconnected, then reconnect.
  for (uint8_t c = 0; c < na; c++) act[c]->applyDefinedStartupState();
  BusyHook::wait(CAL_RELAX_BEFORE_RECONNECT_MS);
  for (uint8_t c = 0; c < na; c++) act[c]->relayEnergize(false);
}

void runBoth(LDRVolume &left, LDRVolume &right, LDRVolume::CalMode mode, Stream &out) {
  LDRVolume *both[2] = {&left, &right};
  runChannels(both, 2, mode, out, "LR");
}

} // namespace DualCalibration