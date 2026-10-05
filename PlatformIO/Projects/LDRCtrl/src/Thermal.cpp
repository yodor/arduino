#include "Thermal.hpp"

namespace Thermal {

float dieTempC() {
  const int N = 16; // the sensor's ADC is ~0.5 degC per LSB; averaging brings it to ~0.1
  float sum = 0.0f;
  for (int i = 0; i < N; i++) sum += analogReadTemp();
  return sum / N;
}

uint32_t uptimeSeconds() { return millis() / 1000UL; }

void report(Stream &out, const __FlashStringHelper *label, uint32_t elapsedMs) {
  char hms[16];
  uint32_t up = uptimeSeconds();
  out.print(label);
  out.print(F(": "));
  out.print(dieTempC(), 1);
  out.print(F(" C (RP2040 die)  uptime "));
  out.print(formatHms(up, hms, sizeof(hms)));
  out.print(F(" ("));
  out.print((unsigned long)up);
  out.print(F(" s)"));
  if (elapsedMs > 0) {
    out.print(F("  took "));
    out.print(elapsedMs / 1000.0f, 1);
    out.print(F(" s"));
  }
  out.println();
}

} // namespace Thermal