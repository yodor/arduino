#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "Calibration.hpp"
#include "DarkProfile.hpp" // LdrCurve
#include "Config.hpp"
#include "DividerMath.hpp"

// ============================================================================
// Capabilities -- what the MEASURED cells say each Rtotal would give, and from
// that RMIN, RMAX and the AUTO choice a full characterization makes.
//
// Pure functions over the curves and each series cell's DarkProfile (no
// hardware, no state), tested on the host. The curves do not depend on Rtotal,
// so every number here is "what if the LUT were solved for R":
//   * depth, loud-end impedance and max gain: exact arithmetic on the measured
//     floors (the same floor definition computeMaxRangeDb uses);
//   * step size at the quietest step: the series curve's slope there -- after a
//     full characterization the dark end of that curve IS the settled walk;
//   * memory: how far the quiet level sits off after a loud passage, once settled,
//     measured by the dark-end walk on each channel; deeper than the deepest
//     settled point it is unknown, and no Rtotal there qualifies.
// The limits applied to these numbers (CAPS_* in Config.hpp) are policy.
// ============================================================================
namespace Capabilities {

inline float medianOf(float *a, uint8_t n) {
  for (uint8_t i = 1; i < n; i++) {           // tiny insertion sort
    float v = a[i]; int8_t j = (int8_t)i - 1;
    while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
    a[j + 1] = v;
  }
  return n ? a[n / 2] : 0.0f;
}

// ln(R) lost per PWM count between point i and i+1 (positive: R falls with duty).
inline float intervalSlope(const LdrCurve &c, uint8_t i) {
  const LdrCurve::Point &a = c.point(i), &b = c.point(i + 1);
  if (b.duty <= a.duty || a.ohms <= 0.0f || b.ohms <= 0.0f) return 0.0f;
  return (logf(a.ohms) - logf(b.ohms)) / (float)(b.duty - a.duty);
}

// Index of the first point (darkest first) of the curve's FINE-SPACED region, or -1. Points
// before it sit in a sparse dark tail (gap above CAPS_LAG_MAX_GAP counts: the coarse list's
// dark points, or readings taken before the cell left deep dark) and are ignored.
inline int fineStartIndex(const LdrCurve &c) {
  const uint8_t n = c.count();
  for (uint8_t i = 0; i + 3 < n; i++) {
    if ((int)c.point(i + 1).duty - (int)c.point(i).duty <= (int)CAPS_LAG_MAX_GAP) return (int)i;
  }
  return -1;
}

// The first fine interval is much steeper than the next one. That is what a cell that had
// not finished relaxing looks like -- but it is ALSO what a genuinely sharp knee looks like
// (the 1M-bleed driver's knee is exactly that), so this is a FLAG on the reported numbers,
// never a reason to discard the points.
inline bool firstIntervalLagSuspect(const LdrCurve &c) {
  int i = fineStartIndex(c);
  if (i < 0) return false;
  const float s0 = intervalSlope(c, (uint8_t)i), s1 = intervalSlope(c, (uint8_t)(i + 1));
  return s1 > 0.0f && s0 > CAPS_LAG_SLOPE_FACTOR * s1;
}

// Resistance at the darkest fine-spaced point; 0 if the curve has none.
inline float reliableTop(const LdrCurve &c) {
  int i = fineStartIndex(c);
  return i < 0 ? 0.0f : c.point((uint8_t)i).ohms;
}

// Slope used beyond the darkest point: median of the next few intervals (skipping the
// possibly-lag first one). Extrapolation into the dark is a guess -- callers flag it.
inline float kneeSlope(const LdrCurve &c) {
  int i = fineStartIndex(c);
  if (i < 0) return 0.0f;
  float s[4]; uint8_t m = 0;
  for (uint8_t j = (uint8_t)(i + 1); j + 1 < c.count() && m < 4; j++) s[m++] = intervalSlope(c, j);
  return medianOf(s, m);
}

// ln(R) per count at resistance `ohms`: the curve's own interval slope where it has data,
// the knee slope beyond its darkest point (extrapolated = true).
inline float slopeAt(const LdrCurve &c, float ohms, bool &extrapolated) {
  extrapolated = false;
  int i = fineStartIndex(c);
  if (i < 0) return 0.0f;
  if (ohms > c.point((uint8_t)i).ohms) { extrapolated = true; return kneeSlope(c); }
  for (uint8_t k = (uint8_t)i; k + 1 < c.count(); k++) {
    if (c.point(k).ohms >= ohms && ohms >= c.point(k + 1).ohms) return intervalSlope(c, k);
  }
  return intervalSlope(c, c.count() - 2); // below the lowest point: use the last interval
}

// What one channel's curves say, independent of any Rtotal.
struct ChannelCaps {
  bool ok = false;          // both curves usable
  float rsFloor = 0, rshFloor = 0;   // lowest resistance (highest-duty point), as computeMaxRangeDb takes it
  float serTop = 0, shuTop = 0;      // darkest fine-spaced resistance on each curve
  bool serLag = false;               // the series curve's first fine interval is lag-suspect (flag only)
  float serLagBelow = 0;             // ...and it spans (serLagBelow, serTop]
  const DarkProfile *prof = nullptr; // the series cell's MEASURED dark end (null/empty = not measured)
  float supportedMax() const { return serTop < shuTop ? serTop : shuTop; }
  // How deep this channel was MEASURED to work: the deepest settled point of its walk
  // (0 = nothing measured -> no Rtotal qualifies for AUTO or RMAX).
  float measuredMax() const { return prof ? prof->deepestOkOhms() : 0.0f; }
};

// Measured memory of this channel's series cell at resistance r (level after a loud
// passage vs settled); false = unknown (no profile, or deeper than the deepest settled point).
inline bool memoryFor(const ChannelCaps &c, float r, float &h) {
  return c.prof && c.prof->memoryAt(r, h);
}

inline ChannelCaps measure(const LdrCurve &series, const LdrCurve &shunt, const DarkProfile *prof = nullptr) {
  ChannelCaps c;
  if (series.empty() || shunt.empty()) return c;
  c.rsFloor = series.point(series.count() - 1).ohms;
  c.rshFloor = shunt.point(shunt.count() - 1).ohms;
  c.serTop = reliableTop(series);
  c.shuTop = reliableTop(shunt);
  c.serLag = firstIntervalLagSuspect(series);
  { int i = fineStartIndex(series); c.serLagBelow = (i >= 0) ? series.point((uint8_t)(i + 1)).ohms : 0.0f; }
  c.ok = c.rsFloor > 0 && c.rshFloor > 0 && c.serTop > 0 && c.shuTop > 0;
  c.prof = prof;
  return c;
}

inline float depthDb(const ChannelCaps &c, float r, const DividerMath::Load &load, float marginDb) {
  if (r <= c.rshFloor || r <= c.rsFloor) return 0.0f;
  float kMin = DividerMath::gain(r - c.rshFloor, c.rshFloor, load);
  float kMax = DividerMath::gain(c.rsFloor, r - c.rsFloor, load);
  if (kMin <= 0.0f || kMax <= kMin) return 0.0f;
  return 20.0f * log10f(kMax / kMin) - marginDb;
}

// Input impedance the source sees at the loudest step (series at its floor).
inline float loudZinOhms(const ChannelCaps &c, float r, const DividerMath::Load &load) {
  float rsh = r - c.rsFloor;
  float p = (load.loadOhms > 0.0f) ? DividerMath::parallel(rsh, load.loadOhms) : rsh;
  return p + c.rsFloor;
}

struct Row {
  float rTotal, depthDb, zinOhms, maxGainDb;
  float slope;            // ln(R) per count of the series cell at the quiet end
  bool slopeExtrap;
  float quantDb;          // worst-case +-half-step gain error at the quietest step
  float memory; bool memoryUnknown; // measured memory at the quiet-end series point; unknown = not measured there
  bool supported;         // both curves reach this resistance with fine-spaced points
  bool lagFlag;           // the quiet-end series point lies in a lag-suspect first interval
};

inline Row evaluate(const ChannelCaps &c, const LdrCurve &series, float r, const DividerMath::Load &load,
                    float marginDb, float ditherLevels) {
  Row w{};
  w.rTotal = r;
  w.depthDb = depthDb(c, r, load, marginDb);
  w.zinOhms = loudZinOhms(c, r, load);
  w.maxGainDb = 20.0f * log10f(DividerMath::gain(c.rsFloor, r - c.rsFloor, load));
  w.slope = slopeAt(series, r - c.rshFloor, w.slopeExtrap);
  const float rs = r - c.rshFloor;
  const float p = (load.loadOhms > 0.0f) ? DividerMath::parallel(c.rshFloor, load.loadOhms) : c.rshFloor;
  const float aS = rs / (load.srcOhms + rs + p);        // how much of a series change reaches the gain
  w.quantDb = 0.5f * 8.686f * aS * w.slope / (ditherLevels < 1.0f ? 1.0f : ditherLevels);
  { float h; w.memoryUnknown = !memoryFor(c, rs, h); w.memory = w.memoryUnknown ? 0.0f : h; } // measured, or unknown
  w.supported = (r <= c.serTop) && (r <= c.shuTop);
  w.lagFlag = c.serLag && rs > c.serLagBelow && rs <= c.serTop;
  return w;
}

// Smallest Rtotal (500 ohm grid) whose loud-end input impedance reaches CAPS_MIN_ZIN_OHMS
// on every channel; 0 if none below 1 MOhm.
// The deepest resistance MEASURED to work on every channel (0 if any was not measured).
inline float measuredMaxAll(const ChannelCaps *ch, uint8_t n) {
  float m = 0.0f;
  for (uint8_t i = 0; i < n; i++) {
    const float v = ch[i].measuredMax();
    if (v <= 0.0f) return 0.0f;
    if (i == 0 || v < m) m = v;
  }
  return m;
}

inline float rMinOhms(const ChannelCaps *ch, uint8_t n, const DividerMath::Load &load) {
  for (float r = 1000.0f; r < 1.0e6f; r += 500.0f) {
    bool ok = true;
    for (uint8_t i = 0; i < n; i++) {
      if (r <= ch[i].rshFloor || r <= ch[i].rsFloor || loudZinOhms(ch[i], r, load) < CAPS_MIN_ZIN_OHMS) ok = false;
    }
    if (ok) return r;
  }
  return 0.0f;
}

// Largest Rtotal every channel's curves support (500 ohm grid, rounded down).
inline float supportedMaxOhms(const ChannelCaps *ch, uint8_t n) {
  float m = 1.0e9f;
  for (uint8_t i = 0; i < n; i++) { float s = ch[i].supportedMax(); if (s < m) m = s; }
  return floorf(m / 500.0f) * 500.0f;
}

// The deepest Rtotal inside [rMin, measured reach] whose measured memory and worst-case
// step error stay under the CAPS_AUTO_* limits on every channel. 0 if none qualifies.
inline float autoPickOhms(const ChannelCaps *ch, const LdrCurve *const *series, uint8_t n,
                          const DividerMath::Load &load, float marginDb, float ditherLevels) {
  const float lo = rMinOhms(ch, n, load);
  float hi = supportedMaxOhms(ch, n);
  { const float m = measuredMaxAll(ch, n); if (hi > m) hi = m; } // never deeper than MEASURED on every channel
  if (lo <= 0.0f || hi < lo) return 0.0f;
  for (float r = hi; r >= lo; r -= 500.0f) {
    bool ok = true;
    for (uint8_t i = 0; i < n && ok; i++) {
      Row w = evaluate(ch[i], *series[i], r, load, marginDb, ditherLevels);
      if (w.memoryUnknown || w.memory > CAPS_AUTO_MAX_MEMORY || w.quantDb > CAPS_AUTO_MAX_QUANT_DB || w.slope > CAPS_AUTO_MAX_SLOPE) ok = false;
    }
    if (ok) return r;
  }
  return 0.0f;
}

// ---------------------------------------------------------------------------
// The Rtotal decision a full calibration makes after its sweeps (the curves do not
// depend on Rtotal, so this is pure arithmetic on what was just measured).
//   range  = [rMinOhms(), usableMaxOhms()] across the channels (stereo-common): how far the
//            curves reach, cut back to where the RMAX limits still hold;
//   AUTO   -> autoPickOhms(); if nothing meets the limits, fall back to RMIN (the most
//             repeatable choice) and say so;
//   MANUAL -> the requested value, clamped into the NEW range if the cells moved since
//             the request was checked (and say so).
// rangeOk is false when the curves do not support a sensible range (unusable curves, or
// RMIN above RMAX); the caller then keeps its current Rtotal.
// ---------------------------------------------------------------------------
struct RtotalChoice {
  bool rangeOk = false;
  float rMin = 0, rMax = 0, autoPick = 0, chosen = 0;
  float reach = 0;         // how far the curves reach (before the usability limits)
  bool fallback = false;   // AUTO found nothing within the limits
  bool clamped = false;    // MANUAL request moved into the new range
};

// The deepest Rtotal (500 ohm grid) at or below the measured reach whose measured memory and
// worst-case step error stay within the RMAX limits on every channel; 0 if none.
inline float usableMaxOhms(const ChannelCaps *ch, const LdrCurve *const *series, uint8_t n,
                           const DividerMath::Load &load, float marginDb, float ditherLevels) {
  const float lo = rMinOhms(ch, n, load);
  float hi = supportedMaxOhms(ch, n);
  { const float m = measuredMaxAll(ch, n); if (hi > m) hi = m; } // never deeper than MEASURED on every channel
  if (lo <= 0.0f || hi < lo) return 0.0f;
  for (float r = hi; r >= lo; r -= 500.0f) {
    bool ok = true;
    for (uint8_t i = 0; i < n && ok; i++) {
      Row w = evaluate(ch[i], *series[i], r, load, marginDb, ditherLevels);
      if (w.memoryUnknown || w.memory > CAPS_RMAX_MAX_MEMORY || w.quantDb > CAPS_RMAX_MAX_QUANT_DB || w.slope > CAPS_RMAX_MAX_SLOPE) ok = false;
    }
    if (ok) return r;
  }
  return 0.0f;
}

inline RtotalChoice chooseRtotal(const ChannelCaps *cc, const LdrCurve *const *series, uint8_t n,
                                 bool autoMode, float requested, const DividerMath::Load &load,
                                 float marginDb, float ditherLevels) {
  RtotalChoice r;
  for (uint8_t i = 0; i < n; i++) if (!cc[i].ok) return r;
  r.rMin = rMinOhms(cc, n, load);
  r.reach = supportedMaxOhms(cc, n);
  r.rMax = usableMaxOhms(cc, series, n, load, marginDb, ditherLevels);
  if (r.rMin <= 0.0f || r.rMax < r.rMin) return r;
  r.rangeOk = true;
  r.autoPick = autoPickOhms(cc, series, n, load, marginDb, ditherLevels);
  if (autoMode) {
    if (r.autoPick > 0.0f) r.chosen = r.autoPick;
    else { r.chosen = r.rMin; r.fallback = true; }
  } else {
    r.chosen = requested;
    if (r.chosen < r.rMin) { r.chosen = r.rMin; r.clamped = true; }
    if (r.chosen > r.rMax) { r.chosen = r.rMax; r.clamped = true; }
  }
  return r;
}

// ---------------------------------------------------------------------------
// Text without printf's float conversions. The Pico core links newlib-nano, where
// snprintf("%f") silently produces NOTHING (no error, just an empty field) -- so no
// float ever goes through a format string in this firmware. Numbers are formatted
// here with integer arithmetic; the host test wraps snprintf and fails on any float
// conversion, so the host catches what the board would silently drop.
// ---------------------------------------------------------------------------
inline void fmtFixed(char *buf, size_t n, double v, uint8_t decimals) {
  if (n == 0) return;
  if (v != v) { strncpy(buf, "nan", n); buf[n - 1] = '\0'; return; }
  bool neg = v < 0.0;
  if (neg) v = -v;
  if (decimals > 6) decimals = 6;
  uint32_t scale = 1;
  for (uint8_t i = 0; i < decimals; i++) scale *= 10;
  if (v * (double)scale > 4.0e9) { strncpy(buf, neg ? "-big" : "big", n); buf[n - 1] = '\0'; return; }
  const uint32_t sc = (uint32_t)(v * (double)scale + 0.5);
  if (sc == 0) neg = false;                      // never print "-0.00"
  uint32_t ip = sc / scale, fp = sc % scale;
  char t[24]; uint8_t k = 0;
  if (neg) t[k++] = '-';
  char d[12]; uint8_t m = 0;
  do { d[m++] = (char)('0' + ip % 10); ip /= 10; } while (ip);
  while (m) t[k++] = d[--m];
  if (decimals) {
    t[k++] = '.';
    char f[8];
    for (int i = (int)decimals - 1; i >= 0; i--) { f[i] = (char)('0' + fp % 10); fp /= 10; }
    for (uint8_t i = 0; i < decimals; i++) t[k++] = f[i];
  }
  t[k] = '\0';
  strncpy(buf, t, n); buf[n - 1] = '\0';
}

// One output line, built piece by piece (no format strings).
struct Line {
  char b[220]; size_t n;
  Line() : n(0) { b[0] = '\0'; }
  void raw(const char *s) { while (*s && n + 1 < sizeof b) b[n++] = *s++; b[n] = '\0'; }
  void txt(const char *s, uint8_t width = 0, bool right = false) {
    size_t len = strlen(s);
    if (right) for (size_t i = len; i < width; i++) raw(" ");
    raw(s);
    if (!right) for (size_t i = len; i < width; i++) raw(" ");
  }
  void num(double v, uint8_t dec, uint8_t width = 0) { char t[24]; fmtFixed(t, sizeof t, v, dec); txt(t, width, true); }
};

// The capability report (part of DIAG): what the cells in memory say each Rtotal would give. `OUT` is anything with
// print()/println() of text (the console's Stream, or a capture buffer in the host test).
// ser[i]/shu[i]: channel i's curves; labels[i]: "L"/"R"; currentR: the Rtotal the LUT now uses.
template <class OUT>
void printReport(OUT &out, const LdrCurve *const *ser, const LdrCurve *const *shu,
                 const char *const *labels, uint8_t n, float currentR,
                 const DarkProfile *const *prof = nullptr) {
  const DividerMath::Load load{AUDIO_SOURCE_OHMS, AMP_INPUT_LOAD_OHMS};
  ChannelCaps cc[2];
  for (uint8_t i = 0; i < n; i++) {
    cc[i] = measure(*ser[i], *shu[i], prof ? prof[i] : nullptr);
    if (!cc[i].ok) {
      Line e; e.raw("ERR "); e.raw(labels[i]);
      e.raw(": no usable curves in memory -- send CAL first");
      out.println(e.b);
      return;
    }
  }
  { Line h; h.raw("CAPABILITIES ");
    for (uint8_t i = 0; i < n; i++) h.raw(labels[i]);
    h.raw(" -- curves in memory; model: Rsrc="); h.num(AUDIO_SOURCE_OHMS, 0);
    h.raw(" Rload="); h.num(AMP_INPUT_LOAD_OHMS, 0);
    h.raw(" margin="); h.num(RANGE_SAFETY_MARGIN_DB, 1);
    h.raw("dB dither="); h.num(CAPS_DITHER_LEVELS, 0);
    out.println(h.b); }
  out.println("Measured dark end of each series cell (dark-end walk: settled from the dark side; memory = level 5 s after a loud passage vs settled):");
  for (uint8_t i = 0; i < n; i++) {
    const DarkProfile *pr = cc[i].prof;
    if (!pr || pr->n == 0) { Line c; c.raw("  "); c.raw(labels[i]); c.raw(": not measured -- send CAL (no Rtotal qualifies for AUTO or RMAX until it is)"); out.println(c.b); continue; }
    for (uint8_t k = 0; k < pr->n; k++) {
      const DarkPoint &d = pr->p[k];
      Line c; c.raw("  "); c.raw(labels[i]); c.raw(" duty "); c.num(d.duty, 0); c.raw(": ");
      c.num(d.ohms / 1000.0, 1); c.raw("k  settled in "); c.num(d.settleS, 0); c.raw(" s  memory x");
      c.num(d.memory, 3); if (!d.ok) c.raw("  <-- did not settle / not darker: end of the usable dark range");
      out.println(c.b);
    }
  }
  out.println("Curve ends: bright end (floor used / lowest reading) and dark end (darkest reading / darkest fine-spaced point):");
  for (uint8_t i = 0; i < n; i++) {
    for (uint8_t e = 0; e < 2; e++) {
      const LdrCurve &cv = e ? *shu[i] : *ser[i];
      uint8_t lo = 0;
      for (uint8_t k = 1; k < cv.count(); k++) if (cv.point(k).ohms < cv.point(lo).ohms) lo = k;
      const int fs = fineStartIndex(cv);
      Line c; c.raw("  "); c.raw(labels[i]); c.raw(e ? " shunt  | floor " : " series | floor ");
      c.num(cv.point(cv.count() - 1).ohms, 1); c.raw(" @"); c.num(cv.point(cv.count() - 1).duty, 0);
      c.raw(", lowest "); c.num(cv.point(lo).ohms, 1); c.raw(" @"); c.num(cv.point(lo).duty, 0);
      c.raw(" | darkest "); c.num(cv.point(0).ohms / 1000.0, 0); c.raw("k @"); c.num(cv.point(0).duty, 0);
      if (fs >= 0) {
        c.raw(", fine-spaced top "); c.num(cv.point((uint8_t)fs).ohms / 1000.0, 0); c.raw("k @"); c.num(cv.point((uint8_t)fs).duty, 0);
        if (firstIntervalLagSuspect(cv)) c.raw(" (lag?)");
      }
      out.println(c.b);
    }
  }
  const float supMax = supportedMaxOhms(cc, n);
  const float rMax = usableMaxOhms(cc, ser, n, load, RANGE_SAFETY_MARGIN_DB, CAPS_DITHER_LEVELS);
  const float rMin = rMinOhms(cc, n, load);
  const float pick = autoPickOhms(cc, ser, n, load, RANGE_SAFETY_MARGIN_DB, CAPS_DITHER_LEVELS);

  float cand[11]; uint8_t nc = 0;
  const float base[] = {5000, 10000, 25000, 50000, 75000, 100000, 150000, 200000};
  for (uint8_t i = 0; i < 8; i++) cand[nc++] = base[i];
  if (currentR > 0) cand[nc++] = currentR;
  if (pick > 0) cand[nc++] = pick;
  if (rMax > 0) cand[nc++] = rMax;
  for (uint8_t a = 1; a < nc; a++) { float v = cand[a]; int8_t j = (int8_t)a - 1; while (j >= 0 && cand[j] > v) { cand[j + 1] = cand[j]; j--; } cand[j + 1] = v; }
  out.println("Rtotal |  depth dB (per channel; common) | Zin loud | max gain | step %/ct | +-quant dB | memory     | notes");
  float prev = -1;
  for (uint8_t k = 0; k < nc; k++) {
    const float r = cand[k];
    if (r == prev) continue;   // current / pick may coincide with a grid value
    prev = r;
    Row w[2]; float common = 1.0e9f, worstQuant = 0, worstSlope = 0; bool extrap = false, supported = true, lag = false;
    Line d;
    for (uint8_t i = 0; i < n; i++) {
      w[i] = evaluate(cc[i], *ser[i], r, load, RANGE_SAFETY_MARGIN_DB, CAPS_DITHER_LEVELS);
      if (w[i].depthDb < common) common = w[i].depthDb;
      if (w[i].quantDb > worstQuant) worstQuant = w[i].quantDb;
      if (w[i].slope > worstSlope) worstSlope = w[i].slope;
      extrap = extrap || w[i].slopeExtrap;
      supported = supported && w[i].supported;
      lag = lag || w[i].lagFlag;
      if (i) d.raw("/");
      d.num(w[i].depthDb, 1);
    }
    if (n == 2) { d.raw("; "); d.num(common, 1); }
    Line notes;
    if (!supported) notes.raw("beyond curve ");
    if (w[0].memoryUnknown) notes.raw("not measured ");
    if (extrap) notes.raw("step*extrap ");
    if (lag) notes.raw("lag? ");
    if (r == currentR) notes.raw("<-- current ");
    if (r == pick) notes.raw("<-- AUTO ");
    if (r == rMax) notes.raw("<-- RMAX ");
    Line row;
    row.num(r, 0, 6); row.raw(" | "); row.txt(d.b, 31); row.raw(" | ");
    row.num(w[0].zinOhms / 1000.0, 1, 5); row.raw("k   | ");
    row.num(w[0].maxGainDb, 2, 6); row.raw("   | ");
    row.num(100.0 * worstSlope, 1, 6); row.raw("    | ");
    row.num(worstQuant, 2, 6); row.raw("     | x");
    { const float m = (n > 1 && w[1].memory > w[0].memory) ? w[1].memory : w[0].memory; row.num(m, 2); } row.raw("      | "); row.raw(notes.b);
    out.println(row.b);
  }
  { Line m; m.raw("RMIN  (loud-end input impedance >= "); m.num(CAPS_MIN_ZIN_OHMS, 0); m.raw(" ohm): "); m.num(rMin, 0); out.println(m.b); }
  { Line m; m.raw("Curves reach (darkest fine-spaced point, both sides): "); m.num(supMax, 0); out.println(m.b); }
  { Line m; m.raw("RMAX  (deepest still usable: measured memory <= "); m.num(CAPS_RMAX_MAX_MEMORY, 2);
    m.raw(", step error <= "); m.num(CAPS_RMAX_MAX_QUANT_DB, 2); m.raw(" dB, slope <= "); m.num(100.0 * CAPS_RMAX_MAX_SLOPE, 0); m.raw(" %/ct): ");
    if (rMax > 0) m.num(rMax, 0); else m.raw("none");
    out.println(m.b); }
  { Line m;
    if (pick > 0) {
      m.raw("AUTO pick (measured memory <= "); m.num(CAPS_AUTO_MAX_MEMORY, 2);
      m.raw(", step error <= "); m.num(CAPS_AUTO_MAX_QUANT_DB, 2); m.raw(" dB, slope <= "); m.num(100.0 * CAPS_AUTO_MAX_SLOPE, 0); m.raw(" %/ct): "); m.num(pick, 0);
    } else {
      m.raw("AUTO pick: none -- no Rtotal in [RMIN, curve-supported] meets the limits");
    }
    out.println(m.b); }
  out.println("* memory (level after a loud passage, settled) is MEASURED by the dark-end walk on each channel -- the worse channel shown; 'not measured' rows lie deeper than the deepest point");
  out.println("  that settled and never qualify for AUTO or RMAX. 'step' is the series cell's");
  out.println("  ln-resistance per PWM count at the quiet end (extrapolated beyond the darkest point). 'lag?' = that point sits in the curve's");
  out.println("  steep first interval, which may be relaxation lag or a genuinely sharp knee -- trust the trimmed LUT over it there.");
}

} // namespace Capabilities
