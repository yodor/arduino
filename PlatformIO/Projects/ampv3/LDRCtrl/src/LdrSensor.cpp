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
  auto rd = [this](Ads1115::Mux m) { return ads_.readVoltageAutoRange(m); };
  SenseWiring::Volts v = SenseWiring::measure(topOnA3_, rd);
  if (SenseWiring::wiringLooksWrong(v)) {           // the other board layout: switch and re-read
    topOnA3_ = !topOnA3_;
    Serial.print(F("[sensor ")); Serial.print(label_);
    Serial.print(F("] ADS inputs: top junction is on "));
    Serial.print(topOnA3_ ? F("A3 (second-edition LDR board)") : F("A1 (original LDR board)"));
    Serial.println(F(" -- mapping switched"));
    v = SenseWiring::measure(topOnA3_, rd);
  }
  float vRef = v.vRef, vRs = v.vRs, vRsh = v.vRsh;

  vRefOut = vRef;
  // Require a real margin above zero, not just >0: at very low current
  // (both LDRs' dark end, well beyond anything a 50k constant-impedance
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
