#include "MenuContent.hpp"
#include "Console.hpp"
#include "SystemInfo.hpp"
#include "DebugFlags.hpp"
#include "MenuScreen.hpp"
#include "Version.hpp"
#include "ScreenRegistry.hpp"
#include "VolumeControl.hpp"
#include "DisplaySettings.hpp"
#include "ColorTheme.hpp"
#include "AudioEngine.hpp"
#include "SpectrumPipeline.hpp"
#include "WaveformPipeline.hpp"
#include "PersistentSettings.hpp"
#include "Renderer.hpp"
#include "BarPhysics.hpp"
#include "DaughterBoardLink.hpp"
#include "RtcClock.hpp"
#include "DisplayTestScreen.hpp"
#include "AutoStandby.hpp"
#include <Arduino.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

void menuAction_Calibrate() {
    VolumeControl::instance().calibrate();
    Console::log("Menu", "Calibration requested");
}

// --- INFO page helpers -------------------------------------------------

// Appends one formatted line (truncated to the page width); no-op when full.
void addLine(char (*lines)[kInfoLineLen], size_t& n, size_t maxLines, const char* fmt, ...) {
    if (n >= maxLines) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(lines[n], kInfoLineLen, fmt, args);
    va_end(args);
    ++n;
}

// "-23.5" without relying on printf float support.
void formatTenths(char* out, size_t outSize, float v) {
    long t = lroundf(v * 10.0f);
    const char* sign = (t < 0) ? "-" : "";
    if (t < 0) t = -t;
    snprintf(out, outSize, "%s%ld.%ld", sign, t / 10, t % 10);
}

// Appends `text` across as many lines as it needs at kInfoCols characters,
// breaking at spaces where possible; the first line starts with `prefix`,
// the rest are indented by two spaces.
void addWrapped(char (*lines)[kInfoLineLen], size_t& n, size_t maxLines, const char* prefix, const char* text) {
    const char* p = text;
    bool first = true;
    while (*p && n < maxLines) {
        const char* lead = first ? prefix : "  ";
        size_t room = kInfoCols - strlen(lead);
        size_t len  = strlen(p);
        if (len > room) {
            len = room;
            for (size_t i = room; i > room / 2; --i) {
                if (p[i] == ' ') { len = i; break; }
            }
        }
        char chunk[kInfoLineLen];
        memcpy(chunk, p, len);
        chunk[len] = '\0';
        addLine(lines, n, maxLines, "%s%s", lead, chunk);
        p += len;
        while (*p == ' ') ++p;
        first = false;
    }
}

// Calibration -> Status: everything the daughter reports in its
// STATUS line, live (the menu re-fills this twice a second while it's open).
size_t fillCalibrationStatus(char (*lines)[kInfoLineLen], size_t maxLines) {
    // Lines fit kInfoCols (23) characters: the page is shown large.
    size_t n = 0;
    const DaughterBoardLink& link = DaughterBoardLink::instance();
    const DaughterStatus&    s    = link.status();

    if (link.isPassthrough()) {
        addLine(lines, n, maxLines, "Console pass-through");
        return n;
    }
    if (!link.isStatusFresh()) {
        addLine(lines, n, maxLines, "No reply from attenuator");
        if (!s.valid) return n;
        addLine(lines, n, maxLines, "(last known values)");
    } else if (s.proto != DAUGHTER_PROTO_EXPECTED) {
        addLine(lines, n, maxLines, "Protocol %u (need %u)", s.proto, DAUGHTER_PROTO_EXPECTED);
    } else {
        addLine(lines, n, maxLines, "Link OK, protocol %u", s.proto);
    }

    addLine(lines, n, maxLines, "State: %s", s.verifying() ? "VERIFYING" : s.calibrating() ? "CALIBRATING" : (s.busy ? "busy" : "idle"));
    addLine(lines, n, maxLines, "Cal: %s", calStateText(s.cal));
    if (s.lastCal[0] && strcmp(s.lastCal, "NONE") != 0) addLine(lines, n, maxLines, "Last cal: %s", s.lastCal);

    char a[12], b[12];
    if (s.calibrating()) {
        // Per the protocol, every other field is transient during a calibration.
        addLine(lines, n, maxLines, "Volume locked");
    } else {
        if (s.vol < 0) {
            addLine(lines, n, maxLines, "Volume: --");
        } else {
            formatTenths(a, sizeof(a), s.db);
            addLine(lines, n, maxLines, "Vol %d: %s dB", s.vol, a);
            addLine(lines, n, maxLines, "Steps %d-%d", s.volMin, s.volMax);
        }
        addLine(lines, n, maxLines, "Mute %s, amp %s", s.mute ? "ON" : "off", s.ampMute ? "ON" : "off");
        formatTenths(a, sizeof(a), s.rangeDb);
        addLine(lines, n, maxLines, "Range %s dB", a);
        addLine(lines, n, maxLines, "R %luK (%s)", static_cast<unsigned long>((s.rOhms + 500) / 1000), s.rAuto ? "auto" : "manual");
        addLine(lines, n, maxLines, "R range %luK-%luK",
                static_cast<unsigned long>(s.rMinOhms / 1000), static_cast<unsigned long>(s.rMaxOhms / 1000));
    }

    formatTenths(b, sizeof(b), s.tempC);
    addLine(lines, n, maxLines, "MCU %s C", b);
    unsigned long up = s.upSeconds;
    addLine(lines, n, maxLines, "Up %lud %02lu:%02lu:%02lu", up / 86400, (up / 3600) % 24, (up / 60) % 60, up % 60);

    // ENV (rev 8): temperatures, rail, compensation, last stereo match --
    // polled while this page is open.
    DaughterBoardLink::instance().requestEnv();
    const DaughterEnv& e = link.env();
    if (e.valid) {
        addLine(lines, n, maxLines, "Block %s C", e.block);
        addLine(lines, n, maxLines, "Rail %s V", e.rail);
        addLine(lines, n, maxLines, "Comp %s", e.comp);
        addLine(lines, n, maxLines, "Offs L%s R%s", e.offL, e.offR);
        addLine(lines, n, maxLines, "Match %s dB", e.match);
        addLine(lines, n, maxLines, "dBlock %s C", e.dBlock);
        addLine(lines, n, maxLines, "NTC %s", e.ntc);
        if (e.resync >= 0) addLine(lines, n, maxLines, "Resync %s%s%s", e.resync ? "advised" : "not needed",
                                   e.resync ? " " : "", e.resync ? e.why : "");
    }

    if (s.hasErr) addWrapped(lines, n, maxLines, "Err: ", s.err);
    return n;
}

// Settings -> Clock -> Set Time / Set Date: field editors preloaded with the
// RTC's current value. Each saves only its own half -- the other half is
// re-read at the moment of saving, so a minute ticking over while you edit
// the date (or midnight while you edit the time) isn't undone.
bool loadTime(int16_t* v) {
    RtcDateTime t;
    if (!RtcClock::instance().read(t)) return false;
    v[0] = t.hour; v[1] = t.minute; v[2] = t.second;
    return true;
}
void limitsTime(const int16_t*, uint8_t field, int16_t* lo, int16_t* hi) {
    *lo = 0;
    *hi = (field == 0) ? 23 : 59;
}
bool saveTime(const int16_t* v) {
    RtcDateTime t;
    if (!RtcClock::instance().read(t)) return false;
    t.hour = static_cast<uint8_t>(v[0]); t.minute = static_cast<uint8_t>(v[1]); t.second = static_cast<uint8_t>(v[2]);
    bool ok = RtcClock::instance().set(t);
    char buf[20];
    RtcClock::format(t, buf, sizeof(buf));
    Console::log("Clock", "%s %s", ok ? "Time set:" : "Setting the time failed:", buf);
    return ok;
}

bool loadDate(int16_t* v) {
    RtcDateTime t;
    if (!RtcClock::instance().read(t)) return false;
    v[0] = static_cast<int16_t>(t.year); v[1] = t.month; v[2] = t.day;
    return true;
}
void limitsDate(const int16_t* v, uint8_t field, int16_t* lo, int16_t* hi) {
    static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (field == 0) { *lo = 2000; *hi = 2099; return; }
    if (field == 1) { *lo = 1;    *hi = 12;   return; }
    int16_t month = (v[1] >= 1 && v[1] <= 12) ? v[1] : 1;
    bool    leap  = (v[0] % 4 == 0); // exact for 2000-2099
    *lo = 1;
    *hi = (month == 2 && leap) ? 29 : kDays[month - 1];
}
bool saveDate(const int16_t* v) {
    RtcDateTime t;
    if (!RtcClock::instance().read(t)) return false;
    t.year = static_cast<uint16_t>(v[0]); t.month = static_cast<uint8_t>(v[1]); t.day = static_cast<uint8_t>(v[2]);
    bool ok = RtcClock::instance().set(t);
    char buf[20];
    RtcClock::format(t, buf, sizeof(buf));
    Console::log("Clock", "%s %s", ok ? "Date set:" : "Setting the date failed:", buf);
    return ok;
}

const MenuEditor kTimeEditor = { 3, {2, 2, 2}, ':', loadTime, limitsTime, saveTime };
const MenuEditor kDateEditor = { 3, {4, 2, 2}, '-', loadDate, limitsDate, saveDate };

// Clock screen options -- applied when the menu closes (the active screen is
// re-entered, which re-lays the clock out).
void menuSet_ClockOnly()    { DisplaySettings::instance().setClockMode(ClockMode::CLOCK_ONLY); PersistentSettings::instance().markDirty(); }
void menuSet_ClockSeconds() { DisplaySettings::instance().setClockMode(ClockMode::SECONDS);    PersistentSettings::instance().markDirty(); }
void menuSet_ClockDate()    { DisplaySettings::instance().setClockMode(ClockMode::DATE);       PersistentSettings::instance().markDirty(); }
bool menuIsActive_ClockOnly()    { return DisplaySettings::instance().getClockMode() == ClockMode::CLOCK_ONLY; }
bool menuIsActive_ClockSeconds() { return DisplaySettings::instance().getClockMode() == ClockMode::SECONDS; }
bool menuIsActive_ClockDate()    { return DisplaySettings::instance().getClockMode() == ClockMode::DATE; }

// Clock digit color (COLOR chooser).
uint16_t clockColorGet()           { return DisplaySettings::instance().getClockColor(); }
void     clockColorSet(uint16_t c) {
    // Picking a colour means using it: the source switches to Custom.
    DisplaySettings::instance().setClockColor(c);
    DisplaySettings::instance().setClockFollowTheme(false);
    PersistentSettings::instance().markDirty();
}
const MenuColor kClockColor = { clockColorGet, clockColorSet };

// Settings > Clock > Color > Source: the theme's middle mix, or the custom colour.
void menuSet_ClockColorTheme()  { DisplaySettings::instance().setClockFollowTheme(true);  PersistentSettings::instance().markDirty(); }
void menuSet_ClockColorCustom() { DisplaySettings::instance().setClockFollowTheme(false); PersistentSettings::instance().markDirty(); }
bool menuIsActive_ClockColorTheme()  { return  DisplaySettings::instance().getClockFollowTheme(); }
bool menuIsActive_ClockColorCustom() { return !DisplaySettings::instance().getClockFollowTheme(); }
const MenuValueOption kClockColorSourceOptions[] = {
    { "Theme",  menuSet_ClockColorTheme,  menuIsActive_ClockColorTheme  },
    { "Custom", menuSet_ClockColorCustom, menuIsActive_ClockColorCustom },
};
const MenuItem kClockColorSubmenuItems[] = {
    { "Source",       MenuItemType::VALUE_CHOICE, nullptr, kClockColorSourceOptions, 2 },
    { "Custom Color", MenuItemType::COLOR,        nullptr, nullptr, 0, nullptr, 0, nullptr, nullptr, &kClockColor },
};
void menuSet_Clock24h()      { DisplaySettings::instance().setClock24h(true);          PersistentSettings::instance().markDirty(); }
void menuSet_Clock12h()      { DisplaySettings::instance().setClock24h(false);         PersistentSettings::instance().markDirty(); }
bool menuIsActive_Clock24h()     { return DisplaySettings::instance().getClock24h(); }
bool menuIsActive_Clock12h()     { return !DisplaySettings::instance().getClock24h(); }

void menuSet_Stereo() { DisplaySettings::instance().setChannelMode(ChannelMode::STEREO); PersistentSettings::instance().markDirty(); }
void menuSet_Mono()   { DisplaySettings::instance().setChannelMode(ChannelMode::MONO);   PersistentSettings::instance().markDirty(); }
bool menuIsActive_Stereo() { return DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO; }
bool menuIsActive_Mono()   { return DisplaySettings::instance().getChannelMode() == ChannelMode::MONO; }

void menuSet_ColorClassic()  { ColorTheme::instance().setPreset(ColorPreset::CLASSIC);  PersistentSettings::instance().markDirty(); }
void menuSet_ColorSunset()   { ColorTheme::instance().setPreset(ColorPreset::SUNSET);   PersistentSettings::instance().markDirty(); }
void menuSet_ColorAurora()   { ColorTheme::instance().setPreset(ColorPreset::AURORA);   PersistentSettings::instance().markDirty(); }
void menuSet_ColorNastyFFT() { ColorTheme::instance().setPreset(ColorPreset::NASTYFFT); PersistentSettings::instance().markDirty(); }
bool menuIsActive_ColorClassic()  { return ColorTheme::instance().getPreset() == ColorPreset::CLASSIC; }
bool menuIsActive_ColorSunset()   { return ColorTheme::instance().getPreset() == ColorPreset::SUNSET; }
bool menuIsActive_ColorAurora()   { return ColorTheme::instance().getPreset() == ColorPreset::AURORA; }
bool menuIsActive_ColorNastyFFT() { return ColorTheme::instance().getPreset() == ColorPreset::NASTYFFT; }

// Band Layout: Doubling (derived bar count from FFT_SIZE, formerly
// Bar9Pipeline's only behavior) vs Fixed Bands (31-band ISO table,
// formerly OctavePipeline's only behavior) vs Full (uncompressed, evenly-
// sliced FFT resolution -- as many bands as the screen can show, see
// FULL_BAND_COUNT's comment in SpectrumPipeline.hpp for how that's
// derived). Set on core0 here, applied on core1 inside SpectrumPipeline::
// pushChunk() -- see that class's comment for why this is safe without
// locking on this platform.
void menuSet_FftDoubling() { AudioEngine::instance().setSpectrumBandLayoutMode(SpectrumPipeline::BandLayoutMode::DOUBLING);    PersistentSettings::instance().markDirty(); }
void menuSet_FftFixed()    { AudioEngine::instance().setSpectrumBandLayoutMode(SpectrumPipeline::BandLayoutMode::FIXED_BANDS); PersistentSettings::instance().markDirty(); }
void menuSet_FftFull()     { AudioEngine::instance().setSpectrumBandLayoutMode(SpectrumPipeline::BandLayoutMode::FULL);        PersistentSettings::instance().markDirty(); }
bool menuIsActive_FftDoubling() { return AudioEngine::instance().getSpectrumBandLayoutMode() == SpectrumPipeline::BandLayoutMode::DOUBLING; }
bool menuIsActive_FftFixed()    { return AudioEngine::instance().getSpectrumBandLayoutMode() == SpectrumPipeline::BandLayoutMode::FIXED_BANDS; }
bool menuIsActive_FftFull()     { return AudioEngine::instance().getSpectrumBandLayoutMode() == SpectrumPipeline::BandLayoutMode::FULL; }

// Energy Mode: Peak (formerly Bar9Pipeline's only aggregation) vs
// Quadrature Sum (formerly OctavePipeline's only aggregation) -- now
// independently selectable from Band Layout above.
void menuSet_EnergyPeak() { AudioEngine::instance().setSpectrumEnergyMode(SpectrumPipeline::EnergyMode::PEAK);           PersistentSettings::instance().markDirty(); }
void menuSet_EnergyQuad() { AudioEngine::instance().setSpectrumEnergyMode(SpectrumPipeline::EnergyMode::QUADRATURE_SUM); PersistentSettings::instance().markDirty(); }
bool menuIsActive_EnergyPeak() { return AudioEngine::instance().getSpectrumEnergyMode() == SpectrumPipeline::EnergyMode::PEAK; }
bool menuIsActive_EnergyQuad() { return AudioEngine::instance().getSpectrumEnergyMode() == SpectrumPipeline::EnergyMode::QUADRATURE_SUM; }

// Stereo Layout: how SpectrumScreen arranges the two channels when both
// are shown (only meaningful in Stereo channel mode) -- purely a core0/
// display-side setting, no cross-core concern like the two above.
void menuSet_LayoutSideBySide() { DisplaySettings::instance().setStereoLayout(StereoLayout::SIDE_BY_SIDE); PersistentSettings::instance().markDirty(); }
void menuSet_LayoutTopBottom()  { DisplaySettings::instance().setStereoLayout(StereoLayout::TOP_BOTTOM);   PersistentSettings::instance().markDirty(); }
bool menuIsActive_LayoutSideBySide() { return DisplaySettings::instance().getStereoLayout() == StereoLayout::SIDE_BY_SIDE; }
bool menuIsActive_LayoutTopBottom()  { return DisplaySettings::instance().getStereoLayout() == StereoLayout::TOP_BOTTOM; }

// Mirror Left Bars: only meaningful when Stereo Layout is Side-by-Side --
// SpectrumScreen simply ignores this setting under Top-Bottom, rather than
// hiding the menu item itself (this menu system doesn't support
// conditionally-visible items).
void menuSet_MirrorOn()  { DisplaySettings::instance().setMirrorLeftBars(true);  PersistentSettings::instance().markDirty(); }
void menuSet_MirrorOff() { DisplaySettings::instance().setMirrorLeftBars(false); PersistentSettings::instance().markDirty(); }
bool menuIsActive_MirrorOn()  { return DisplaySettings::instance().getMirrorLeftBars(); }
bool menuIsActive_MirrorOff() { return !DisplaySettings::instance().getMirrorLeftBars(); }

// Brightness: unlike every other setting above, this one has no per-frame
// screen render() that would pick up a stored value on its own -- the
// setter has to also push it to hardware immediately, right here, not
// just record it for later. Template rather than four near-identical
// functions.
template <uint8_t Pct>
void menuSet_Brightness() {
    DisplaySettings::instance().setBrightnessPercent(Pct);
    Renderer::instance().setBacklightPercent(Pct);
    PersistentSettings::instance().markDirty();
}

template <uint8_t Pct>
bool menuIsActive_Brightness() { return DisplaySettings::instance().getBrightnessPercent() == Pct; }

// Wave Window Mode: Oscilloscope (kOscilloscopeSamples, a genuine "few
// cycles" view of waveform shape) vs Track Preview (kTrackPreviewSamples,
// a much longer span showing amplitude envelope instead -- see
// WaveformPipeline.hpp for the full reasoning behind both). Same core0-
// sets/core1-applies pattern as the FFT settings above.
void menuSet_WaveOscilloscope() { AudioEngine::instance().setWaveformWindowMode(WaveformPipeline::WindowMode::OSCILLOSCOPE);  PersistentSettings::instance().markDirty(); }
void menuSet_WaveTrackPreview() { AudioEngine::instance().setWaveformWindowMode(WaveformPipeline::WindowMode::TRACK_PREVIEW); PersistentSettings::instance().markDirty(); }
bool menuIsActive_WaveOscilloscope() { return AudioEngine::instance().getWaveformWindowMode() == WaveformPipeline::WindowMode::OSCILLOSCOPE; }
bool menuIsActive_WaveTrackPreview() { return AudioEngine::instance().getWaveformWindowMode() == WaveformPipeline::WindowMode::TRACK_PREVIEW; }
void menuSet_WaveDaw()      { AudioEngine::instance().setWaveformWindowMode(WaveformPipeline::WindowMode::DAW); PersistentSettings::instance().markDirty(); }
bool menuIsActive_WaveDaw() { return AudioEngine::instance().getWaveformWindowMode() == WaveformPipeline::WindowMode::DAW; }

// Peaks Enabled: whether SpectrumScreen/DigitalVuMeterScreen draw their
// peak-hold indicators at all -- purely a core0/display-side setting (see
// DisplaySettings::setPeaksEnabled()'s comment for why no special
// clear/reset handling is needed here beyond the menu's existing onEnter()
// behavior on close).
// Theme > Peaks: Off hides the peak markers; the others set how fast they
// fall (BarPhysics: hold, then an accelerating fall).
void setPeaks(bool on, PeakDecaySpeed speed) {
    DisplaySettings::instance().setPeaksEnabled(on);
    if (on) BarPhysics::instance().setPeakDecaySpeed(speed);
    PersistentSettings::instance().markDirty();
}
bool peaksAre(bool on, PeakDecaySpeed speed) {
    bool enabled = DisplaySettings::instance().getPeaksEnabled();
    return on ? (enabled && BarPhysics::instance().getPeakDecaySpeed() == speed) : !enabled;
}
void menuSet_PeaksOff()     { setPeaks(false, PeakDecaySpeed::NORMAL); }
void menuSet_PeaksTurtle()  { setPeaks(true, PeakDecaySpeed::TURTLE); }
void menuSet_PeaksSlow()    { setPeaks(true, PeakDecaySpeed::SLOW); }
void menuSet_PeaksNormal()  { setPeaks(true, PeakDecaySpeed::NORMAL); }
void menuSet_PeaksFast()    { setPeaks(true, PeakDecaySpeed::FAST); }
void menuSet_PeaksCheetah() { setPeaks(true, PeakDecaySpeed::CHEETAH); }
bool menuIsActive_PeaksOff()     { return peaksAre(false, PeakDecaySpeed::NORMAL); }
bool menuIsActive_PeaksTurtle()  { return peaksAre(true, PeakDecaySpeed::TURTLE); }
bool menuIsActive_PeaksSlow()    { return peaksAre(true, PeakDecaySpeed::SLOW); }
bool menuIsActive_PeaksNormal()  { return peaksAre(true, PeakDecaySpeed::NORMAL); }
bool menuIsActive_PeaksFast()    { return peaksAre(true, PeakDecaySpeed::FAST); }
bool menuIsActive_PeaksCheetah() { return peaksAre(true, PeakDecaySpeed::CHEETAH); }

const MenuValueOption kColorOptions[] = {
    { "Classic",  menuSet_ColorClassic,  menuIsActive_ColorClassic  },
    { "NastyFFT", menuSet_ColorNastyFFT, menuIsActive_ColorNastyFFT },
    { "Sunset",   menuSet_ColorSunset,   menuIsActive_ColorSunset   },
    { "Aurora",   menuSet_ColorAurora,   menuIsActive_ColorAurora   },
};

const MenuValueOption kChannelModeOptions[] = {
    { "Stereo", menuSet_Stereo, menuIsActive_Stereo },
    { "Mono",   menuSet_Mono,   menuIsActive_Mono   },
};

const MenuValueOption kFftLayoutOptions[] = {
    { "Doubling",    menuSet_FftDoubling, menuIsActive_FftDoubling },
    { "Fixed Bands", menuSet_FftFixed,    menuIsActive_FftFixed    },
    { "Full",        menuSet_FftFull,     menuIsActive_FftFull     },
};

const MenuValueOption kFftEnergyOptions[] = {
    { "Peak",           menuSet_EnergyPeak, menuIsActive_EnergyPeak },
    { "Quadrature Sum", menuSet_EnergyQuad, menuIsActive_EnergyQuad },
};

const MenuValueOption kStereoLayoutOptions[] = {
    { "Side-by-Side", menuSet_LayoutSideBySide, menuIsActive_LayoutSideBySide },
    { "Top-Bottom",   menuSet_LayoutTopBottom,  menuIsActive_LayoutTopBottom  },
};

const MenuValueOption kMirrorOptions[] = {
    { "On",  menuSet_MirrorOn,  menuIsActive_MirrorOn  },
    { "Off", menuSet_MirrorOff, menuIsActive_MirrorOff },
};

// "FFT" submenu, nested inside "Theme" -- the four FFT/spectrum-layout
// settings. Declared before kThemeSubmenuItems below since C++ needs a
// referenced array fully defined before the array that points to it.
const MenuItem kFftSubmenuItems[] = {
    { "Stereo Layout",    MenuItemType::VALUE_CHOICE, nullptr, kStereoLayoutOptions, 2 },
    { "Band Layout",      MenuItemType::VALUE_CHOICE, nullptr, kFftLayoutOptions,    3 },
    { "Energy Mode",      MenuItemType::VALUE_CHOICE, nullptr, kFftEnergyOptions,    2 },
    { "Mirror Left Bars", MenuItemType::VALUE_CHOICE, nullptr, kMirrorOptions,       2 },
};
constexpr size_t kFftSubmenuItemCount = sizeof(kFftSubmenuItems) / sizeof(kFftSubmenuItems[0]);

const MenuValueOption kWaveWindowModeOptions[] = {
    { "Oscilloscope",  menuSet_WaveOscilloscope, menuIsActive_WaveOscilloscope },
    { "Track Preview", menuSet_WaveTrackPreview, menuIsActive_WaveTrackPreview },
    { "DAW",           menuSet_WaveDaw,          menuIsActive_WaveDaw          },
};

// "Wave" submenu, nested inside "Theme" -- currently just the one
// setting (Window Mode), with room to grow the same way "FFT" did.
// Theme > Wave > Auto Gain: the waveform trace scaled to fill its lane.
void menuSet_WaveGainOn()  { DisplaySettings::instance().setWaveformAutoGain(true);  PersistentSettings::instance().markDirty(); }
void menuSet_WaveGainOff() { DisplaySettings::instance().setWaveformAutoGain(false); PersistentSettings::instance().markDirty(); }
bool menuIsActive_WaveGainOn()  { return  DisplaySettings::instance().getWaveformAutoGain(); }
bool menuIsActive_WaveGainOff() { return !DisplaySettings::instance().getWaveformAutoGain(); }
const MenuValueOption kWaveAutoGainOptions[] = {
    { "On",  menuSet_WaveGainOn,  menuIsActive_WaveGainOn  },
    { "Off", menuSet_WaveGainOff, menuIsActive_WaveGainOff },
};

const MenuItem kWaveSubmenuItems[] = {
    { "Window Mode", MenuItemType::VALUE_CHOICE, nullptr, kWaveWindowModeOptions, 3 },
    { "Auto Gain",   MenuItemType::VALUE_CHOICE, nullptr, kWaveAutoGainOptions,   2 },
};
constexpr size_t kWaveSubmenuItemCount = sizeof(kWaveSubmenuItems) / sizeof(kWaveSubmenuItems[0]);

// Theme > Peaks -- the peak markers on the Spectrum and Digital VU screens.
const MenuValueOption kPeaksOptions[] = {
    { "Off",     menuSet_PeaksOff,     menuIsActive_PeaksOff     },
    { "Turtle",  menuSet_PeaksTurtle,  menuIsActive_PeaksTurtle  },
    { "Slow",    menuSet_PeaksSlow,    menuIsActive_PeaksSlow    },
    { "Normal",  menuSet_PeaksNormal,  menuIsActive_PeaksNormal  },
    { "Fast",    menuSet_PeaksFast,    menuIsActive_PeaksFast    },
    { "Cheetah", menuSet_PeaksCheetah, menuIsActive_PeaksCheetah },
};

// "Theme" submenu, nested at root -- display/appearance settings grouped
// together, with "FFT", "Wave", and "Peaks" themselves one further level in.
const MenuItem kThemeSubmenuItems[] = {
    { "Stereo / Mono", MenuItemType::VALUE_CHOICE, nullptr, kChannelModeOptions, 2 },
    { "Colors",        MenuItemType::VALUE_CHOICE, nullptr, kColorOptions,       4 },
    { "FFT",           MenuItemType::SUBMENU,      nullptr, nullptr, 0, kFftSubmenuItems,   kFftSubmenuItemCount },
    { "Wave",          MenuItemType::SUBMENU,      nullptr, nullptr, 0, kWaveSubmenuItems,  kWaveSubmenuItemCount },
    { "Peaks",         MenuItemType::VALUE_CHOICE, nullptr, kPeaksOptions, 6 },
};
constexpr size_t kThemeSubmenuItemCount = sizeof(kThemeSubmenuItems) / sizeof(kThemeSubmenuItems[0]);

const MenuValueOption kBrightnessOptions[] = {
    { "25%",  menuSet_Brightness<25>,  menuIsActive_Brightness<25>  },
    { "50%",  menuSet_Brightness<50>,  menuIsActive_Brightness<50>  },
    { "75%",  menuSet_Brightness<75>,  menuIsActive_Brightness<75>  },
    { "100%", menuSet_Brightness<100>, menuIsActive_Brightness<100> },
};

// --- Settings -> Clock ---
const MenuValueOption kClockModeOptions[] = {
    { "Clock only", menuSet_ClockOnly,    menuIsActive_ClockOnly    },
    { "Seconds",    menuSet_ClockSeconds, menuIsActive_ClockSeconds },
    { "Date",       menuSet_ClockDate,    menuIsActive_ClockDate    },
};const MenuValueOption kClockFormatOptions[] = {
    { "24 hour", menuSet_Clock24h, menuIsActive_Clock24h },
    { "12 hour", menuSet_Clock12h, menuIsActive_Clock12h },
};
const MenuItem kClockSubmenuItems[] = {
    { "Set Time",     MenuItemType::EDITOR,       nullptr, nullptr, 0, nullptr, 0, nullptr, &kTimeEditor },
    { "Set Date",     MenuItemType::EDITOR,       nullptr, nullptr, 0, nullptr, 0, nullptr, &kDateEditor },
    { "Display",      MenuItemType::VALUE_CHOICE, nullptr, kClockModeOptions,   3 },
    { "Format",       MenuItemType::VALUE_CHOICE, nullptr, kClockFormatOptions, 2 },
    { "Color",        MenuItemType::SUBMENU,      nullptr, nullptr, 0, kClockColorSubmenuItems, 2 },
};
constexpr size_t kClockSubmenuItemCount = sizeof(kClockSubmenuItems) / sizeof(kClockSubmenuItems[0]);

// --- Calibration ---
//
// Rtotal: the Rs+Rsh the daughter's volume steps are solved for. The choice
// made here is only a request for the NEXT calibration ("Start"): it starts
// out as whatever the daughter currently uses (RAUTO / R from STATUS), and
// Start sends plain "CAL" when the choice equals that (the daughter then
// decides how much work it needs), or "CAL R=AUTO" / "CAL R=<ohms>" when it
// was changed (the daughter re-solves from its stored measurements).

// The Rtotal picked in the selector, used by the Start right after it.
bool     g_rSelAuto = true;
uint32_t g_rSelOhms = 0;

// The start option's label names what will run: "Start R=AUTO" or
// "Start R=650K" (MenuScreen reads the label each time it draws).

void menuAction_StartCalibration();   // below: sends CAL with the chosen R

// "Rtotal" selector: ONE field covering both modes -- index 0 is AUTO,
// then RMIN..RMAX (the daughter's measured range) in kRStepOhms steps,
// rounded inward. It opens on the daughter's current setting; Up/Down step
// (wrapping), Right confirms, then Cancel / Start.
constexpr uint32_t kRStepOhms = 5000;
int16_t g_rLoadedIndex = -1; // what the selector opened on (unchanged = the exact current value)

uint32_t rFirstOhms() {
    uint32_t rmin = DaughterBoardLink::instance().status().rMinOhms;
    return ((rmin + kRStepOhms - 1) / kRStepOhms) * kRStepOhms;
}
int16_t rStepCount() {
    const DaughterStatus& s = DaughterBoardLink::instance().status();
    if (s.rMinOhms == 0 || s.rMaxOhms == 0) return 0; // range not measured yet: AUTO only
    uint32_t first = rFirstOhms(), last = (s.rMaxOhms / kRStepOhms) * kRStepOhms;
    return (last < first) ? 0 : static_cast<int16_t>((last - first) / kRStepOhms + 1);
}
uint32_t rIndexOhms(int16_t i) { return rFirstOhms() + static_cast<uint32_t>(i - 1) * kRStepOhms; }

// Doesn't open while the daughter is busy or not answering -- the item then
// shows the progress page instead (a calibration running is the usual case).
// Before a full characterization has measured RMIN/RMAX (a fresh unit) it
// offers AUTO only, so the first calibration can always be started.
bool loadRTotal(int16_t* v) {
    const DaughterBoardLink& link = DaughterBoardLink::instance();
    const DaughterStatus&    s    = link.status();
    if (!link.isStatusFresh() || s.busy) return false;
    if (s.rAuto || rStepCount() == 0) {
        v[0] = 0;
    } else {
        int32_t i = 1 + (static_cast<int32_t>(s.rOhms) - static_cast<int32_t>(rFirstOhms()) + static_cast<int32_t>(kRStepOhms / 2)) / static_cast<int32_t>(kRStepOhms);
        if (i < 1) i = 1;
        if (i > rStepCount()) i = rStepCount();
        v[0] = static_cast<int16_t>(i);
    }
    g_rLoadedIndex = v[0];
    return true;
}
void limitsRTotal(const int16_t*, uint8_t, int16_t* lo, int16_t* hi) {
    *lo = 0;
    *hi = rStepCount();
}
void formatRTotal(const int16_t* v, uint8_t, char* out, size_t outSize) {
    if (v[0] == 0) snprintf(out, outSize, "AUTO");
    else           snprintf(out, outSize, "%3luK", static_cast<unsigned long>(rIndexOhms(v[0]) / 1000));
}
bool saveRTotal(const int16_t* v) {
    const DaughterStatus& s = DaughterBoardLink::instance().status();
    if (v[0] == g_rLoadedIndex) {          // unchanged: exactly what the daughter uses now
        g_rSelAuto = s.rAuto;
        g_rSelOhms = s.rOhms;
    } else if (v[0] == 0) {
        g_rSelAuto = true;
    } else {
        g_rSelAuto = false;
        g_rSelOhms = rIndexOhms(v[0]);
    }
    menuAction_StartCalibration();   // Right on the value runs it: no Cancel / Start step
    return true;
}
// Under the item in the list: the daughter's current Rtotal, e.g. "650K (auto)".
void describeRTotal(char* out, size_t outSize) {
    const DaughterBoardLink& link = DaughterBoardLink::instance();
    const DaughterStatus&    s    = link.status();
    if (!link.isStatusFresh() || s.rOhms == 0) { snprintf(out, outSize, "--"); return; }
    snprintf(out, outSize, "%luK (%s)", static_cast<unsigned long>((s.rOhms + 500) / 1000), s.rAuto ? "auto" : "manual");
}
const MenuEditor kRTotalEditor = { 1, {4}, ' ', loadRTotal, limitsRTotal, saveRTotal, formatRTotal, describeRTotal };

// Start: plain CAL if the selection equals what the daughter uses now (it
// then decides how much work it needs), otherwise CAL R=AUTO / R=<ohms>.
void menuAction_StartCalibration() {
    const DaughterStatus& s = DaughterBoardLink::instance().status();
    VolumeControl& vc = VolumeControl::instance();
    bool same = s.valid && (g_rSelAuto ? s.rAuto : (!s.rAuto && s.rOhms == g_rSelOhms));
    if (same) {
        vc.calibrate(VolumeControl::CalR::KEEP_R);
        Console::log("Menu", "Calibration requested");
    } else if (g_rSelAuto) {
        vc.calibrate(VolumeControl::CalR::AUTO_R);
        Console::log("Menu", "Calibration requested, Rtotal AUTO");
    } else {
        vc.calibrate(VolumeControl::CalR::SET_R, g_rSelOhms);
        Console::log("Menu", "Calibration requested, Rtotal %lu ohm", static_cast<unsigned long>(g_rSelOhms));
    }
}

// Progress page labels for each PROGRESS phase.
const char* phaseLabel(const char* phase) {
    // Short enough to fit a large info line together with side and step.
    if (!strcmp(phase, "CHECK"))        return "Checking cells";
    if (!strcmp(phase, "SWEEP_SERIES")) return "Series sweep";
    if (!strcmp(phase, "SWEEP_SHUNT"))  return "Shunt sweep";
    if (!strcmp(phase, "WALK"))         return "Dark-end walk";
    if (!strcmp(phase, "SOLVE"))        return "Solving steps";
    if (!strcmp(phase, "TRIM"))         return "Trimming";
    if (!strcmp(phase, "HOLD"))         return "Settling";
    if (!strcmp(phase, "SEEK"))         return "Stereo match";
    if (!strcmp(phase, "SAVE"))         return "Saving";
    if (!strcmp(phase, "STEPS"))        return "Step levels";     // verify (rev 20)
    if (!strcmp(phase, "CREEP"))        return "Creep test";      // verify (rev 20)
    if (!strcmp(phase, "DONE"))         return "Done";
    return "Starting";
}

// Calibration -> Start -> "Start now" lands here: live progress
// while it runs (from PROGRESS, polled every second), the result afterwards.
size_t fillCalibrationProgress(char (*lines)[kInfoLineLen], size_t maxLines) {
    // Lines fit kInfoCols (23) characters: the page is shown large.
    size_t n = 0;
    DaughterBoardLink& link = DaughterBoardLink::instance();
    const DaughterStatus&   s  = link.status();
    const DaughterProgress& pr = link.progress();
    link.requestEnv();

    if (!link.isStatusFresh()) {
        addLine(lines, n, maxLines, "No reply from attenuator");
        return n;
    }

    if (s.running()) {   // a calibration, or a verify (rev 20: BUSY with CAL=OK)
        bool known = pr.valid && pr.kind[0] && strcmp(pr.kind, "NONE") != 0;
        if (s.verifying() || (known && !strcmp(pr.kind, "VERIFY"))) addLine(lines, n, maxLines, "Verifying");
        else if (known) addLine(lines, n, maxLines, "Calibrating %s", pr.kind);
        else            addLine(lines, n, maxLines, "Calibrating");
        if (pr.valid) {
            addLine(lines, n, maxLines, "%s%s%s%s%s", phaseLabel(pr.phase),
                    pr.side[0] ? " " : "", pr.side, pr.step[0] ? " " : "", pr.step);
            int pct = (pr.pct < 0) ? 0 : (pr.pct > 100 ? 100 : pr.pct);
            constexpr int kBarW = 14;
            char bar[kInfoLineLen];
            int filled = pct * kBarW / 100;
            bar[0] = '[';
            for (int i = 0; i < kBarW; ++i) bar[1 + i] = (i < filled) ? '#' : '-';
            snprintf(bar + 1 + kBarW, sizeof(bar) - 1 - kBarW, "] %d%%", pct);
            addLine(lines, n, maxLines, "%s", bar);
            if (pr.etaS >= 0) addLine(lines, n, maxLines, "%ld min %02ld s left", static_cast<long>(pr.etaS / 60), static_cast<long>(pr.etaS % 60));
        } else {
            addLine(lines, n, maxLines, "Waiting for progress");
        }
        char t[12];
        formatTenths(t, sizeof(t), s.tempC);
        addLine(lines, n, maxLines, "MCU %s, block %s C", t, link.env().valid ? link.env().block : "--");
        addLine(lines, n, maxLines, "Amp muted meanwhile");
        return n;
    }

    if (link.calStart() == DaughterCalStart::PENDING || VolumeControl::instance().isCalibrationPending()) {
        addLine(lines, n, maxLines, "Starting...");
        return n;
    }
    if (link.calStart() == DaughterCalStart::REJECTED) {
        addLine(lines, n, maxLines, "Did not start:");
        addWrapped(lines, n, maxLines, "  ", link.getCalStartError());
        return n;
    }

    // Idle: the result of the last calibration, and of the last verify.
    addLine(lines, n, maxLines, "Result: %s", calStateText(s.cal));
    const DaughterVerify& vr = link.verifyResult();
    if (vr.valid && vr.seq > 0) addLine(lines, n, maxLines, "Verify: %s", vr.verdict);
    if (s.lastCal[0]) addLine(lines, n, maxLines, "Last: %s", s.lastCal);
    if (s.cal == DaughterCalState::ERR && s.hasErr) addWrapped(lines, n, maxLines, "  ", s.err);
    if (link.env().valid) addLine(lines, n, maxLines, "Match %s dB", link.env().match);
    char a[12];
    formatTenths(a, sizeof(a), s.rangeDb);
    addLine(lines, n, maxLines, "R %luK (%s)", static_cast<unsigned long>((s.rOhms + 500) / 1000), s.rAuto ? "auto" : "manual");
    addLine(lines, n, maxLines, "Range %s dB", a);
    return n;
}

// Calibration -> Temperature: the driver block's NTC (the
// temperature the thermal compensation works from), the change since the
// last calibration, and whether the daughter recommends a resync (rev 13/14).
size_t fillTemperature(char (*lines)[kInfoLineLen], size_t maxLines) {
    size_t n = 0;
    DaughterBoardLink& link = DaughterBoardLink::instance();
    link.requestEnv();
    const DaughterEnv&    e = link.env();
    const DaughterStatus& s = link.status();
    if (!link.isStatusFresh()) { addLine(lines, n, maxLines, "No reply from attenuator"); return n; }
    if (!e.valid)              { addLine(lines, n, maxLines, "Waiting for data...");    return n; }

    if (e.ntcOk()) addLine(lines, n, maxLines, "Block %s C", e.block);
    else           addLine(lines, n, maxLines, "Block NTC %s!", e.ntc);
    char t[12];
    formatTenths(t, sizeof(t), s.tempC);
    addLine(lines, n, maxLines, "MCU %s C", t);
    addLine(lines, n, maxLines, "Since cal %s C", e.dBlock);
    addLine(lines, n, maxLines, "Comp %s", e.comp);
    if (e.resync == 1) {
        const char* why = !strcmp(e.why, "SPAN")   ? "beyond learned range"
                        : !strcmp(e.why, "LIMIT")  ? "30 C from cal temp"
                        : !strcmp(e.why, "NOCOMP") ? "no comp, moved >1 C"
                        : !strcmp(e.why, "SEEK")   ? "seek didn't settle"
                        : !strcmp(e.why, "VERIFY") ? "last verify: drift"
                                                   : e.why;
        addLine(lines, n, maxLines, "Resync ADVISED");
        addLine(lines, n, maxLines, "  %s", why);
    } else if (e.resync == 0 && !strcmp(e.why, "WARMING")) {
        // rev 17: a resync would apply, but the temperature is still settling.
        addLine(lines, n, maxLines, "Resync after warm-up");
        addLine(lines, n, maxLines, "  temperature settling");
    } else if (e.resync == 0) {
        addLine(lines, n, maxLines, "Resync not needed");
    }
    return n;
}

// Calibration -> Learning: the daughter's thermal learning
// (rev 9 LEARN) -- how many resyncs it has, how far the block temperature
// has varied, and the per-side fit against the bar it must clear.
size_t fillLearning(char (*lines)[kInfoLineLen], size_t maxLines) {
    // Lines fit kInfoCols (23) characters: the page is shown large.
    size_t n = 0;
    DaughterBoardLink& link = DaughterBoardLink::instance();
    link.requestLearn();
    const DaughterLearn& l = link.learn();
    if (!link.isStatusFresh()) {
        addLine(lines, n, maxLines, "No reply from attenuator");
        return n;
    }
    if (!l.valid) {
        addLine(lines, n, maxLines, "Waiting for data...");
        return n;
    }
    addLine(lines, n, maxLines, "Compensation %s", l.comp);
    addLine(lines, n, maxLines, "Resyncs %s (need %s)", l.n, l.minN);
    addLine(lines, n, maxLines, "Spread %s C", l.spread);
    addLine(lines, n, maxLines, "  (need %s C)", l.minSpread);
    addLine(lines, n, maxLines, "L: k %s R2 %s", l.kL, l.r2L);
    addLine(lines, n, maxLines, "R: k %s R2 %s", l.kR, l.r2R);
    addLine(lines, n, maxLines, "Used from R2 %s", l.minR2);
    addLine(lines, n, maxLines, "k: counts per C block");
    return n;
}

// Calibration -> Forget (rev 9 FORGET). Cancel is first and
// focused; picking one of the others shows the Status page, where the ~3 s
// of BUSY and the resulting state can be watched.
void menuAction_ForgetCal()  { VolumeControl::instance().forget(DaughterBoardLink::Forget::CAL);  Console::log("Menu", "FORGET CAL requested"); }
void menuAction_ForgetComp() { VolumeControl::instance().forget(DaughterBoardLink::Forget::COMP); Console::log("Menu", "FORGET COMP requested"); }
void menuAction_ForgetAll()  { VolumeControl::instance().forget(DaughterBoardLink::Forget::ALL);  Console::log("Menu", "FORGET ALL requested"); }

const MenuValueOption kForgetOptions[] = {
    { "Cancel",      nullptr,               nullptr },
    { "Calibration", menuAction_ForgetCal,  nullptr }, // volume muted until a new (full) calibration completes
    { "Learning",    menuAction_ForgetComp, nullptr }, // thermal learning log; compensation relearns
    { "Everything",  menuAction_ForgetAll,  nullptr },
};

// --- Digit-by-digit number editors ---------------------------------------------
// One editor field per decimal digit (most significant first): Up/Down change
// the focused digit, Right moves on -- quick even for six-digit values.
void splitDigits(uint32_t v, int16_t* f, uint8_t n) {
    for (int8_t i = static_cast<int8_t>(n) - 1; i >= 0; --i) { f[i] = static_cast<int16_t>(v % 10); v /= 10; }
}
uint32_t joinDigits(const int16_t* f, uint8_t n) {
    uint32_t v = 0;
    for (uint8_t i = 0; i < n; ++i) v = v * 10 + static_cast<uint32_t>(f[i]);
    return v;
}
void limitsDigit(const int16_t*, uint8_t, int16_t* lo, int16_t* hi) { *lo = 0; *hi = 9; }

// --- Calibration -> Model (rev 12) ------------------------------------
// The audio model the steps are solved for: the source's output impedance
// (the splitter -- about 300 ohm for the JFET splitter, 47 for an op-amp one)
// and the load after the attenuator (the amplifier input; 0 = unloaded).
// Each editor opens on the daughter's MODEL value; confirming offers Cancel /
// Apply, since applying a different value makes the daughter CAL=STALE
// (volume muted) until a calibration re-solves -- quick, no full scan.

uint32_t g_editSrc  = 0;
uint32_t g_editLoad = 0;

void describeOhms(char* out, size_t outSize, int32_t ohms, bool zeroIsUnloaded) {
    DaughterBoardLink::instance().requestModel();
    if (ohms < 0)                         snprintf(out, outSize, "--");
    else if (ohms == 0 && zeroIsUnloaded) snprintf(out, outSize, "unloaded");
    else                                  snprintf(out, outSize, "%ld ohm", static_cast<long>(ohms));
}

bool loadSrc(int16_t* v) {
    const DaughterModel& m = DaughterBoardLink::instance().model();
    DaughterBoardLink::instance().requestModel();
    if (!m.valid || m.src < 0 || DaughterBoardLink::instance().status().busy) return false;
    splitDigits(static_cast<uint32_t>(m.src > 9999 ? 9999 : m.src), v, 4);
    return true;
}
bool saveSrc(const int16_t* v) { g_editSrc = joinDigits(v, 4); return true; }
void describeSrc(char* out, size_t n) { describeOhms(out, n, DaughterBoardLink::instance().model().src, false); }

bool loadLoad(int16_t* v) {
    const DaughterModel& m = DaughterBoardLink::instance().model();
    DaughterBoardLink::instance().requestModel();
    if (!m.valid || m.load < 0 || DaughterBoardLink::instance().status().busy) return false;
    splitDigits(static_cast<uint32_t>(m.load > 999999 ? 999999 : m.load), v, 6);
    return true;
}
bool saveLoad(const int16_t* v) {
    g_editLoad = joinDigits(v, 6);
    if (g_editLoad > 0 && g_editLoad < 1000) g_editLoad = 1000; // the daughter takes 0 (unloaded) or 1000 and up
    return true;
}
void describeLoad(char* out, size_t n) { describeOhms(out, n, DaughterBoardLink::instance().model().load, true); }

void menuAction_ApplySrc() {
    if (static_cast<int32_t>(g_editSrc) == DaughterBoardLink::instance().model().src) return; // unchanged
    VolumeControl::instance().setModelSource(g_editSrc);
    Console::log("Menu", "Source impedance -> %lu ohm", static_cast<unsigned long>(g_editSrc));
}
void menuAction_ApplyLoad() {
    if (static_cast<int32_t>(g_editLoad) == DaughterBoardLink::instance().model().load) return;
    VolumeControl::instance().setModelLoad(g_editLoad);
    Console::log("Menu", "Load impedance -> %lu ohm", static_cast<unsigned long>(g_editLoad));
}

const MenuEditor kSrcEditor  = { 4, {1, 1, 1, 1},       '\0', loadSrc,  limitsDigit, saveSrc,  nullptr, describeSrc  };
const MenuEditor kLoadEditor = { 6, {1, 1, 1, 1, 1, 1}, '\0', loadLoad, limitsDigit, saveLoad, nullptr, describeLoad };

const MenuValueOption kApplySrcOptions[]  = { { "Cancel", nullptr, nullptr }, { "Apply", menuAction_ApplySrc,  nullptr } };
const MenuValueOption kApplyLoadOptions[] = { { "Cancel", nullptr, nullptr }, { "Apply", menuAction_ApplyLoad, nullptr } };

// Model -> Overview: in use vs solved, and whether a re-solve is needed.
size_t fillModelInfo(char (*lines)[kInfoLineLen], size_t maxLines) {
    size_t n = 0;
    DaughterBoardLink& link = DaughterBoardLink::instance();
    link.requestModel();
    const DaughterModel&  m = link.model();
    const DaughterStatus& s = link.status();
    if (!link.isStatusFresh()) { addLine(lines, n, maxLines, "No reply from attenuator"); return n; }
    if (!m.valid)              { addLine(lines, n, maxLines, "Waiting for data...");    return n; }
    auto ohms = [](char* b, size_t sz, int32_t v, bool unl) {
        if (v < 0) snprintf(b, sz, "--"); else if (v == 0 && unl) snprintf(b, sz, "unloaded"); else snprintf(b, sz, "%ld", static_cast<long>(v));
    };
    char a[16], b[16];
    ohms(a, sizeof a, m.src, false);  addLine(lines, n, maxLines, "Source %s ohm", a);
    ohms(a, sizeof a, m.load, true);  addLine(lines, n, maxLines, m.load == 0 ? "Load %s" : "Load %s ohm", a);
    ohms(a, sizeof a, m.solvedSrc, false); ohms(b, sizeof b, m.solvedLoad, true);
    addLine(lines, n, maxLines, "Solved: src %s", a);
    addLine(lines, n, maxLines, "  load %s", b);
    // rev 16: the AUTO preference and how deep the stored walk reaches.
    if (strcmp(m.autoPref, "--")) {
        addLine(lines, n, maxLines, "Auto depth %s", m.autoPref);
        if (m.reach > 0) {
            addLine(lines, n, maxLines, "Reach %ldK", static_cast<long>((m.reach + 500) / 1000));
            long prefMax = !strcmp(m.autoPref, "SNAPPY") ? 600000L : !strcmp(m.autoPref, "DEEP") ? 1600000L : 1000000L;
            if (prefMax > m.reach) {
                addLine(lines, n, maxLines, "Deeper: forget the");
                addLine(lines, n, maxLines, "  cal, then calibrate");
            }
        }
    }
    addLine(lines, n, maxLines, "Cal: %s", calStateText(s.cal));
    if (s.cal == DaughterCalState::STALE) addLine(lines, n, maxLines, "Calibrate to re-solve");
    return n;
}

// Calibration -> Auto Depth (rev 16): how AUTO trades depth against
// settling -- deeper Rtotal sits the series cell darker at the quiet steps,
// where it's slowest. Shown from MODEL's AUTO=; choosing one sends SET AUTO
// and shows the Model overview (CAL may go STALE: a calibration re-solves).
void menuSet_AutoSnappy()   { VolumeControl::instance().setAutoPref(DaughterBoardLink::AutoPref::SNAPPY);   Console::log("Menu", "SET AUTO SNAPPY"); }
void menuSet_AutoBalanced() { VolumeControl::instance().setAutoPref(DaughterBoardLink::AutoPref::BALANCED); Console::log("Menu", "SET AUTO BALANCED"); }
void menuSet_AutoDeep()     { VolumeControl::instance().setAutoPref(DaughterBoardLink::AutoPref::DEEP);     Console::log("Menu", "SET AUTO DEEP"); }
bool autoPrefIs(const char* p) { DaughterBoardLink::instance().requestModel(); return !strcmp(DaughterBoardLink::instance().model().autoPref, p); }
bool menuIsActive_AutoSnappy()   { return autoPrefIs("SNAPPY"); }
bool menuIsActive_AutoBalanced() { return autoPrefIs("BALANCED"); }
bool menuIsActive_AutoDeep()     { return autoPrefIs("DEEP"); }
const MenuValueOption kAutoPrefOptions[] = {
    { "Snappy",   menuSet_AutoSnappy,   menuIsActive_AutoSnappy   },
    { "Balanced", menuSet_AutoBalanced, menuIsActive_AutoBalanced },
    { "Deep",     menuSet_AutoDeep,     menuIsActive_AutoDeep     },
};

const MenuItem kModelSubmenuItems[] = {
    { "Source",   MenuItemType::EDITOR, nullptr, kApplySrcOptions,  2, nullptr, 0, fillModelInfo, &kSrcEditor  },
    { "Load",     MenuItemType::EDITOR, nullptr, kApplyLoadOptions, 2, nullptr, 0, fillModelInfo, &kLoadEditor },
    { "Overview", MenuItemType::INFO,   nullptr, nullptr, 0, nullptr, 0, fillModelInfo },
};
constexpr size_t kModelSubmenuItemCount = sizeof(kModelSubmenuItems) / sizeof(kModelSubmenuItems[0]);

// Offered right after the Rtotal selector is confirmed: Cancel (focused)
// or start -- the second label names the Rtotal, then the progress page.
// Calibration. "Rtotal" shows the daughter's current Rtotal;
// entering it opens the selector on that value, confirming offers Cancel /
// Start R=..., and Start shows the progress page.
// Calibration > Last Verify: the newest CAL VERIFY run (rev 20 VERIFY RESULT).
size_t fillVerifyResult(char (*lines)[kInfoLineLen], size_t maxLines) {
    size_t n = 0;
    DaughterBoardLink& link = DaughterBoardLink::instance();
    link.requestVerifyResult();
    const DaughterVerify& v = link.verifyResult();
    if (!link.isStatusFresh()) { addLine(lines, n, maxLines, "No reply from attenuator"); return n; }
    if (link.status().proto < 20) { addLine(lines, n, maxLines, "Needs protocol 20"); return n; }
    if (!v.valid) { addLine(lines, n, maxLines, "Waiting for data"); return n; }
    if (v.seq == 0) {
        addLine(lines, n, maxLines, "No verify on record");
        addLine(lines, n, maxLines, "Calibrate > Verify");
        return n;
    }
    addLine(lines, n, maxLines, "Verdict: %s", v.verdict);
    addLine(lines, n, maxLines, "  %s", !strcmp(v.verdict, "OK")     ? "the table holds"
                                     : !strcmp(v.verdict, "RESYNC") ? "a resync fixes it"
                                     : !strcmp(v.verdict, "CHECK")  ? "look on the console"
                                                                    : "--");
    if (strcmp(v.ageMin, "-")) addLine(lines, n, maxLines, "Run #%lu, %s min ago", static_cast<unsigned long>(v.seq), v.ageMin);
    else                       addLine(lines, n, maxLines, "Run #%lu, %s restarts ago", static_cast<unsigned long>(v.seq), v.restarts);
    addLine(lines, n, maxLines, "Block %s C", v.block);
    addLine(lines, n, maxLines, "Residual L%s R%s", v.resL, v.resR);
    addLine(lines, n, maxLines, "Moved L%s R%s", v.movedL, v.movedR);
    addLine(lines, n, maxLines, "Mid L%s R%s dB", v.midL, v.midR);
    addLine(lines, n, maxLines, "Rms L%s R%s dB", v.rmsL, v.rmsR);
    addLine(lines, n, maxLines, "Worst L-R %s dB", v.lr);
    addLine(lines, n, maxLines, "Walk %s", v.adopted ? "adopted" : "kept");
    return n;
}

// Calibration > (first item): what the calibration is, live -- the item's
// label is the Rtotal ("970K (auto)") or the state, the line under it what
// to know (a resync advised, a run's progress...).
void describeCalibration(char* label, size_t ls, char* hint, size_t hs, uint16_t* color) {
    DaughterBoardLink& link = DaughterBoardLink::instance();
    const DaughterStatus&   s  = link.status();
    const DaughterEnv&      e  = link.env();
    const DaughterProgress& pr = link.progress();
    const DaughterVerify&   v  = link.verifyResult();
    if (!link.isStatusFresh()) {
        snprintf(label, ls, "No link");
        snprintf(hint, hs, "attenuator not answering"); *color = COLOR_RED;
        return;
    }
    if (s.running()) {
        snprintf(label, ls, "%s", s.verifying() ? "Verifying" : "Calibrating");
        if (pr.valid) snprintf(hint, hs, "%s %d%%", phaseLabel(pr.phase), pr.pct < 0 ? 0 : pr.pct);
        else          snprintf(hint, hs, "amp muted meanwhile");
        *color = COLOR_YELLOW;
        return;
    }
    switch (s.cal) {
        case DaughterCalState::NONE:
        case DaughterCalState::CHANGED:
            snprintf(label, ls, "No calibration");
            snprintf(hint, hs, "%s", s.cal == DaughterCalState::CHANGED ? "new cells: calibrate" : "calibrate to play");
            *color = COLOR_RED;
            return;
        case DaughterCalState::ERR:
            snprintf(label, ls, "Calibration error");
            snprintf(hint, hs, "%s", s.hasErr ? s.err : "see Status");
            *color = COLOR_RED;
            return;
        default: break;
    }
    snprintf(label, ls, "%luK (%s)", static_cast<unsigned long>((s.rOhms + 500) / 1000), s.rAuto ? "auto" : "manual");
    if (s.cal == DaughterCalState::STALE) { snprintf(hint, hs, "re-solve needed: Start"); *color = COLOR_YELLOW; return; }
    if (e.valid && e.resync == 1)         { snprintf(hint, hs, "resync advised: Start");  *color = COLOR_YELLOW; return; }
    if (v.valid && v.seq > 0 && !strcmp(v.verdict, "CHECK")) { snprintf(hint, hs, "verify: check console"); *color = COLOR_RED; return; }
    if (e.valid && e.resync == 0 && !strcmp(e.why, "WARMING")) { snprintf(hint, hs, "resync after warm-up"); return; }
    snprintf(hint, hs, "calibrated, last %s", s.lastCal[0] ? s.lastCal : "--");
}

// Calibration > Calibrate > Start / Verify / Full.
void describeCalStart(char*, size_t, char* hint, size_t hs, uint16_t*) {
    snprintf(hint, hs, "choose R, then it runs");
}
void describeCalVerify(char*, size_t, char* hint, size_t hs, uint16_t* color) {
    if (DaughterBoardLink::instance().status().cal != DaughterCalState::OK) { snprintf(hint, hs, "needs a calibration"); *color = COLOR_RED; return; }
    snprintf(hint, hs, "8 min, amp muted, no change");
}
void describeCalFull(char*, size_t, char* hint, size_t hs, uint16_t*) {
    snprintf(hint, hs, "6 min, re-measures cells");
}
void menuAction_CalVerify() {
    const DaughterStatus& s = DaughterBoardLink::instance().status();
    if (s.running() || s.cal != DaughterCalState::OK) return;   // the page says why
    VolumeControl::instance().calibrate(VolumeControl::CalR::VERIFY);
}
void menuAction_CalFull() {
    if (DaughterBoardLink::instance().status().running()) return;
    VolumeControl::instance().calibrate(VolumeControl::CalR::FULL);
}

const MenuItem kCalibrateItems[] = {
    { "Start",  MenuItemType::EDITOR, nullptr,              nullptr, 0, nullptr, 0, fillCalibrationProgress, &kRTotalEditor, nullptr, nullptr, describeCalStart },
    { "Verify", MenuItemType::ACTION, menuAction_CalVerify, nullptr, 0, nullptr, 0, fillCalibrationProgress, nullptr,        nullptr, nullptr, describeCalVerify },
    { "Full",   MenuItemType::ACTION, menuAction_CalFull,   nullptr, 0, nullptr, 0, fillCalibrationProgress, nullptr,        nullptr, nullptr, describeCalFull },
};
constexpr size_t kCalibrateItemCount = sizeof(kCalibrateItems) / sizeof(kCalibrateItems[0]);

const MenuItem kCalibrationSubmenuItems[] = {
    { "Calibrate", MenuItemType::SUBMENU,     nullptr, nullptr, 0, kCalibrateItems, kCalibrateItemCount, nullptr, nullptr, nullptr, nullptr, describeCalibration },
    { "Last Verify", MenuItemType::INFO,      nullptr, nullptr, 0, nullptr, 0, fillVerifyResult },
    { "Auto Depth", MenuItemType::VALUE_CHOICE, nullptr, kAutoPrefOptions, 3, nullptr, 0, fillModelInfo },
    { "Model",    MenuItemType::SUBMENU,      nullptr, nullptr, 0, kModelSubmenuItems, kModelSubmenuItemCount },
    { "Progress", MenuItemType::INFO,         nullptr, nullptr, 0, nullptr, 0, fillCalibrationProgress },
    { "Status",   MenuItemType::INFO,         nullptr, nullptr, 0, nullptr, 0, fillCalibrationStatus },
    { "Temperature", MenuItemType::INFO,      nullptr, nullptr, 0, nullptr, 0, fillTemperature },
    { "Learning", MenuItemType::INFO,         nullptr, nullptr, 0, nullptr, 0, fillLearning },
    { "Forget",   MenuItemType::VALUE_CHOICE, nullptr, kForgetOptions, 4, nullptr, 0, fillCalibrationStatus },
};constexpr size_t kCalibrationSubmenuItemCount = sizeof(kCalibrationSubmenuItems) / sizeof(kCalibrationSubmenuItems[0]);

// --- Diagnostics ---
// The action only raises a request; main.cpp closes the menu and runs the
// test screen (a menu action is a plain function with no access to main's
// screen state).
void menuAction_DisplayTest() { DisplayTestScreen::requestStart(); }

// Amp Mute: the daughter's AMP_MUTE, by hand. The power sequence drives it
// too (muted in standby, released at the end of power-on), so this is for
// checks while the unit is on.
void menuSet_AmpMuteOn()  { VolumeControl::instance().ampMuteOn();  Console::log("Menu", "AMP_MUTE ON"); }
void menuSet_AmpMuteOff() { VolumeControl::instance().ampMuteOff(); Console::log("Menu", "AMP_MUTE OFF"); }
bool menuIsActive_AmpMuteOn()  { return VolumeControl::instance().isAmpMuted(); }
bool menuIsActive_AmpMuteOff() { return !VolumeControl::instance().isAmpMuted(); }
const MenuValueOption kAmpMuteOptions[] = {
    { "On",  menuSet_AmpMuteOn,  menuIsActive_AmpMuteOn  },
    { "Off", menuSet_AmpMuteOff, menuIsActive_AmpMuteOff },
};

// Compensation: the daughter's thermal compensation (COMP ON/OFF), e.g. for
// an A/B listen. The daughter doesn't store it -- it boots ON. Shown from
// ENV's COMP (ON / PART / LEARNING count as on), polled while the menu reads it.
void menuSet_CompOn()  { VolumeControl::instance().setCompensation(true);  Console::log("Menu", "COMP ON"); }
void menuSet_CompOff() { VolumeControl::instance().setCompensation(false); Console::log("Menu", "COMP OFF"); }
bool menuIsActive_CompOn()  { DaughterBoardLink::instance().requestEnv(); return VolumeControl::instance().compensationState() == 1; }
bool menuIsActive_CompOff() { DaughterBoardLink::instance().requestEnv(); return VolumeControl::instance().compensationState() == 0; }
const MenuValueOption kCompOptions[] = {
    { "On",  menuSet_CompOn,  menuIsActive_CompOn  },
    { "Off", menuSet_CompOff, menuIsActive_CompOff },
};

// --- Diagnostics > Versions ------------------------------------------------------
// Lines fit kInfoCols (23) characters: the pages are shown large.

// This board: firmware, build, chip, toolchain.
size_t fillMasterVersion(char (*lines)[kInfoLineLen], size_t maxLines) {
    size_t n = 0;
    addLine(lines, n, maxLines, "FW %s", FW_VERSION);
    addLine(lines, n, maxLines, "Built %s", __DATE__);
    addLine(lines, n, maxLines, "  at %s", __TIME__);
    if (SystemInfo::chipRevision()) {
        addLine(lines, n, maxLines, "%s A%u, %lu MHz", SystemInfo::chipName(), SystemInfo::chipRevision(),
                static_cast<unsigned long>(SystemInfo::cpuMHz()));
    } else {
        addLine(lines, n, maxLines, "%s, %lu MHz", SystemInfo::chipName(), static_cast<unsigned long>(SystemInfo::cpuMHz()));
    }
    addLine(lines, n, maxLines, "Arch %s", SystemInfo::archName());
    if (SystemInfo::flashKB()) addLine(lines, n, maxLines, "Flash %luK", static_cast<unsigned long>(SystemInfo::flashKB()));
    addLine(lines, n, maxLines, "UID %s", SystemInfo::uniqueId());
    if (*SystemInfo::coreVersion()) addLine(lines, n, maxLines, "Core %s", SystemInfo::coreVersion());
    if (*SystemInfo::sdkVersion())  addLine(lines, n, maxLines, "SDK %s", SystemInfo::sdkVersion());
    addLine(lines, n, maxLines, "Expects protocol %u", DAUGHTER_PROTO_EXPECTED);
    unsigned long up = millis() / 1000;
    addLine(lines, n, maxLines, "Up %lud %02lu:%02lu:%02lu", up / 86400, (up / 3600) % 24, (up / 60) % 60, up % 60);
    return n;
}

// The volume (daughter) board, from its VER reply (rev 15).
size_t fillDaughterVersion(char (*lines)[kInfoLineLen], size_t maxLines) {
    size_t n = 0;
    DaughterBoardLink& link = DaughterBoardLink::instance();
    link.requestVer();
    const DaughterVersion& v = link.version();
    if (!link.isStatusFresh() && !v.valid) { addLine(lines, n, maxLines, "No reply from attenuator"); return n; }
    if (!v.valid) {
        if (link.status().valid && link.status().proto < 15) addLine(lines, n, maxLines, "Needs protocol 15+");
        else                                                 addLine(lines, n, maxLines, "Waiting for data...");
        if (link.status().valid) addLine(lines, n, maxLines, "Protocol %u", link.status().proto);
        return n;
    }
    addLine(lines, n, maxLines, "FW %s", v.fw);
    // BUILD is ISO 8601: date and time on two lines.
    char date[16] = "--", time[16] = "";
    const char* t = strchr(v.build, 'T');
    if (t) {
        size_t dl = static_cast<size_t>(t - v.build);
        if (dl >= sizeof(date)) dl = sizeof(date) - 1;
        memcpy(date, v.build, dl);
        date[dl] = '\0';
        snprintf(time, sizeof(time), "%s", t + 1);
    } else {
        snprintf(date, sizeof(date), "%s", v.build);
    }
    addLine(lines, n, maxLines, "Built %s", date);
    if (time[0]) addLine(lines, n, maxLines, "  at %s", time);
    addLine(lines, n, maxLines, "Protocol %u", v.proto);
    addLine(lines, n, maxLines, "HW %s rev %s", v.hw, v.hwRev);
    addLine(lines, n, maxLines, "Cal algorithm rev %s", v.calRev);
    addLine(lines, n, maxLines, "%s rev %s, %s MHz", v.chip, v.chipRev, v.mhz);
    addLine(lines, n, maxLines, "Flash %s", v.flash);
    addLine(lines, n, maxLines, "UID %s", v.uid);
    addLine(lines, n, maxLines, "Board %s", v.board);
    addLine(lines, n, maxLines, "Core %s", v.core);
    addLine(lines, n, maxLines, "SDK %s", v.sdk);
    if (v.note[0]) addWrapped(lines, n, maxLines, "Note: ", v.note);
    return n;
}

const MenuItem kVersionsSubmenuItems[] = {
    { "Master",       MenuItemType::INFO, nullptr, nullptr, 0, nullptr, 0, fillMasterVersion },
    { "Attenuator", MenuItemType::INFO, nullptr, nullptr, 0, nullptr, 0, fillDaughterVersion },
};

// Diagnostics > Debug Output: the serial debug lines, at run time (not saved).
void menuSet_DbgAudioOn()  { DebugFlags::audio = true;  Console::log("Menu", "Audio debug output on"); }
void menuSet_DbgAudioOff() { DebugFlags::audio = false; Console::log("Menu", "Audio debug output off"); }
void menuSet_DbgPerfOn()   { DebugFlags::perf  = true;  Console::log("Menu", "Perf debug output on"); }
void menuSet_DbgPerfOff()  { DebugFlags::perf  = false; Console::log("Menu", "Perf debug output off"); }
bool menuIsActive_DbgAudioOn()  { return  DebugFlags::audio; }
bool menuIsActive_DbgAudioOff() { return !DebugFlags::audio; }
bool menuIsActive_DbgPerfOn()   { return  DebugFlags::perf; }
bool menuIsActive_DbgPerfOff()  { return !DebugFlags::perf; }
const MenuValueOption kDbgAudioOptions[] = {
    { "On",  menuSet_DbgAudioOn,  menuIsActive_DbgAudioOn  },
    { "Off", menuSet_DbgAudioOff, menuIsActive_DbgAudioOff },
};
const MenuValueOption kDbgPerfOptions[] = {
    { "On",  menuSet_DbgPerfOn,  menuIsActive_DbgPerfOn  },
    { "Off", menuSet_DbgPerfOff, menuIsActive_DbgPerfOff },
};
const MenuItem kDebugOutputItems[] = {
    { "Audio", MenuItemType::VALUE_CHOICE, nullptr, kDbgAudioOptions, 2 }, // [Audio] / [Spectrum] timing lines
    { "Perf",  MenuItemType::VALUE_CHOICE, nullptr, kDbgPerfOptions,  2 }, // [Perf] frame-rate line
};

// Diagnostics > Compensation: the switch (On/Off) inside; under the name, what
// the attenuator's compensation is actually doing (its ENV COMP).
void describeCompensation(char*, size_t, char* hint, size_t hs, uint16_t* color) {
    VolumeControl& vc = VolumeControl::instance();
    const char* c = vc.compensationText();
    int8_t level  = vc.compensationLevel();
    const char* text = !strcmp(c, "ON")       ? "on"
                     : !strcmp(c, "PART")     ? "on, one side only"
                     : !strcmp(c, "LEARNING") ? "learning"
                     : !strcmp(c, "OFF")      ? "off"
                     : !strcmp(c, "NONTC")    ? "off: block sensor fault"
                                              : "--";
    snprintf(hint, hs, "%s", text);
    *color = level == 2 ? COLOR_GREEN : level == 1 ? COLOR_YELLOW : level == 0 ? COLOR_RED : 0x8410;
}

const MenuItem kDiagnosticsSubmenuItems[] = {
    { "Versions",     MenuItemType::SUBMENU,      nullptr, nullptr, 0, kVersionsSubmenuItems, 2 },
    { "Amp Mute",     MenuItemType::VALUE_CHOICE, nullptr, kAmpMuteOptions, 2 },
    { "Compensation", MenuItemType::VALUE_CHOICE, nullptr, kCompOptions,    2, nullptr, 0, nullptr, nullptr, nullptr, nullptr, describeCompensation },
    { "Display Test", MenuItemType::ACTION,       menuAction_DisplayTest, nullptr, 0 },
    { "Debug Output", MenuItemType::SUBMENU,      nullptr, nullptr, 0, kDebugOutputItems, 2 },
};
constexpr size_t kDiagnosticsSubmenuItemCount = sizeof(kDiagnosticsSubmenuItems) / sizeof(kDiagnosticsSubmenuItems[0]);

// --- Settings -> Power ---
void menuSet_StandbyClock() { DisplaySettings::instance().setStandbyDisplay(StandbyDisplay::CLOCK);     PersistentSettings::instance().markDirty(); }
void menuSet_StandbyDim()   { DisplaySettings::instance().setStandbyDisplay(StandbyDisplay::DIM_CLOCK); PersistentSettings::instance().markDirty(); }
void menuSet_StandbyOff()   { DisplaySettings::instance().setStandbyDisplay(StandbyDisplay::OFF);       PersistentSettings::instance().markDirty(); }
bool menuIsActive_StandbyClock() { return DisplaySettings::instance().getStandbyDisplay() == StandbyDisplay::CLOCK; }
bool menuIsActive_StandbyDim()   { return DisplaySettings::instance().getStandbyDisplay() == StandbyDisplay::DIM_CLOCK; }
bool menuIsActive_StandbyOff()   { return DisplaySettings::instance().getStandbyDisplay() == StandbyDisplay::OFF; }

const MenuValueOption kStandbyDisplayOptions[] = {
    { "Clock",     menuSet_StandbyClock, menuIsActive_StandbyClock },
    { "Dim Clock", menuSet_StandbyDim,   menuIsActive_StandbyDim   },
    { "Off",       menuSet_StandbyOff,   menuIsActive_StandbyOff   },
};
// Auto standby: minutes of silence (nothing on the audio inputs) before the
// unit switches to standby by itself; 0 = off. One editor field, OFF then
// 1..60 minutes.
bool loadAutoStandby(int16_t* v) { v[0] = DisplaySettings::instance().getAutoStandbyMinutes(); return true; }
void limitsAutoStandby(const int16_t*, uint8_t, int16_t* lo, int16_t* hi) { *lo = 0; *hi = 60; }
void formatAutoStandby(const int16_t* v, uint8_t, char* out, size_t outSize) {
    if (v[0] == 0) snprintf(out, outSize, "   OFF");
    else           snprintf(out, outSize, "%2d min", static_cast<int>(v[0]));
}
bool saveAutoStandby(const int16_t* v) {
    DisplaySettings::instance().setAutoStandbyMinutes(static_cast<uint8_t>(v[0]));
    PersistentSettings::instance().markDirty();
    if (v[0]) Console::log("Menu", "Auto standby after %d min of silence", static_cast<int>(v[0]));
    else      Console::log("Menu", "Auto standby off");
    return true;
}
const MenuEditor kAutoStandbyEditor = { 1, {6}, ' ', loadAutoStandby, limitsAutoStandby, saveAutoStandby, formatAutoStandby };

// Silence Level: the input peak (ADC counts, 12-bit, DC removed) above which
// the inputs count as playing. Three digits; 1 count is about 0.8 mV at the ADC.
bool loadSilence(int16_t* v) { splitDigits(DisplaySettings::instance().getAutoStandbyThreshold(), v, 3); return true; }
bool saveSilence(const int16_t* v) {
    uint32_t t = joinDigits(v, 3);
    if (t < 1) t = 1;
    DisplaySettings::instance().setAutoStandbyThreshold(static_cast<uint16_t>(t));
    PersistentSettings::instance().markDirty();
    Console::log("Menu", "Silence level %lu counts", static_cast<unsigned long>(t));
    return true;
}
void describeSilence(char* out, size_t n) {
    uint16_t t = DisplaySettings::instance().getAutoStandbyThreshold();
    snprintf(out, n, "%u (%u mV)", t, static_cast<unsigned>((t * 3300u + 2048u) / 4096u));
}
const MenuEditor kSilenceEditor = { 3, {1, 1, 1}, '\0', loadSilence, limitsDigit, saveSilence, nullptr, describeSilence };

// Input Level: the live input peak against the threshold, and the timer --
// for setting the Silence Level by watching it.
size_t fillInputLevel(char (*lines)[kInfoLineLen], size_t maxLines) {
    size_t n = 0;
    const AutoStandby& as = AutoStandby::instance();
    uint16_t t = DisplaySettings::instance().getAutoStandbyThreshold();
    addLine(lines, n, maxLines, "Peak now  %d", as.peak());
    addLine(lines, n, maxLines, "Threshold %u", t);
    addLine(lines, n, maxLines, "Input: %s", as.playing() ? "PLAYING" : "silent");
    uint8_t minutes = DisplaySettings::instance().getAutoStandbyMinutes();
    if (minutes == 0) {
        addLine(lines, n, maxLines, "Auto standby off");
    } else if (as.counting()) {
        uint32_t left = static_cast<uint32_t>(minutes) * 60u - as.idleMs() / 1000u;
        addLine(lines, n, maxLines, "Standby in %lu:%02lu", static_cast<unsigned long>(left / 60), static_cast<unsigned long>(left % 60));
    } else {
        addLine(lines, n, maxLines, "Timer reset (active)");
    }
    return n;
}

const MenuItem kPowerSubmenuItems[] = {
    { "Standby Display", MenuItemType::VALUE_CHOICE, nullptr, kStandbyDisplayOptions, 3 },
    { "Auto Standby",    MenuItemType::EDITOR,       nullptr, nullptr, 0, nullptr, 0, nullptr, &kAutoStandbyEditor },
    { "Silence Level",   MenuItemType::EDITOR,       nullptr, nullptr, 0, nullptr, 0, nullptr, &kSilenceEditor },
    { "Input Level",     MenuItemType::INFO,         nullptr, nullptr, 0, nullptr, 0, fillInputLevel },
};
constexpr size_t kPowerSubmenuItemCount = sizeof(kPowerSubmenuItems) / sizeof(kPowerSubmenuItems[0]);

// --- Settings ---
// Settings -> Volume Display: what the Info screen's big value shows.
void menuSet_VolDb()    { DisplaySettings::instance().setVolumeDisplay(VolumeDisplay::DB);    PersistentSettings::instance().markDirty(); }
void menuSet_VolSteps() { DisplaySettings::instance().setVolumeDisplay(VolumeDisplay::STEPS); PersistentSettings::instance().markDirty(); }
bool menuIsActive_VolDb()    { return DisplaySettings::instance().getVolumeDisplay() == VolumeDisplay::DB; }
bool menuIsActive_VolSteps() { return DisplaySettings::instance().getVolumeDisplay() == VolumeDisplay::STEPS; }
const MenuValueOption kVolumeDisplayOptions[] = {
    { "dB",    menuSet_VolDb,    menuIsActive_VolDb    },
    { "Steps", menuSet_VolSteps, menuIsActive_VolSteps },
};

// Settings -> Balance (rev 18 BAL): -16..+16 in 0.25 dB (+-4 dB). The slider
// shows where the sound sits: right of centre lowers the LEFT channel
// (positive BAL), left of centre the right one. Applied live; saved with the
// settings (written when the menu closes) and sent at the daughter's power-up.
int16_t balanceGet()          { return VolumeControl::instance().balance(); }
void    balanceSet(int16_t v) {
    VolumeControl::instance().setBalance(static_cast<int8_t>(v));
    PersistentSettings::instance().markDirty();
}
void    balanceFormat(int16_t v, char* out, size_t n) {
    if (v == 0) { snprintf(out, n, "Center"); return; }
    int a = (v < 0 ? -v : v) * 25; // hundredths of a dB
    snprintf(out, n, "%s -%d.%02d dB", v > 0 ? "Left" : "Right", a / 100, a % 100);
}
const MenuSlider kBalanceSlider = { -16, 16, balanceGet, balanceSet, balanceFormat, "L", "R" };

const MenuItem kSettingsSubmenuItems[] = {
    { "Brightness",      MenuItemType::VALUE_CHOICE, nullptr, kBrightnessOptions,  4 },
    { "Balance",         MenuItemType::SLIDER,       nullptr, nullptr, 0, nullptr, 0, nullptr, nullptr, nullptr, &kBalanceSlider },
    { "Volume Display",  MenuItemType::VALUE_CHOICE, nullptr, kVolumeDisplayOptions, 2 },
    { "Clock",           MenuItemType::SUBMENU, nullptr, nullptr, 0, kClockSubmenuItems,       kClockSubmenuItemCount },
    { "Power",           MenuItemType::SUBMENU, nullptr, nullptr, 0, kPowerSubmenuItems,       kPowerSubmenuItemCount },
};
constexpr size_t kSettingsSubmenuItemCount = sizeof(kSettingsSubmenuItems) / sizeof(kSettingsSubmenuItems[0]);

const MenuItem kRootMenuItems[] = {
    { "Theme",           MenuItemType::SUBMENU,      nullptr, nullptr, 0, kThemeSubmenuItems,       kThemeSubmenuItemCount },
    { "Calibration",     MenuItemType::SUBMENU,      nullptr, nullptr, 0, kCalibrationSubmenuItems, kCalibrationSubmenuItemCount },
    { "Settings",        MenuItemType::SUBMENU,      nullptr, nullptr, 0, kSettingsSubmenuItems,    kSettingsSubmenuItemCount },
    { "Diagnostics",     MenuItemType::SUBMENU,      nullptr, nullptr, 0, kDiagnosticsSubmenuItems, kDiagnosticsSubmenuItemCount },
};
constexpr size_t kRootMenuItemCount = sizeof(kRootMenuItems) / sizeof(kRootMenuItems[0]);

} // namespace

const MenuItem* getRootMenuItems() { return kRootMenuItems; }
size_t getRootMenuItemCount() { return kRootMenuItemCount; }

namespace {
// Position of the item whose submenu is `target` in a list (or -1).
int indexOfSubmenu(const MenuItem* items, size_t count, const MenuItem* target) {
    for (size_t i = 0; i < count; ++i) if (items[i].submenuItems == target) return static_cast<int>(i);
    return -1;
}
int indexOfInfo(const MenuItem* items, size_t count, MenuInfoFill fill, MenuItemType type) {
    for (size_t i = 0; i < count; ++i) if (items[i].type == type && items[i].fillInfo == fill) return static_cast<int>(i);
    return -1;
}
} // namespace

void menuOpenResyncStart(MenuScreen& menu) {
    int calibration = indexOfSubmenu(kRootMenuItems, sizeof(kRootMenuItems) / sizeof(kRootMenuItems[0]), kCalibrationSubmenuItems);
    if (calibration < 0) return;
    menu.focus(static_cast<size_t>(calibration));
    menu.enter();
    int calibrate = indexOfSubmenu(kCalibrationSubmenuItems, kCalibrationSubmenuItemCount, kCalibrateItems);
    if (calibrate < 0) return;
    menu.focus(static_cast<size_t>(calibrate));
    menu.enter();      // into Calibrate: "Start" is first -- Right, then Right on the R, runs it
}
