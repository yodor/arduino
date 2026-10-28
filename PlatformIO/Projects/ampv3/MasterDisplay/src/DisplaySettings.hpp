#pragma once
#include <Arduino.h>
#include "Config.hpp"
#include "ScreenRegistry.hpp"

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

// What the Clock screen shows next to HH:MM (Settings -> Clock -> Display).
enum class ClockMode : uint8_t {
    CLOCK_ONLY, // HH:MM, as big as the panel allows
    SECONDS,    // HH:MM:SS
    DATE        // HH:MM with the date in a column on the right
};

// What the Info screen's big value shows (Settings -> Volume Display).
enum class VolumeDisplay : uint8_t {
    DB,     // "-23.5 dB"
    STEPS   // "14/31" -- the current step in white, the total in gray
};

// What the display shows in standby (Settings -> Power -> Standby Display).
enum class StandbyDisplay : uint8_t {
    CLOCK,     // the Clock screen at the normal brightness
    DIM_CLOCK, // the Clock screen at STANDBY_DIM_PERCENT
    OFF        // backlight off -- a key press only wakes it for the power popup
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

    // Backlight brightness, stored as a percentage (0-100) rather than a
    // raw PWM duty value -- a meaningful, portable unit that stays valid
    // even if the hardware's PWM resolution ever changes, converted to
    // the actual 0-255 duty value only at the point of writing it
    // (Renderer::setBacklightPercent()). Deliberately just storage here --
    // this class stays hardware-agnostic like every other setting in it;
    // whoever changes this value (currently only MenuContent.cpp's
    // brightness setters and PersistentSettings::loadAndApply()) is
    // responsible for also calling Renderer::instance().setBacklightPercent()
    // to actually push it to the pin, since unlike every other setting
    // here, there's no per-frame screen render() that would otherwise
    // pick this up on its own.
    uint8_t getBrightnessPercent() const { return m_brightnessPercent; }
    void setBrightnessPercent(uint8_t pct) { m_brightnessPercent = (pct > 100) ? 100 : pct; }

    // Whether SpectrumScreen/DigitalVuMeterScreen draw their peak-hold
    // indicators at all (Theme -> Peaks -> Enabled). Purely read by each
    // screen's own render() each frame -- no hardware/cross-core coupling
    // like brightness above, and no special handling needed when this
    // changes: the menu always forces the active screen through onEnter()
    // on close (see main.cpp's closeMenu()), which already clears the
    // screen and resets every bar's tracked state, so there's never a
    // stale peak mark left behind from toggling this mid-session.
    bool getPeaksEnabled() const { return m_peaksEnabled; }
    void setPeaksEnabled(bool enabled) { m_peaksEnabled = enabled; }

    // Clock screen (Settings -> Clock). Read by ClockScreen when it lays
    // itself out in onEnter() -- the menu always re-enters the active screen
    // on close, so changes apply as soon as the menu closes.
    ClockMode getClockMode() const      { return m_clockMode; }
    void      setClockMode(ClockMode m) { m_clockMode = m; }
    uint16_t  getClockColor() const     { return m_clockColor; }  // RGB565, the custom colour of the lit digits
    void      setClockColor(uint16_t c) { m_clockColor = c; }
    // Clock colour source: the colour theme's middle mix (true), or the
    // custom colour above.
    bool      getClockFollowTheme() const   { return m_clockFollowTheme; }
    void      setClockFollowTheme(bool on)  { m_clockFollowTheme = on; }

    // Auto standby (Settings -> Power): minutes of silence before switching
    // to standby, 0 = off.
    uint8_t getAutoStandbyMinutes() const    { return m_autoStandbyMin; }
    void    setAutoStandbyMinutes(uint8_t m) { m_autoStandbyMin = m; }
    // Input peak (ADC counts) above which the inputs count as playing.
    uint16_t getAutoStandbyThreshold() const    { return m_autoStandbyThreshold; }
    void     setAutoStandbyThreshold(uint16_t t) { m_autoStandbyThreshold = t; }

    // Waveform screen auto-gain (Theme > Wave > Auto Gain), off by default.
    bool getWaveformAutoGain() const   { return m_waveformAutoGain; }
    void setWaveformAutoGain(bool on)  { m_waveformAutoGain = on; }

    VolumeDisplay getVolumeDisplay() const          { return m_volumeDisplay; }
    void          setVolumeDisplay(VolumeDisplay v) { m_volumeDisplay = v; }
    bool getClock24h() const         { return m_clock24h; }
    void setClock24h(bool on)        { m_clock24h = on; }

    StandbyDisplay getStandbyDisplay() const    { return m_standbyDisplay; }
    void setStandbyDisplay(StandbyDisplay mode) { m_standbyDisplay = mode; }

private:
    DisplaySettings() = default;
    ChannelMode  m_channelMode         = ChannelMode::STEREO;
    StereoLayout m_stereoLayout        = StereoLayout::SIDE_BY_SIDE;
    bool         m_mirrorLeftBars      = true; // matches the old MIRROR_LEFT_CHANNEL_BARS default
    uint8_t      m_brightnessPercent   = 100;  // matches the original always-on digitalWrite(HIGH) behavior
    bool         m_peaksEnabled        = true; // matches current always-on behavior
    ClockMode    m_clockMode           = ClockMode::DATE;
    uint16_t     m_clockColor          = 0x07E0; // green
    bool         m_clockFollowTheme    = true;
    bool         m_clock24h            = true;
    StandbyDisplay m_standbyDisplay    = StandbyDisplay::CLOCK;
    uint8_t      m_autoStandbyMin      = 0;      // off
    uint16_t     m_autoStandbyThreshold = AUTO_STANDBY_AUDIO_THRESHOLD;
    VolumeDisplay m_volumeDisplay      = VolumeDisplay::DB;
    bool         m_waveformAutoGain    = false;
};
