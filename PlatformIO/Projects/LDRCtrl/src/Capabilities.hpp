#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "Calibration.hpp" // LdrCurve
#include "Config.hpp"
#include "DividerMath.hpp"

// ============================================================================
// Capabilities -- what the measured curves say each Rtotal would give.
//
// Pure functions over LdrCurve (no hardware, no state), tested on the host
// against real dumps. Nothing here changes calibration: CALCAPS prints it, and
// the later CAL FULL "auto" choice will use the same numbers.
//
// The sweep itself does not depend on Rtotal -- the curves are the cells'
// properties -- so every number here is "what if the LUT were solved for R".
//
// Honest limits of a curves-only model:
//   * depth, loud-end impedance and max gain are exact arithmetic on the
//     measured floors (the same floor definition computeMaxRangeDb uses);
//   * the step size comes from the curve's own slope where the curve is
//     trustworthy, and from its settled knee slope beyond that (flagged);
//   * hysteresis is a PRIOR from earlier CALSCAN data, not a measurement of
//     this unit, and is extrapolated above its last entry (flagged);
//   * "supported" only says the curve has consistent (non-lag) points that far
//     into the dark. The live trim can still work beyond it -- the 100k
//     calibration trimmed with the curve's reliable top only just above 100k.
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

// Index of the first point (darkest first) that is NOT lag-suspect, or -1.
inline int reliableIndex(const LdrCurve &c) {
  const uint8_t n = c.count();
  if (n < 4) return -1;
  for (uint8_t i = 0; i + 1 < n; i++) {
    if ((int)c.point(i + 1).duty - (int)c.point(i).duty > (int)CAPS_LAG_MAX_GAP) continue; // sparse dark tail
    float s[6]; uint8_t m = 0;
    for (uint8_t j = i + 1; j + 1 < n && m < 6; j++) s[m++] = intervalSlope(c, j);
    if (m < 3) return -1;
    if (intervalSlope(c, i) > CAPS_LAG_SLOPE_FACTOR * medianOf(s, m)) continue;            // lag signature
    return (int)i;
  }
  return -1;
}

// Resistance at the darkest consistent point; 0 if the curve has none.
inline float reliableTop(const LdrCurve &c) {
  int i = reliableIndex(c);
  return i < 0 ? 0.0f : c.point((uint8_t)i).ohms;
}

// The knee's settled slope AT ITS DARK END: median over the first few intervals of the
// reliable region. Deliberately not the whole knee -- it flattens toward the bright
// end, and this is what gets extrapolated further into the dark.
inline float kneeSlope(const LdrCurve &c) {
  int i = reliableIndex(c);
  if (i < 0) return 0.0f;
  float s[4]; uint8_t m = 0;
  for (uint8_t j = (uint8_t)i; j + 1 < c.count() && m < 4; j++) s[m++] = intervalSlope(c, j);
  return medianOf(s, m);
}

// ln(R) per count at resistance `ohms`: the curve's own slope where it is trustworthy,
// the settled knee slope beyond its reliable top (extrapolated = true).
inline float slopeAt(const LdrCurve &c, float ohms, bool &extrapolated) {
  extrapolated = false;
  int i = reliableIndex(c);
  if (i < 0) return 0.0f;
  if (ohms > c.point((uint8_t)i).ohms) { extrapolated = true; return kneeSlope(c); }
  for (uint8_t k = (uint8_t)i; k + 1 < c.count(); k++) {
    if (c.point(k).ohms >= ohms && ohms >= c.point(k + 1).ohms) return intervalSlope(c, k);
  }
  return intervalSlope(c, c.count() - 2); // below the lowest point: use the last interval
}

// Asc/desc resistance ratio at Rtotal `r`, log-interpolated from the CALSCAN prior;
// flat below its first entry, extrapolated (flag) above its last.
inline float hystRatio(float r, bool &extrapolated) {
  extrapolated = false;
  const uint8_t n = CAPS_HYST_PRIOR_COUNT;
  if (r <= CAPS_HYST_PRIOR_R[0]) return CAPS_HYST_PRIOR_RATIO[0];
  for (uint8_t i = 0; i + 1 < n; i++) {
    if (r <= CAPS_HYST_PRIOR_R[i + 1]) {
      float f = logf(r / CAPS_HYST_PRIOR_R[i]) / logf(CAPS_HYST_PRIOR_R[i + 1] / CAPS_HYST_PRIOR_R[i]);
      return CAPS_HYST_PRIOR_RATIO[i] * powf(CAPS_HYST_PRIOR_RATIO[i + 1] / CAPS_HYST_PRIOR_RATIO[i], f);
    }
  }
  extrapolated = true;
  float f = logf(r / CAPS_HYST_PRIOR_R[n - 1]) / logf(CAPS_HYST_PRIOR_R[n - 1] / CAPS_HYST_PRIOR_R[n - 2]);
  return CAPS_HYST_PRIOR_RATIO[n - 1] * powf(CAPS_HYST_PRIOR_RATIO[n - 1] / CAPS_HYST_PRIOR_RATIO[n - 2], f);
}

// What one channel's curves say, independent of any Rtotal.
struct ChannelCaps {
  bool ok = false;          // both curves usable
  float rsFloor = 0, rshFloor = 0;   // lowest resistance (highest-duty point), as computeMaxRangeDb takes it
  float serTop = 0, shuTop = 0;      // darkest consistent resistance on each curve
  float supportedMax() const { return serTop < shuTop ? serTop : shuTop; }
};

inline ChannelCaps measure(const LdrCurve &series, const LdrCurve &shunt) {
  ChannelCaps c;
  if (series.empty() || shunt.empty()) return c;
  c.rsFloor = series.point(series.count() - 1).ohms;
  c.rshFloor = shunt.point(shunt.count() - 1).ohms;
  c.serTop = reliableTop(series);
  c.shuTop = reliableTop(shunt);
  c.ok = c.rsFloor > 0 && c.rshFloor > 0 && c.serTop > 0 && c.shuTop > 0;
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
  float hyst; bool hystExtrap;
  bool supported;         // both curves reach this resistance with consistent points
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
  w.hyst = hystRatio(r, w.hystExtrap);
  w.supported = (r <= c.serTop) && (r <= c.shuTop);
  return w;
}

// Smallest Rtotal (500 ohm grid) whose loud-end input impedance reaches CAPS_MIN_ZIN_OHMS
// on every channel; 0 if none below 1 MOhm.
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

// The deepest Rtotal inside [rMin, supportedMax] whose hysteresis prior and worst-case
// step error stay under the CAPS_AUTO_* limits on every channel. 0 if none qualifies.
inline float autoPickOhms(const ChannelCaps *ch, const LdrCurve *const *series, uint8_t n,
                          const DividerMath::Load &load, float marginDb, float ditherLevels) {
  const float lo = rMinOhms(ch, n, load), hi = supportedMaxOhms(ch, n);
  if (lo <= 0.0f || hi < lo) return 0.0f;
  for (float r = hi; r >= lo; r -= 500.0f) {
    bool ok = true;
    for (uint8_t i = 0; i < n && ok; i++) {
      Row w = evaluate(ch[i], *series[i], r, load, marginDb, ditherLevels);
      if (w.hyst > CAPS_AUTO_MAX_HYST || w.quantDb > CAPS_AUTO_MAX_QUANT_DB) ok = false;
    }
    if (ok) return r;
  }
  return 0.0f;
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

// CALCAPS: print what the curves in memory say each Rtotal would give. `OUT` is anything with
// print()/println() of text (the console's Stream, or a capture buffer in the host test).
// ser[i]/shu[i]: channel i's curves; labels[i]: "L"/"R"; currentR: the Rtotal the LUT now uses.
template <class OUT>
void printReport(OUT &out, const LdrCurve *const *ser, const LdrCurve *const *shu,
                 const char *const *labels, uint8_t n, float currentR) {
  const DividerMath::Load load{AUDIO_SOURCE_OHMS, AMP_INPUT_LOAD_OHMS};
  ChannelCaps cc[2];
  for (uint8_t i = 0; i < n; i++) {
    cc[i] = measure(*ser[i], *shu[i]);
    if (!cc[i].ok) {
      Line e; e.raw("ERR "); e.raw(labels[i]);
      e.raw(": no usable curves in memory -- run CALAUTO FULL first (CALCAPS reads the curves, it does not sweep)");
      out.println(e.b);
      return;
    }
  }
  { Line h; h.raw("CALCAPS ");
    for (uint8_t i = 0; i < n; i++) h.raw(labels[i]);
    h.raw(" -- curves in memory; model: Rsrc="); h.num(AUDIO_SOURCE_OHMS, 0);
    h.raw(" Rload="); h.num(AMP_INPUT_LOAD_OHMS, 0);
    h.raw(" margin="); h.num(RANGE_SAFETY_MARGIN_DB, 1);
    h.raw("dB dither="); h.num(CAPS_DITHER_LEVELS, 0);
    out.println(h.b); }
  out.println("Darkest consistent (non-lag) point on each curve:");
  for (uint8_t i = 0; i < n; i++) {
    Line c; c.raw("  "); c.raw(labels[i]); c.raw(" series "); c.num(cc[i].serTop / 1000.0, 0); c.raw("k   ");
    c.raw(labels[i]); c.raw(" shunt "); c.num(cc[i].shuTop / 1000.0, 0);
    c.raw("k   (floors: series "); c.num(cc[i].rsFloor, 1); c.raw(", shunt "); c.num(cc[i].rshFloor, 1); c.raw(" ohm)");
    out.println(c.b);
  }
  const float supMax = supportedMaxOhms(cc, n);
  const float rMin = rMinOhms(cc, n, load);
  const float pick = autoPickOhms(cc, ser, n, load, RANGE_SAFETY_MARGIN_DB, CAPS_DITHER_LEVELS);

  float cand[10]; uint8_t nc = 0;
  const float base[] = {5000, 10000, 25000, 50000, 75000, 100000, 125000, 150000};
  for (uint8_t i = 0; i < 8; i++) cand[nc++] = base[i];
  if (currentR > 0) cand[nc++] = currentR;
  if (pick > 0) cand[nc++] = pick;
  for (uint8_t a = 1; a < nc; a++) { float v = cand[a]; int8_t j = (int8_t)a - 1; while (j >= 0 && cand[j] > v) { cand[j + 1] = cand[j]; j--; } cand[j + 1] = v; }
  out.println("Rtotal |  depth dB (per channel; common) | Zin loud | max gain | step %/ct | +-quant dB | hysteresis | notes");
  float prev = -1;
  for (uint8_t k = 0; k < nc; k++) {
    const float r = cand[k];
    if (r == prev) continue;   // current / pick may coincide with a grid value
    prev = r;
    Row w[2]; float common = 1.0e9f, worstQuant = 0, worstSlope = 0; bool extrap = false, supported = true;
    Line d;
    for (uint8_t i = 0; i < n; i++) {
      w[i] = evaluate(cc[i], *ser[i], r, load, RANGE_SAFETY_MARGIN_DB, CAPS_DITHER_LEVELS);
      if (w[i].depthDb < common) common = w[i].depthDb;
      if (w[i].quantDb > worstQuant) worstQuant = w[i].quantDb;
      if (w[i].slope > worstSlope) worstSlope = w[i].slope;
      extrap = extrap || w[i].slopeExtrap;
      supported = supported && w[i].supported;
      if (i) d.raw("/");
      d.num(w[i].depthDb, 1);
    }
    if (n == 2) { d.raw("; "); d.num(common, 1); }
    Line notes;
    if (!supported) notes.raw("beyond curve ");
    if (w[0].hystExtrap) notes.raw("hyst*extrap ");
    if (extrap) notes.raw("step*extrap ");
    if (r == currentR) notes.raw("<-- current ");
    if (r == pick) notes.raw("<-- AUTO ");
    Line row;
    row.num(r, 0, 6); row.raw(" | "); row.txt(d.b, 31); row.raw(" | ");
    row.num(w[0].zinOhms / 1000.0, 1, 5); row.raw("k   | ");
    row.num(w[0].maxGainDb, 2, 6); row.raw("   | ");
    row.num(100.0 * worstSlope, 1, 6); row.raw("    | ");
    row.num(worstQuant, 2, 6); row.raw("     | x");
    row.num(w[0].hyst, 2); row.raw("      | "); row.raw(notes.b);
    out.println(row.b);
  }
  { Line m; m.raw("RMIN  (loud-end input impedance >= "); m.num(CAPS_MIN_ZIN_OHMS, 0); m.raw(" ohm): "); m.num(rMin, 0); out.println(m.b); }
  { Line m; m.raw("Curve-supported up to: "); m.num(supMax, 0);
    m.raw("   (beyond it the live trim must extrapolate past consistent data)"); out.println(m.b); }
  { Line m;
    if (pick > 0) {
      m.raw("AUTO pick (hysteresis prior <= "); m.num(CAPS_AUTO_MAX_HYST, 2);
      m.raw(", step error <= "); m.num(CAPS_AUTO_MAX_QUANT_DB, 2); m.raw(" dB): "); m.num(pick, 0);
    } else {
      m.raw("AUTO pick: none -- no Rtotal in [RMIN, curve-supported] meets the limits");
    }
    out.println(m.b); }
  out.println("* the hysteresis column is a PRIOR (CALSCAN on the original driver), extrapolated above 100k; 'step' is the series cell's");
  out.println("  ln-resistance per PWM count at the quiet end -- the settled knee slope where the curve is beyond its consistent data.");
}

} // namespace Capabilities