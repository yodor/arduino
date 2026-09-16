#pragma once
#include <Arduino.h>

// Whether screens display left/right channels separately or combine them
// into a single summed trace/bar-set.
enum class ChannelMode : uint8_t {
    STEREO,
    MONO
};

// How the two stereo channels are arranged relative to each other when a
// spectrum screen shows both -- independent of ChannelMode above, which
// only controls whether 1 or 2 channels show at all. Only meaningful when
// ChannelMode::STEREO is active; MONO has one shared bar group, nothing to
// arrange.
enum class StereoLayout : uint8_t {
    SIDE_BY_SIDE, // left/right bar groups side by side across the width (formerly BarSpectrumScreen's only layout)
    TOP_BOTTOM    // left grows up from center, right grows down, meeting at a shared centerline (formerly OctaveScreen's only layout)
};

// Shared display settings, toggled via UI and consulted by whichever
// screens care (currently: channel mode, respected by SpectrumScreen and
// WaveformScreen). A natural home for further global display defaults
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

    // SpectrumScreen's stereo arrangement -- see StereoLayout's comment
    // above for what each value means.
    StereoLayout getStereoLayout() const { return m_stereoLayout; }
    void setStereoLayout(StereoLayout layout) { m_stereoLayout = layout; }

    // Only meaningful when StereoLayout::SIDE_BY_SIDE is active -- reverses
    // the left channel's bar order (highest frequency at the outer edge,
    // lowest nearest center) so low frequencies concentrate in the middle
    // of the display and high frequencies push out to both outer edges.
    // The right channel's bars already increase left-to-right with the
    // lowest frequency nearest center, so only the left group needs
    // reordering to match. Purely a rendering-position change -- which
    // array slot tracks which frequency bin's smoothing/peak state is
    // unaffected. Formerly a compile-time constant (MIRROR_LEFT_CHANNEL_
    // BARS in Config.hpp), now a runtime menu setting.
    bool getMirrorLeftBars() const { return m_mirrorLeftBars; }
    void setMirrorLeftBars(bool mirror) { m_mirrorLeftBars = mirror; }

private:
    DisplaySettings() = default;
    ChannelMode  m_channelMode         = ChannelMode::STEREO;
    uint8_t      m_startingScreenIndex = 0;
    StereoLayout m_stereoLayout        = StereoLayout::SIDE_BY_SIDE;
    bool         m_mirrorLeftBars      = true; // matches the old MIRROR_LEFT_CHANNEL_BARS default
};