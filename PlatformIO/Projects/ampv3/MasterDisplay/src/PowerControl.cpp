#include "PowerControl.hpp"
#include "Console.hpp"
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
    // Always starts in standby, relay off. With POWER_ON_AT_BOOT, update()
    // then runs the normal power-on sequence: since protocol revision 10 the
    // daughter keeps the amp muted from power-up until the master releases
    // it, so jumping straight to ON would leave the amp muted for good.
    digitalWrite(RELAY_CTRL_PIN, LOW);
    pinMode(RELAY_CTRL_PIN, OUTPUT);
    digitalWrite(PWR_BTN_LED_PIN, LOW);
    pinMode(PWR_BTN_LED_PIN, OUTPUT);
    m_relayOn     = false;
    m_state       = State::STANDBY;
    m_stateMs     = millis();
    m_bootPowerOn = POWER_ON_AT_BOOT;
}

void PowerControl::setRelay(bool on) {
    m_relayOn = on;
    digitalWrite(RELAY_CTRL_PIN, on ? HIGH : LOW);
    digitalWrite(PWR_BTN_LED_PIN, on ? HIGH : LOW);
    Console::log("Power", "Mains relay %s", on ? "ON" : "OFF");
}

void PowerControl::enter(State s) {
    m_state   = s;
    m_stateMs = millis();
}

void PowerControl::toggle() {
    if (inTransition()) {
        Console::log("Power", "Power key ignored -- a power sequence is running");
        return;
    }
    if (isStandby()) {
        Console::log("Power", "Switching on");
        VolumeControl::instance().ampMuteOn();
        enter(State::MUTING_FOR_ON);
    } else {
        Console::log("Power", "Switching to standby");
        VolumeControl::instance().ampMuteOn();
        enter(State::MUTING_FOR_OFF);
    }
}

void PowerControl::update() {
    if (m_bootPowerOn) {
        m_bootPowerOn = false;
        toggle(); // standby -> the power-on sequence
    }
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
                Console::log("Power", "On");
            }
            break;
        case State::MUTING_FOR_OFF:
            if (muted) {
                setRelay(false);
                enter(State::STANDBY);
                Console::log("Power", "Standby");
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
