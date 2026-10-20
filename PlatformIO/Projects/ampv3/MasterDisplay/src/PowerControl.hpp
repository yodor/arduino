#pragma once
#include <Arduino.h>

// Mains power for the amplifier: RELAY_CTRL_PIN drives a 2N7000 whose drain
// pulls the SSR's input -- pin HIGH = FET on = SSR on = amp powered.
// PWR_BTN_LED_PIN (the power button's LED) mirrors the state.
//
// Toggled by the remote's power key (IR 0x40 -> UIEvent::POWER_TOGGLE); a
// physical power button (PWR_BTN_PIN) isn't connected yet. This is the
// first step of the standby logic: just the relay, the LED and a log line.
// The state is NOT saved -- after a power cut the unit always comes up in
// the POWER_ON_AT_BOOT state (Config.hpp).
class PowerControl {
public:
    static PowerControl& instance();

    void init();             // pin setup, POWER_ON_AT_BOOT state
    void set(bool on);
    void toggle() { set(!m_on); }
    bool isOn() const { return m_on; }

private:
    PowerControl() = default;
    bool m_on = false;
};
