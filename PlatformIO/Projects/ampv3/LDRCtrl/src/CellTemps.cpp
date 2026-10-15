#include "CellTemps.hpp"
#include "OneWireMath.hpp"
#include "Pins.hpp"
#include "BusyHook.hpp"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/time.h"
#include "hardware/timer.h"
#include <LittleFS.h>

namespace {
constexpr uint8_t MAX_SENSORS = 4;
constexpr const char *ASSIGN_PATH = "/ds18b20.txt";
uint8_t gRom[MAX_SENSORS][8];
uint8_t gCount = 0;
bool gLineLow = false;              // the bus did not idle high: wiring fault, search skipped
bool gBegun = false;                // started on first use (TEMP), never at boot
int8_t gSide[2] = {-1, -1};         // index into gRom for L and R, -1 = unknown

// ---- the bus: the pin only ever pulls low or lets go (external 4.7k pull-up) ----
struct GpioBus {
  // Timing with the Pico SDK's own primitives: busy_wait_us_32() spins on the hardware
  // timer, save_and_disable_interrupts()/restore_interrupts() just save and restore the
  // interrupt mask. Neither depends on interrupts -- the Arduino delayMicroseconds()/
  // noInterrupts() wrappers stopped the first reset pulse dead with interrupts off.
  uint8_t pin;
  void low() { gpio_set_dir(pin, GPIO_OUT); }   // output latch is 0
  void release() { gpio_set_dir(pin, GPIO_IN); }
  bool reset() {
    low(); busy_wait_us_32(480);                  // long low: interrupts may run
    const uint32_t irq = save_and_disable_interrupts();
    release(); busy_wait_us_32(70);
    const bool present = !gpio_get(pin);          // a device holds the line low
    restore_interrupts(irq);
    busy_wait_us_32(410);
    return present;
  }
  void writeBit(bool b) {
    const uint32_t irq = save_and_disable_interrupts();
    low();
    if (b) { busy_wait_us_32(6); release(); restore_interrupts(irq); busy_wait_us_32(64); }
    else   { busy_wait_us_32(60); release(); restore_interrupts(irq); busy_wait_us_32(10); }
  }
  bool readBit() {
    const uint32_t irq = save_and_disable_interrupts();
    low(); busy_wait_us_32(6); release(); busy_wait_us_32(9);
    const bool v = gpio_get(pin);
    restore_interrupts(irq);
    busy_wait_us_32(55);
    return v;
  }
  void writeByte(uint8_t v) { for (uint8_t i = 0; i < 8; i++) writeBit((v >> i) & 1); }
  uint8_t readByte() { uint8_t v = 0; for (uint8_t i = 0; i < 8; i++) if (readBit()) v |= (uint8_t)(1u << i); return v; }
};
GpioBus gBus{ONEWIRE_PIN};

void loadAssignment() {
  gSide[0] = gSide[1] = -1;
  File f = LittleFS.open(ASSIGN_PATH, "r");
  if (!f) return;
  char buf[64] = {0};
  const size_t n = f.read((uint8_t *)buf, sizeof(buf) - 1);
  f.close();
  buf[n] = 0;
  for (uint8_t side = 0; side < 2; side++) {
    const char *tag = side ? "R=" : "L=";
    const char *p = strstr(buf, tag);
    uint8_t rom[8];
    if (!p || !OneWireMath::textToRom(p + 2, rom)) continue;
    for (uint8_t i = 0; i < gCount; i++) if (!memcmp(gRom[i], rom, 8)) gSide[side] = (int8_t)i;
  }
}

void saveAssignment() {
  File f = LittleFS.open(ASSIGN_PATH, "w");
  if (!f) return;
  for (uint8_t side = 0; side < 2; side++) {
    if (gSide[side] < 0) continue;
    char t[18]; OneWireMath::romToText(gRom[gSide[side]], t);
    f.print(side ? "R=" : "L="); f.println(t);
  }
  f.close();
}

// Convert on every sensor at once, then read each by ROM. t[i] = NAN if it did not answer.
void convertAndRead(float t[MAX_SENSORS]) {
  for (uint8_t i = 0; i < MAX_SENSORS; i++) t[i] = NAN;
  if (gCount == 0 || !gBus.reset()) return;
  gBus.writeByte(0xCC);                 // SKIP ROM: all sensors
  gBus.writeByte(0x44);                 // CONVERT T
  const unsigned long t0 = millis();
  while (millis() - t0 < 800) {         // powered sensors hold the line low while converting
    BusyHook::wait(10);
    if (gBus.readBit()) break;
  }
  for (uint8_t i = 0; i < gCount; i++) {
    if (!gBus.reset()) continue;
    gBus.writeByte(0x55);               // MATCH ROM
    for (uint8_t k = 0; k < 8; k++) gBus.writeByte(gRom[i][k]);
    gBus.writeByte(0xBE);               // READ SCRATCHPAD
    uint8_t sp[9];
    for (uint8_t k = 0; k < 9; k++) sp[k] = gBus.readByte();
    if (OneWireMath::scratchpadValid(sp)) t[i] = OneWireMath::ds18b20Celsius(sp[0], sp[1]);
  }
}
} // namespace

namespace CellTemps {

void begin() {
  if (gBegun) return;
  gBegun = true;
  gpio_init(ONEWIRE_PIN);
  gpio_disable_pulls(ONEWIRE_PIN);      // the board's 4.7k does the pulling up
  gpio_put(ONEWIRE_PIN, 0);
  gpio_set_dir(ONEWIRE_PIN, GPIO_IN);
  busy_wait_us_32(2000);
  // An idle 1-Wire bus sits high on its pull-up. Held low means a wiring fault (pinout,
  // a short, a reversed sensor): report it and leave the bus alone.
  gLineLow = !gpio_get(ONEWIRE_PIN);
  if (gLineLow) {
    gCount = 0;
    Serial.println(F("Cell sensors (DS18B20): 1-Wire line held LOW -- check the wiring (TO-92 flat face, legs down: 1 GND, 2 DQ, 3 VDD). Cell temperatures disabled."));
    return;
  }
  gCount = OneWireMath::searchRoms(gBus, gRom, MAX_SENSORS);
  uint8_t keep = 0;                     // DS18B20s only
  for (uint8_t i = 0; i < gCount; i++)
    if (gRom[i][0] == OneWireMath::FAMILY_DS18B20) { if (keep != i) memcpy(gRom[keep], gRom[i], 8); keep++; }
  gCount = keep;
  loadAssignment();
  Serial.print(F("Cell sensors (DS18B20): "));
  Serial.print(gCount);
  Serial.print(F(" found"));
  if (gCount == 0) Serial.println(F(" -- none answered on GP21 (check the cable and the 4.7k pull-up)"));
  else Serial.println(assigned() ? F(", L/R assigned") : F(", not yet assigned -- TEMP ID L in DEBUG"));
}

uint8_t count() { return gBegun ? gCount : 0; }
bool begun() { return gBegun; }
bool assigned() { return gSide[0] >= 0 && gSide[1] >= 0; }

uint8_t read(float out[2]) {
  float t[MAX_SENSORS];
  convertAndRead(t);
  uint8_t n = 0;
  for (uint8_t side = 0; side < 2; side++) {
    out[side] = gSide[side] >= 0 ? t[gSide[side]] : NAN;
    if (!isnan(out[side])) n++;
  }
  return n;
}

void printSensors(Stream &s) {
  if (!gBegun) begin();
  float t[MAX_SENSORS];
  convertAndRead(t);
  if (gLineLow) { s.println(F("  cell sensors: 1-Wire line held LOW at boot -- wiring fault (1 GND, 2 DQ, 3 VDD)")); return; }
  if (gCount == 0) { s.println(F("  cell sensors: none found on the 1-Wire bus (GP21)")); return; }
  for (uint8_t i = 0; i < gCount; i++) {
    char txt[18]; OneWireMath::romToText(gRom[i], txt);
    s.print(F("  DS18B20 ")); s.print(txt);
    s.print(gSide[0] == i ? F("  = L cell  ") : gSide[1] == i ? F("  = R cell  ") : F("  (unassigned)  "));
    if (isnan(t[i])) s.println(F("no answer")); else { s.print(t[i], 2); s.println(F(" C")); }
  }
  if (!assigned()) s.println(F("  -> TEMP ID L : warm the LEFT board's sensor with a fingertip to assign L/R"));
}

bool identify(Stream &s, uint8_t side) {
  if (!gBegun) begin();
  if (gCount == 0) { s.println(F("ERR no DS18B20 found on the bus")); return false; }
  float base[MAX_SENSORS], now[MAX_SENSORS];
  convertAndRead(base);
  s.print(F("Warm the ")); s.print(side ? F("RIGHT") : F("LEFT"));
  s.println(F(" board's sensor with a fingertip now (watching up to 40 s)..."));
  int8_t hit = -1;
  const unsigned long t0 = millis();
  while (millis() - t0 < 40000UL && hit < 0) {
    BusyHook::wait(1200);
    convertAndRead(now);
    float best = 0; int8_t bi = -1; float second = 0;
    for (uint8_t i = 0; i < gCount; i++) {
      if (isnan(now[i]) || isnan(base[i])) continue;
      const float d = now[i] - base[i];
      if (d > best) { second = best; best = d; bi = (int8_t)i; } else if (d > second) second = d;
    }
    if (bi >= 0 && best >= 0.5f && best - second >= 0.3f) hit = bi;
  }
  if (hit < 0) { s.println(F("ERR no sensor warmed by 0.5 C -- nothing changed")); return false; }
  gSide[side] = hit;
  if (gCount == 2) gSide[1 - side] = (int8_t)(1 - hit);   // the other one is the other side
  else if (gSide[1 - side] == hit) gSide[1 - side] = -1;
  saveAssignment();
  char txt[18]; OneWireMath::romToText(gRom[hit], txt);
  s.print(F("OK ")); s.print(side ? F("R") : F("L")); s.print(F(" cell = ")); s.println(txt);
  if (gCount == 2) s.println(F("   (the other sensor is the other side) -- saved"));
  return true;
}

} // namespace CellTemps
