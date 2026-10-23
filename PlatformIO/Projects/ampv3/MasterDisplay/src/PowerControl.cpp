#include "PowerControl.hpp"
#include "Config.hpp"
#include "VolumeControl.hpp"

PowerControl& PowerControl::instance() {
    static PowerControl inst;
    return inst;
}

void PowerControl::init() {
    // Drive the pins to the boot state BEFORE enabling them as outputs, so
    // they never glitch the other way for an instant. (Runs before Serial is
    // up -- main.cpp logs the state later.)
    digitalWrite(RELAY_CTRL_PIN, POWER_ON_AT_BOOT ? HIGH : LOW);
    pinMode(RELAY_CTRL_PIN, OUTPUT);
    digitalWrite(PWR_BTN_LED_PIN, POWER_ON_AT_BOOT ? HIGH : LOW);
    pinMode(PWR_BTN_LED_PIN, OUTPUT);
    m_relayOn = POWER_ON_AT_BOOT;
    // Straight to ON at boot: the daughter holds the amp muted through its
    // own boot hold anyway, which covers the amp's power-up.
    m_state   = POWER_ON_AT_BOOT ? State::ON : State::STANDBY;
    m_stateMs = millis();
}

void PowerControl::setRelay(bool on) {
    m_relayOn = on;
    digitalWrite(RELAY_CTRL_PIN, on ? HIGH : LOW);
    digitalWrite(PWR_BTN_LED_PIN, on ? HIGH : LOW);
    Serial.printf("[Power] Mains relay %s\n", on ? "ON" : "OFF");
}

void PowerControl::enter(State s) {
    m_state   = s;
    m_stateMs = millis();
}

void PowerControl::toggle() {
    if (inTransition()) {
        Serial.println("[Power] Power key ignored -- a power sequence is running");
        return;
    }
    if (isStandby()) {
        Serial.println("[Power] Switching on");
        VolumeControl::instance().ampMuteOn();
        enter(State::MUTING_FOR_ON);
    } else {
        Serial.println("[Power] Switching to standby");
        VolumeControl::instance().ampMuteOn();
        enter(State::MUTING_FOR_OFF);
    }
}

void PowerControl::update() {
    uint32_t elapsed = millis() - m_stateMs;
    bool     muted   = VolumeControl::instance().ampMuteConfirmed() || elapsed >= POWER_MUTE_WAIT_MS;

    switch (m_state) {
        case State::MUTING_FOR_ON:
            if (muted) {
                setRelay(true);
                enter(State::SETTLING);
            }
            break;
        case State::SETTLING:
            if (elapsed >= POWER_ON_SETTLE_MS) {
                VolumeControl::instance().ampMuteOff();
                enter(State::ON);
                Serial.println("[Power] On");
            }
            break;
        case State::MUTING_FOR_OFF:
            if (muted) {
                setRelay(false);
                enter(State::STANDBY);
                Serial.println("[Power] Standby");
            }
            break;
        default:
            break;
    }
}

const char* PowerControl::stepText() const {
    switch (m_state) {
        case State::MUTING_FOR_ON:  return "muting the amp";
        case State::SETTLING:       return "amp starting up";
        case State::MUTING_FOR_OFF: return "muting the amp";
        case State::ON:             return "on";
        default:                    return "mains off";
    }
}

uint8_t PowerControl::percent() const {
    uint32_t elapsed = millis() - m_stateMs;
    uint32_t total   = POWER_MUTE_WAIT_MS + POWER_ON_SETTLE_MS;
    switch (m_state) {
        case State::MUTING_FOR_ON:  return static_cast<uint8_t>((elapsed < POWER_MUTE_WAIT_MS ? elapsed : POWER_MUTE_WAIT_MS) * 100 / total);
        case State::SETTLING:       return static_cast<uint8_t>((POWER_MUTE_WAIT_MS + (elapsed < POWER_ON_SETTLE_MS ? elapsed : POWER_ON_SETTLE_MS)) * 100 / total);
        case State::MUTING_FOR_OFF: return static_cast<uint8_t>((elapsed < POWER_MUTE_WAIT_MS ? elapsed : POWER_MUTE_WAIT_MS) * 100 / POWER_MUTE_WAIT_MS);
        default:                    return 100;
    }
}
