#pragma once
#include <Arduino.h>
#include <stdio.h>

// ============================================================================
// Thermal (die temperature + uptime)
//
// The RP2040's on-die temperature sensor, read as a PROXY for the board's
// temperature. Why it is here: the LED driver stages (NPN Vbe, PNP V_EB, a
// 100k bleed) drift with temperature -- measured cold-start vs warm: the duty
// needed for a given resistance moved by ~11-14 counts at the quiet end --
// and nothing yet says how that tracks temperature. This makes the temperature
// visible next to every calibration/trim so duty-vs-degC can be fitted.
//
// Caveat worth knowing when reading the numbers: it is the DIE temperature,
// which sits a few degrees above the board and self-heats a little under CPU
// load -- read it at idle (the calibration code reads it before starting) and
// look at CHANGES, not the absolute value (the sensor's absolute accuracy is
// only about +/-2 degC; its resolution after averaging is ~0.1 degC).
// ============================================================================
namespace Thermal {

// Average of 16 conversions, degrees Celsius.
float dieTempC();

// Seconds since power-up or reset (millis()/1000). The daughter board has no
// wall clock, and for warm-up analysis time-since-power-on is the number that
// matters anyway. Wraps after ~49.7 days of continuous running.
uint32_t uptimeSeconds();

// "HH:MM:SS" into buf (hours may exceed 24, and 99). Returns buf.
inline char *formatHms(uint32_t seconds, char *buf, size_t len) {
  snprintf(buf, len, "%02lu:%02lu:%02lu", (unsigned long)(seconds / 3600UL),
           (unsigned long)((seconds / 60UL) % 60UL), (unsigned long)(seconds % 60UL));
  return buf;
}

// Prints "<label>: 31.4 C (RP2040 die)  uptime 01:02:05 (3725 s)" on its own
// line: the two numbers needed to log a warm-up curve, always together. If
// elapsedMs is non-zero, "  took 39.4 s" is appended -- the end-of-calibration
// lines use it so temperature, uptime and total duration read as one record.
void report(Stream &out, const __FlashStringHelper *label, uint32_t elapsedMs = 0);

} // namespace Thermal