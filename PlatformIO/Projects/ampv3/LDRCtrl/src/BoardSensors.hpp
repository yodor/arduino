#pragma once
// Driver board analog sensors (v1b): pure conversions, host-testable.
//
//  * NTC (100k at 25 C, B = 3950) on the aluminium block that carries all eight driver
//    transistors. Divider: 100k from ADC_VREF to the ADC pin, NTC from the pin to AGND.
//    The ADC also measures against ADC_VREF, so the reading is a pure ratio: the 3.3 V
//    rail cancels out.
//  * TL431 (REF tied to cathode -> 2.495 V) on its own ADC pin. Read against ADC_VREF,
//    i.e. the 3.3 V rail, so its code tells how far the rail has moved. The absolute value
//    follows the TL431's tolerance (0.5-1 %); the CHANGE is what matters (the PWM drive is
//    duty x rail).
#include <math.h>
#include <stdint.h>

namespace BoardSensors {

constexpr float ADC_FULL      = 4096.0f;    // 12-bit
constexpr float NTC_R25_OHMS  = 100000.0f;
constexpr float NTC_B         = 3950.0f;
constexpr float NTC_TOP_OHMS  = 100000.0f;  // divider resistor to ADC_VREF
constexpr float TL431_VOLTS   = 2.495f;

// The block NTC's state from its averaged ADC code: OPEN (missing / broken wire: the 100k
// pull-up takes the pin to full scale), SHORT (pin at ~0), BAD (a reading that is no real
// block temperature: outside -20..+100 C), or OK.
enum class NtcState : uint8_t { OK, OPEN, SHORT, BAD };
constexpr float NTC_MIN_C = -20.0f, NTC_MAX_C = 100.0f;

inline float ntcRawCelsius(float code) {
  const float r = code / ADC_FULL;
  const float ohms = NTC_TOP_OHMS * r / (1.0f - r);
  const float invT = 1.0f / 298.15f + logf(ohms / NTC_R25_OHMS) / NTC_B;
  return 1.0f / invT - 273.15f;
}
inline NtcState ntcState(float code) {
  if (!(code < ADC_FULL - 20.0f)) return NtcState::OPEN;
  if (!(code > 20.0f)) return NtcState::SHORT;
  const float t = ntcRawCelsius(code);
  if (!(t >= NTC_MIN_C && t <= NTC_MAX_C)) return NtcState::BAD;
  return NtcState::OK;
}
inline const char *ntcStateName(NtcState s) {
  return s == NtcState::OK ? "OK" : s == NtcState::OPEN ? "OPEN" : s == NtcState::SHORT ? "SHORT" : "BAD";
}
// Averaged ADC code (0..4095) -> block temperature in C, or NAN unless the NTC is OK.
inline float ntcCelsius(float code) {
  return ntcState(code) == NtcState::OK ? ntcRawCelsius(code) : NAN;
}

// Averaged ADC code of the TL431 -> the 3.3 V rail in volts, or NAN if implausible
// (no TL431 fitted, or the rail far outside 2.6..4.0 V).
inline float railVolts(float code) {
  if (!(code > 0.0f)) return NAN;
  const float v = TL431_VOLTS * ADC_FULL / code;
  return (v > 2.6f && v < 4.0f) ? v : NAN;
}

} // namespace BoardSensors
