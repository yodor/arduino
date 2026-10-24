#pragma once
#include <Arduino.h>

// Auto standby (Settings -> Power): while the unit is on, switches it to
// standby after N minutes without activity -- audio on the inputs, a key or
// remote press, or a calibration running. Off when N is 0.
//
// "Audio on the inputs" = the waveform's peak (DC removed, raw 12-bit ADC
// counts) above the Silence Level setting. The ADC sees the source before
// the volume control, so a paused player counts as silence even at high
// volume. Settings -> Power -> Input Level shows the live peak against the
// threshold, to set it without guessing.
class AutoStandby {
public:
    static AutoStandby& instance();

    // Call every loop(). Returns true once it's time to switch to standby
    // (the caller runs the power sequence).
    bool update(bool keyActivity);

    int16_t  peak() const    { return m_peak; }     // latest input peak, ADC counts
    bool     playing() const;                       // peak above the threshold
    uint32_t idleMs() const;                        // time since the last activity (0 while not counting)
    bool     counting() const { return m_counting; } // on, enabled, and nothing keeping it awake

private:
    AutoStandby() = default;
    uint32_t m_lastActivityMs = 0;
    uint32_t m_lastCheckMs    = 0;
    int16_t  m_peak           = 0;
    bool     m_counting       = false;
};
