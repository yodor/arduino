#pragma once
#include <Arduino.h>

// Whether screens display left/right channels separately or combine them
// into a single summed trace/bar-set.
enum class ChannelMode : uint8_t {
    STEREO,
    MONO
};

// Shared display settings, toggled via UI and consulted by whichever
// screens care (currently: channel mode, respected by BarSpectrumScreen
// and WaveformScreen). A natural home for further global display defaults
// (starting screen, color themes) once a menu system exists to set them.
class DisplaySettings {
public:
    static DisplaySettings& instance();

    ChannelMode getChannelMode() const { return m_channelMode; }
    void setChannelMode(ChannelMode mode) { m_channelMode = mode; }
    void toggleChannelMode() {
        m_channelMode = (m_channelMode == ChannelMode::STEREO) ? ChannelMode::MONO : ChannelMode::STEREO;
    }

    // Index into main.cpp's g_screens[] array to boot into. Not persisted
    // across power cycles yet (resets to 0 on every boot) -- add flash/
    // EEPROM storage later if that's wanted.
    uint8_t getStartingScreenIndex() const { return m_startingScreenIndex; }
    void setStartingScreenIndex(uint8_t idx) { m_startingScreenIndex = idx; }

private:
    DisplaySettings() = default;
    ChannelMode m_channelMode        = ChannelMode::STEREO;
    uint8_t     m_startingScreenIndex = 0;
};