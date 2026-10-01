#include "LdrSensor.hpp"

void LdrSensor::activate() {
  wire_.setSDA(sdaPin_);
  wire_.setSCL(sclPin_);
  wire_.begin();
}

void LdrSensor::deactivate() {
  wire_.end();
}

void LdrSensor::scanBus() {
  activate();
  Ads1115::scanBus(wire_);
  deactivate();
}

bool LdrSensor::read(float &vRefOut, float &rsOut, float &rshOut) {
  float vRef = ads_.readVoltageAutoRange(Ads1115::MUX_AIN0_AIN1); // drop across Rref
  float vRs  = ads_.readVoltageAutoRange(Ads1115::MUX_AIN1_AIN3); // drop across Rseries
  float vRsh = ads_.readVoltageAutoRange(Ads1115::MUX_AIN3_GND);  // drop across Rshunt

  vRefOut = vRef;
  // Require a real margin above zero, not just >0: at very low current
  // (both LDRs' dark end, well beyond anything a 10k constant-impedance
  // target ever needs), Vref can read as noise-driven tiny-POSITIVE
  // values that pass a bare >0 check yet are still just noise -- and
  // since Rs/Rsh divide by this, that noise gets amplified into wildly
  // inflated, non-physical resistance values. 10mV is >100x the ADS1115's
  // real noise floor at any gain used here, while still well below any
  // Vref we've ever seen on a genuinely useful (sub-few-kOhm) reading.
  static constexpr float MIN_VALID_VREF = 0.01f;
  if (vRef < MIN_VALID_VREF) return false;

  float current = vRef / rRefOhms_;
  rsOut  = vRs / current;
  rshOut = vRsh / current;
  return true;
}