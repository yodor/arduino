#include "Relay.hpp"

void Relay::begin() {
  // Pre-latch LOW (de-energized/audio) before enabling the output, same
  // glitch-avoidance pattern used for the mute/regulator pins.
  digitalWrite(pin_, LOW);
  pinMode(pin_, OUTPUT);
  energized_ = false;
}

void Relay::energize(bool on) {
  digitalWrite(pin_, on ? HIGH : LOW);
  energized_ = on;
}
