#include "PowerControl.hpp"
#include "Config.hpp"

PowerControl& PowerControl::instance() {
    static PowerControl inst;
    return inst;
}

void PowerControl::init() {
    // Drive the pin to the boot state BEFORE enabling it as an output, so it
    // never glitches the other way for an instant.
    digitalWrite(RELAY_CTRL_PIN, POWER_ON_AT_BOOT ? HIGH : LOW);
    pinMode(RELAY_CTRL_PIN, OUTPUT);
    digitalWrite(PWR_BTN_LED_PIN, POWER_ON_AT_BOOT ? HIGH : LOW);
    pinMode(PWR_BTN_LED_PIN, OUTPUT);
    m_on = POWER_ON_AT_BOOT; // runs before Serial is up -- main.cpp logs the state later
}

void PowerControl::set(bool on) {
    m_on = on;
    digitalWrite(RELAY_CTRL_PIN, on ? HIGH : LOW);
    digitalWrite(PWR_BTN_LED_PIN, on ? HIGH : LOW);
    Serial.printf("[Power] Amp power %s\n", on ? "ON" : "OFF");
}
