#include "Ads1115.hpp"
#include <math.h>

static constexpr uint8_t REG_CONVERSION = 0x00;
static constexpr uint8_t REG_CONFIG     = 0x01;

// MODE=single-shot(bit8=1) | DR=860SPS(0b111<<5) | COMP_QUE=disable(0b11,
// i.e. ALERT/RDY pin unused). OS and MUX/PGA bits are added per-call.
static constexpr uint16_t BASE_CFG = 0x0100 | 0x00E0 | 0x0003;

bool Ads1115::begin(TwoWire &wire, uint8_t addr) {
  wire_ = &wire;
  addr_ = addr;
  return true;
}

bool Ads1115::writeConfig(uint16_t cfg) {
  wire_->beginTransmission(addr_);
  wire_->write(REG_CONFIG);
  wire_->write((uint8_t)(cfg >> 8));
  wire_->write((uint8_t)(cfg & 0xFF));
  return wire_->endTransmission() == 0;
}

uint16_t Ads1115::readRegister16(uint8_t reg) {
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  wire_->endTransmission(false); // repeated start, keep the bus
  wire_->requestFrom((int)addr_, 2);
  uint16_t hi = wire_->read();
  uint16_t lo = wire_->read();
  return (uint16_t)((hi << 8) | lo);
}

int16_t Ads1115::readRaw(Mux mux, Gain gain) {
  uint16_t cfg = 0x8000 | (uint16_t)mux | (uint16_t)gain | BASE_CFG;
  if (!writeConfig(cfg)) return 0;

  unsigned long start = millis();
  while (millis() - start < 10) {
    if (readRegister16(REG_CONFIG) & 0x8000) break; // OS bit set = conversion ready
  }
  return (int16_t)readRegister16(REG_CONVERSION);
}

float Ads1115::rangeForGain(Gain gain) {
  switch (gain) {
    case GAIN_TWOTHIRDS: return 6.144f;
    case GAIN_ONE:       return 4.096f;
    case GAIN_TWO:       return 2.048f;
    case GAIN_FOUR:      return 1.024f;
    case GAIN_EIGHT:     return 0.512f;
    case GAIN_SIXTEEN:   return 0.256f;
  }
  return 6.144f;
}

Ads1115::Gain Ads1115::gainForRange(float expectedVolts) {
  float v = fabsf(expectedVolts) * 1.15f; // headroom so we don't clip right at the edge
  if (v <= 0.256f) return GAIN_SIXTEEN;
  if (v <= 0.512f) return GAIN_EIGHT;
  if (v <= 1.024f) return GAIN_FOUR;
  if (v <= 2.048f) return GAIN_TWO;
  if (v <= 4.096f) return GAIN_ONE;
  return GAIN_TWOTHIRDS;
}

float Ads1115::readVoltageAutoRange(Mux mux) {
  int16_t coarseRaw = readRaw(mux, GAIN_TWOTHIRDS);
  float coarseV = (coarseRaw / 32768.0f) * rangeForGain(GAIN_TWOTHIRDS);

  Gain fineGain = gainForRange(coarseV);
  int16_t fineRaw = readRaw(mux, fineGain);
  return (fineRaw / 32768.0f) * rangeForGain(fineGain);
}

bool Ads1115::isPresent() {
  wire_->beginTransmission(addr_);
  return wire_->endTransmission() == 0;
}

void Ads1115::scanBus(TwoWire &wire) {
  Serial.println(F("Scanning I2C bus..."));
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    wire.beginTransmission(addr);
    if (wire.endTransmission() == 0) {
      Serial.print(F("  found device at 0x"));
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (found == 0) Serial.println(F("  no devices found"));
}
