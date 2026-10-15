#pragma once
// 1-Wire / DS18B20 logic that does not touch hardware: CRC8, temperature conversion, ROM
// formatting/parsing and the ROM search (Maxim AN187) over any bus object providing
//   bool reset();            // true if a presence pulse was seen
//   void writeBit(bool b);
//   bool readBit();
// so the search can be host-tested against a simulated bus.
#include <stdint.h>
#include <math.h>

namespace OneWireMath {

// Dallas/Maxim CRC8 (polynomial x^8 + x^5 + x^4 + 1, LSB first).
inline uint8_t crc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < len; i++) {
    uint8_t b = data[i];
    for (uint8_t k = 0; k < 8; k++) {
      const uint8_t mix = (crc ^ b) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      b >>= 1;
    }
  }
  return crc;
}

// DS18B20 scratchpad bytes 0..1 (12-bit, two's complement, 1/16 C) -> Celsius.
inline float ds18b20Celsius(uint8_t lsb, uint8_t msb) {
  const int16_t raw = (int16_t)((uint16_t)msb << 8 | lsb);
  return raw / 16.0f;
}

// A scratchpad is valid if its CRC matches and it is not the all-ones of an absent device.
inline bool scratchpadValid(const uint8_t sp[9]) {
  bool allFF = true;
  for (uint8_t i = 0; i < 9; i++) if (sp[i] != 0xFF) allFF = false;
  return !allFF && crc8(sp, 8) == sp[8];
}

constexpr uint8_t FAMILY_DS18B20 = 0x28;

inline bool romValid(const uint8_t rom[8]) { return crc8(rom, 7) == rom[7] && rom[0] != 0; }

// "28-FF4C1D6416037C" style text (family-serial+crc), 17 characters plus terminator.
inline void romToText(const uint8_t rom[8], char out[18]) {
  static const char *H = "0123456789ABCDEF";
  out[0] = H[rom[0] >> 4]; out[1] = H[rom[0] & 15]; out[2] = '-';
  for (uint8_t i = 1; i < 8; i++) { out[1 + 2 * i] = H[rom[i] >> 4]; out[2 + 2 * i] = H[rom[i] & 15]; }
  out[17] = 0;
}

inline bool textToRom(const char *s, uint8_t rom[8]) {
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  uint8_t n = 0; int hi = -1;
  for (const char *p = s; *p && n < 8; p++) {
    const int v = hex(*p);
    if (v < 0) continue;                 // skip '-' and anything else
    if (hi < 0) hi = v; else { rom[n++] = (uint8_t)(hi << 4 | v); hi = -1; }
  }
  return n == 8 && romValid(rom);
}

// Find up to maxDevices ROMs on the bus (the standard search algorithm). Returns the count.
// Bounded: a faulty bus (line stuck low, garbage bits) makes every pass "discover" an
// invalid ROM and the classic algorithm would then walk 2^64 of them -- it hung the boot.
// A healthy bus needs one pass per device, so 2 x maxDevices + 2 passes is ample.
template <class Bus>
uint8_t searchRoms(Bus &bus, uint8_t (*roms)[8], uint8_t maxDevices) {
  uint8_t found = 0;
  uint8_t rom[8] = {0};
  int lastDiscrepancy = 0;
  bool lastDevice = false;
  uint8_t passes = 0;
  const uint8_t maxPasses = (uint8_t)(2 * maxDevices + 2);
  while (!lastDevice && found < maxDevices && passes++ < maxPasses) {
    if (!bus.reset()) return found;      // nobody there
    int lastZero = 0;
    for (uint8_t b = 0; b < 8; b++) bus.writeBit((0xF0 >> b) & 1); // SEARCH ROM (LSB first)
    for (int id = 1; id <= 64; id++) {
      const bool bit = bus.readBit();
      const bool cmp = bus.readBit();
      if (bit && cmp) return found;      // no device answered: bus error
      bool dir;
      if (bit != cmp) dir = bit;         // all remaining devices agree
      else {                             // discrepancy: both 0 and 1 present
        const uint8_t byteI = (uint8_t)((id - 1) / 8), mask = (uint8_t)(1u << ((id - 1) % 8));
        if (id < lastDiscrepancy) dir = (rom[byteI] & mask) != 0;
        else dir = (id == lastDiscrepancy);
        if (!dir) lastZero = id;
      }
      const uint8_t byteI = (uint8_t)((id - 1) / 8), mask = (uint8_t)(1u << ((id - 1) % 8));
      if (dir) rom[byteI] |= mask; else rom[byteI] &= (uint8_t)~mask;
      bus.writeBit(dir);
    }
    lastDiscrepancy = lastZero;
    if (lastDiscrepancy == 0) lastDevice = true;
    if (romValid(rom)) {
      for (uint8_t i = 0; i < 8; i++) roms[found][i] = rom[i];
      found++;
    }
  }
  return found;
}

} // namespace OneWireMath
