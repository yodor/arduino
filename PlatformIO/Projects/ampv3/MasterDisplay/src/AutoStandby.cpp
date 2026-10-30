#include "AutoStandby.hpp"
#include "Config.hpp"
#include "AudioEngine.hpp"
#include "DisplaySettings.hpp"
#include "PowerControl.hpp"
#include "AttenuatorBoardLink.hpp"

AutoStandby& AutoStandby::instance() {
    static AutoStandby inst;
    return inst;
}

bool AutoStandby::playing() const {
    return m_peak > static_cast<int16_t>(DisplaySettings::instance().getAutoStandbyThreshold());
}

uint32_t AutoStandby::idleMs() const {
    return m_counting ? millis() - m_lastActivityMs : 0;
}

bool AutoStandby::update(bool keyActivity) {
    uint32_t now = millis();

    // The input peak, a few times a second (always measured, so the Input
    // Level page is live even with auto standby off).
    if (now - m_lastCheckMs >= AUTO_STANDBY_CHECK_MS) {
        m_lastCheckMs = now;
        int16_t l[64], r[64];
        AudioEngine::instance().getWaveform(l, r, 64);
        int16_t peak = 0;
        for (int i = 0; i < 64; ++i) {
            int16_t a = static_cast<int16_t>(l[i] < 0 ? -l[i] : l[i]);
            int16_t b = static_cast<int16_t>(r[i] < 0 ? -r[i] : r[i]);
            if (a > peak) peak = a;
            if (b > peak) peak = b;
        }
        m_peak = peak;
    }

    const AttenuatorStatus& s = AttenuatorBoardLink::instance().status();
    bool calibrating = s.valid && s.running();   // a verify too: it holds the amp muted for minutes
    uint8_t minutes  = DisplaySettings::instance().getAutoStandbyMinutes();

    if (keyActivity || playing() || calibrating || !PowerControl::instance().isOn() || minutes == 0) {
        m_lastActivityMs = now; // the timer only runs while on, enabled and idle
        m_counting = false;
        return false;
    }
    m_counting = true;
    if (now - m_lastActivityMs >= static_cast<uint32_t>(minutes) * 60000u) {
        m_lastActivityMs = now;
        m_counting = false;
        return true;
    }
    return false;
}
