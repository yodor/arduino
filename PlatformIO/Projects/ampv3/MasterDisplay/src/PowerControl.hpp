#pragma once
#include <Arduino.h>

// Amp mains power and the power sequence. RELAY_CTRL_PIN drives a 2N7000
// whose drain pulls the SSR's input -- pin HIGH = FET on = SSR on = amp
// powered. PWR_BTN_LED_PIN (the power button's LED) follows the relay.
//
// The master and the daughter are always powered (only the physical AC
// switch cuts them); "off" here means STANDBY: amp mains off, amp muted.
//
//   switching on:  mute the amp (AMP_MUTE ON, confirmed or POWER_MUTE_WAIT_MS)
//                  -> relay on -> wait POWER_ON_SETTLE_MS for the amp's supply
//                  -> unmute (AMP_MUTE OFF) -> ON
//   switching off: mute the amp (confirmed or POWER_MUTE_WAIT_MS)
//                  -> relay off -> STANDBY (amp left muted)
//
// Toggled by the remote's power key (IR 0x40 -> UIEvent::POWER_TOGGLE) or a
// 3 s hold of RIGHT; a press while a sequence runs is ignored. The state
// isn't saved: the unit boots into standby, or -- with POWER_ON_AT_BOOT
// (Config.hpp) -- runs the power-on sequence right away.
//
// Fits the daughter's revision-10 power-up: it boots with both mutes on and
// never releases them itself. AMP_MUTE is released here (end of the on
// sequence); the LDR mute is released by VolumeControl once CAL=OK, and is
// deliberately left alone in standby, so leaving standby is instant and
// exactly on level.
class PowerControl {
public:
    enum class State : uint8_t { STANDBY, MUTING_FOR_ON, SETTLING, ON, MUTING_FOR_OFF };

    static PowerControl& instance();

    void init();     // pin setup, POWER_ON_AT_BOOT state -- first thing in setup()
    void update();   // call every loop(): advances a running sequence

    void toggle();   // starts the on or off sequence (ignored while one runs)

    State state() const        { return m_state; }
    bool  isOn() const         { return m_state == State::ON; }
    bool  isStandby() const    { return m_state == State::STANDBY; }
    bool  inTransition() const { return !isOn() && !isStandby(); }
    bool  switchingOn() const  { return m_state == State::MUTING_FOR_ON || m_state == State::SETTLING; }
    bool  relayOn() const      { return m_relayOn; }

    // For the power popup: what's happening now, and how far along (0..100).
    const char* stepText() const;
    uint8_t     percent() const;

private:
    PowerControl() = default;
    void setRelay(bool on);
    void enter(State s);

    State    m_state     = State::STANDBY;
    bool     m_relayOn   = false;
    uint32_t m_stateMs   = 0; // when the current state began
    bool     m_bootPowerOn = false; // POWER_ON_AT_BOOT: start the on-sequence on the first update()
};
