#include "MenuContent.hpp"
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
#include <Arduino.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

void menuAction_Calibrate() {
    VolumeControl::instance().calibrate();
    Serial.println("[Menu] Calibration requested");
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

// Settings -> Calibration -> Status: everything the daughter reports in its
// STATUS line, live (the menu re-fills this twice a second while it's open).
size_t fillCalibrationStatus(char (*lines)[kInfoLineLen], size_t maxLines) {
    size_t n = 0;
    const DaughterBoardLink& link = DaughterBoardLink::instance();
    const DaughterStatus&    s    = link.status();

    if (link.isPassthrough()) {
        addLine(lines, n, maxLines, "Console pass-through active");
        return n;
    }
    if (!link.isStatusFresh()) {
        addLine(lines, n, maxLines, "Link:    no reply from daughter");
        if (!s.valid) return n;
        addLine(lines, n, maxLines, "(last known values below)");
    } else if (s.proto != DAUGHTER_PROTO_EXPECTED) {
        addLine(lines, n, maxLines, "Link:    PROTO %u (expected %u)", s.proto, DAUGHTER_PROTO_EXPECTED);
    } else {
        addLine(lines, n, maxLines, "Link:    OK, protocol %u", s.proto);
    }

    if (s.bootHold) {
        addLine(lines, n, maxLines, "State:   boot hold, %lums left", static_cast<unsigned long>(s.bootHoldRemainingMs));
    } else if (s.calibrating()) {
        addLine(lines, n, maxLines, "State:   CALIBRATING...");
    } else {
        addLine(lines, n, maxLines, "State:   %s", s.busy ? "busy" : "idle");
    }

    const char* cal = calStateText(s.cal);
    if (s.lastCal[0] && strcmp(s.lastCal, "NONE") != 0) {
        addLine(lines, n, maxLines, "Cal:     %s (last: %s)", cal, s.lastCal);
    } else {
        addLine(lines, n, maxLines, "Cal:     %s", cal);
    }

    char a[12], b[12];
    if (s.calibrating()) {
        // Per the protocol, every other field is transient during a run.
        addLine(lines, n, maxLines, "Volume:  locked until it finishes");
    } else {
        if (s.vol < 0) {
            addLine(lines, n, maxLines, "Volume:  not calibrated");
        } else {
            formatTenths(a, sizeof(a), s.db);
            addLine(lines, n, maxLines, "Volume:  %d (%d-%d), %s dB", s.vol, s.volMin, s.volMax, a);
        }
        addLine(lines, n, maxLines, "Mute:    %s, amp %s", s.mute ? "ON" : "off", s.ampMute ? "ON" : "off");
        formatTenths(a, sizeof(a), s.rangeDb);
        addLine(lines, n, maxLines, "Range:   %s dB", a);
        addLine(lines, n, maxLines, "R total: %lu ohm (%s)", static_cast<unsigned long>(s.rOhms), s.rAuto ? "AUTO" : "manual");
        addLine(lines, n, maxLines, "R range: %lu - %lu ohm",
                static_cast<unsigned long>(s.rMinOhms), static_cast<unsigned long>(s.rMaxOhms));
    }

    formatTenths(b, sizeof(b), s.tempC);
    addLine(lines, n, maxLines, "Temp:    %s C (MCU)", b);

    // Revision 8 ENV: board temperatures, rail, thermal compensation and the
    // last calibration's stereo match -- polled while this page is open.
    DaughterBoardLink::instance().requestEnv();
    const DaughterEnv& e = link.env();
    if (e.valid) {
        addLine(lines, n, maxLines, "Block:   %s C, cells L %s R %s", e.block, e.cellL, e.cellR);
        addLine(lines, n, maxLines, "Rail:    %s V, comp %s", e.rail, e.comp);
        addLine(lines, n, maxLines, "Comp:    off L %s R %s", e.offL, e.offR);
        addLine(lines, n, maxLines, "Match:   %s dB, block %s C", e.match, e.dBlock);
    }
    unsigned long up = s.upSeconds;
    addLine(lines, n, maxLines, "Uptime:  %lud %02lu:%02lu:%02lu", up / 86400, (up / 3600) % 24, (up / 60) % 60, up % 60);

    if (s.hasErr) {
        // Free text from the daughter, wrapped across as many lines as needed.
        const char* p = s.err;
        const char* prefix = "Error:   ";
        while (*p && n < maxLines) {
            char chunk[kInfoLineLen];
            size_t room = kInfoLineLen - 1 - strlen(prefix);
            size_t len  = strlen(p) < room ? strlen(p) : room;
            memcpy(chunk, p, len);
            chunk[len] = '\0';
            addLine(lines, n, maxLines, "%s%s", prefix, chunk);
            p += len;
            prefix = "         ";
        }
    }
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
    Serial.printf("[Clock] %s %s\n", ok ? "Time set:" : "Setting the time failed:", buf);
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
    Serial.printf("[Clock] %s %s\n", ok ? "Date set:" : "Setting the date failed:", buf);
    return ok;
}

const MenuEditor kTimeEditor = { 3, {2, 2, 2}, ':', loadTime, limitsTime, saveTime };
const MenuEditor kDateEditor = { 3, {4, 2, 2}, '-', loadDate, limitsDate, saveDate };

// Clock screen options -- applied when the menu closes (the active screen is
// re-entered, which re-lays the clock out).
void menuSet_ClockDateOn()   { DisplaySettings::instance().setClockShowDate(true);     PersistentSettings::instance().markDirty(); }
void menuSet_ClockDateOff()  { DisplaySettings::instance().setClockShowDate(false);    PersistentSettings::instance().markDirty(); }
bool menuIsActive_ClockDateOn()  { return DisplaySettings::instance().getClockShowDate(); }
bool menuIsActive_ClockDateOff() { return !DisplaySettings::instance().getClockShowDate(); }
void menuSet_ClockSecOn()    { DisplaySettings::instance().setClockShowSeconds(true);  PersistentSettings::instance().markDirty(); }
void menuSet_ClockSecOff()   { DisplaySettings::instance().setClockShowSeconds(false); PersistentSettings::instance().markDirty(); }
bool menuIsActive_ClockSecOn()   { return DisplaySettings::instance().getClockShowSeconds(); }
bool menuIsActive_ClockSecOff()  { return !DisplaySettings::instance().getClockShowSeconds(); }
void menuSet_Clock24h()      { DisplaySettings::instance().setClock24h(true);          PersistentSettings::instance().markDirty(); }
void menuSet_Clock12h()      { DisplaySettings::instance().setClock24h(false);         PersistentSettings::instance().markDirty(); }
bool menuIsActive_Clock24h()     { return DisplaySettings::instance().getClock24h(); }
bool menuIsActive_Clock12h()     { return !DisplaySettings::instance().getClock24h(); }

void menuSet_Stereo() { DisplaySettings::instance().setChannelMode(ChannelMode::STEREO); PersistentSettings::instance().markDirty(); }
void menuSet_Mono()   { DisplaySettings::instance().setChannelMode(ChannelMode::MONO);   PersistentSettings::instance().markDirty(); }
bool menuIsActive_Stereo() { return DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO; }
bool menuIsActive_Mono()   { return DisplaySettings::instance().getChannelMode() == ChannelMode::MONO; }

void menuSet_ColorClassic()  { ColorTheme::instance().setPreset(ColorPreset::CLASSIC);  PersistentSettings::instance().markDirty(); }
void menuSet_ColorOcean()    { ColorTheme::instance().setPreset(ColorPreset::OCEAN);    PersistentSettings::instance().markDirty(); }
void menuSet_ColorColorful() { ColorTheme::instance().setPreset(ColorPreset::COLORFUL); PersistentSettings::instance().markDirty(); }
void menuSet_ColorNastyFFT() { ColorTheme::instance().setPreset(ColorPreset::NASTYFFT); PersistentSettings::instance().markDirty(); }
bool menuIsActive_ColorClassic()  { return ColorTheme::instance().getPreset() == ColorPreset::CLASSIC; }
bool menuIsActive_ColorOcean()    { return ColorTheme::instance().getPreset() == ColorPreset::OCEAN; }
bool menuIsActive_ColorColorful() { return ColorTheme::instance().getPreset() == ColorPreset::COLORFUL; }
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

// Peaks Enabled: whether SpectrumScreen/DigitalVuMeterScreen draw their
// peak-hold indicators at all -- purely a core0/display-side setting (see
// DisplaySettings::setPeaksEnabled()'s comment for why no special
// clear/reset handling is needed here beyond the menu's existing onEnter()
// behavior on close).
void menuSet_PeaksOn()  { DisplaySettings::instance().setPeaksEnabled(true);  PersistentSettings::instance().markDirty(); }
void menuSet_PeaksOff() { DisplaySettings::instance().setPeaksEnabled(false); PersistentSettings::instance().markDirty(); }
bool menuIsActive_PeaksOn()  { return DisplaySettings::instance().getPeaksEnabled(); }
bool menuIsActive_PeaksOff() { return !DisplaySettings::instance().getPeaksEnabled(); }

// Peak Decay Speed: a multiplier on the peak-hold drop rate (Slow/Normal/
// Fast), not independently-tuned values -- see BarPhysics::
// recomputePeakDrop()'s comment. Same core0-only reasoning as above, no
// cross-core handling needed. Off is a genuinely different mode, not just
// the slowest multiplier -- see PeakDecaySpeed's own comment in
// BarPhysics.hpp: once set, the peak never falls on its own at all.
void menuSet_PeakDecaySlow()   { BarPhysics::instance().setPeakDecaySpeed(PeakDecaySpeed::SLOW);   PersistentSettings::instance().markDirty(); }
void menuSet_PeakDecayNormal() { BarPhysics::instance().setPeakDecaySpeed(PeakDecaySpeed::NORMAL); PersistentSettings::instance().markDirty(); }
void menuSet_PeakDecayFast()   { BarPhysics::instance().setPeakDecaySpeed(PeakDecaySpeed::FAST);   PersistentSettings::instance().markDirty(); }
void menuSet_PeakDecayOff()    { BarPhysics::instance().setPeakDecaySpeed(PeakDecaySpeed::OFF);    PersistentSettings::instance().markDirty(); }
bool menuIsActive_PeakDecaySlow()   { return BarPhysics::instance().getPeakDecaySpeed() == PeakDecaySpeed::SLOW; }
bool menuIsActive_PeakDecayNormal() { return BarPhysics::instance().getPeakDecaySpeed() == PeakDecaySpeed::NORMAL; }
bool menuIsActive_PeakDecayFast()   { return BarPhysics::instance().getPeakDecaySpeed() == PeakDecaySpeed::FAST; }
bool menuIsActive_PeakDecayOff()    { return BarPhysics::instance().getPeakDecaySpeed() == PeakDecaySpeed::OFF; }

const MenuValueOption kColorOptions[] = {
    { "Classic",  menuSet_ColorClassic,  menuIsActive_ColorClassic  },
    { "Ocean",    menuSet_ColorOcean,    menuIsActive_ColorOcean    },
    { "Colorful", menuSet_ColorColorful, menuIsActive_ColorColorful },
    { "NastyFFT", menuSet_ColorNastyFFT, menuIsActive_ColorNastyFFT },
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
};

// "Wave" submenu, nested inside "Theme" -- currently just the one
// setting (Window Mode), with room to grow the same way "FFT" did.
const MenuItem kWaveSubmenuItems[] = {
    { "Window Mode", MenuItemType::VALUE_CHOICE, nullptr, kWaveWindowModeOptions, 2 },
};
constexpr size_t kWaveSubmenuItemCount = sizeof(kWaveSubmenuItems) / sizeof(kWaveSubmenuItems[0]);

const MenuValueOption kPeaksEnabledOptions[] = {
    { "On",  menuSet_PeaksOn,  menuIsActive_PeaksOn  },
    { "Off", menuSet_PeaksOff, menuIsActive_PeaksOff },
};

const MenuValueOption kPeakDecaySpeedOptions[] = {
    { "Off",    menuSet_PeakDecayOff,    menuIsActive_PeakDecayOff    },
    { "Slow",   menuSet_PeakDecaySlow,   menuIsActive_PeakDecaySlow   },
    { "Normal", menuSet_PeakDecayNormal, menuIsActive_PeakDecayNormal },
    { "Fast",   menuSet_PeakDecayFast,   menuIsActive_PeakDecayFast   },
};

// "Peaks" submenu, nested inside "Theme" -- peak-hold indicator settings
// for both SpectrumScreen and DigitalVuMeterScreen, same role as "FFT"/
// "Wave" play for their own screens.
const MenuItem kPeaksSubmenuItems[] = {
    { "Enabled",     MenuItemType::VALUE_CHOICE, nullptr, kPeaksEnabledOptions,    2 },
    { "Decay Speed", MenuItemType::VALUE_CHOICE, nullptr, kPeakDecaySpeedOptions,  4 },
};
constexpr size_t kPeaksSubmenuItemCount = sizeof(kPeaksSubmenuItems) / sizeof(kPeaksSubmenuItems[0]);

// "Theme" submenu, nested at root -- display/appearance settings grouped
// together, with "FFT", "Wave", and "Peaks" themselves one further level in.
const MenuItem kThemeSubmenuItems[] = {
    { "Stereo / Mono", MenuItemType::VALUE_CHOICE, nullptr, kChannelModeOptions, 2 },
    { "Colors",        MenuItemType::VALUE_CHOICE, nullptr, kColorOptions,       4 },
    { "FFT",           MenuItemType::SUBMENU,      nullptr, nullptr, 0, kFftSubmenuItems,   kFftSubmenuItemCount },
    { "Wave",          MenuItemType::SUBMENU,      nullptr, nullptr, 0, kWaveSubmenuItems,  kWaveSubmenuItemCount },
    { "Peaks",         MenuItemType::SUBMENU,      nullptr, nullptr, 0, kPeaksSubmenuItems, kPeaksSubmenuItemCount },
};
constexpr size_t kThemeSubmenuItemCount = sizeof(kThemeSubmenuItems) / sizeof(kThemeSubmenuItems[0]);

const MenuValueOption kBrightnessOptions[] = {
    { "25%",  menuSet_Brightness<25>,  menuIsActive_Brightness<25>  },
    { "50%",  menuSet_Brightness<50>,  menuIsActive_Brightness<50>  },
    { "75%",  menuSet_Brightness<75>,  menuIsActive_Brightness<75>  },
    { "100%", menuSet_Brightness<100>, menuIsActive_Brightness<100> },
};

// --- Settings -> Clock ---
const MenuValueOption kClockDateOptions[] = {
    { "On",  menuSet_ClockDateOn,  menuIsActive_ClockDateOn  },
    { "Off", menuSet_ClockDateOff, menuIsActive_ClockDateOff },
};
const MenuValueOption kClockSecondsOptions[] = {
    { "On",  menuSet_ClockSecOn,  menuIsActive_ClockSecOn  },
    { "Off", menuSet_ClockSecOff, menuIsActive_ClockSecOff },
};
const MenuValueOption kClockFormatOptions[] = {
    { "24 hour", menuSet_Clock24h, menuIsActive_Clock24h },
    { "12 hour", menuSet_Clock12h, menuIsActive_Clock12h },
};
const MenuItem kClockSubmenuItems[] = {
    { "Set Time",     MenuItemType::EDITOR,       nullptr, nullptr, 0, nullptr, 0, nullptr, &kTimeEditor },
    { "Set Date",     MenuItemType::EDITOR,       nullptr, nullptr, 0, nullptr, 0, nullptr, &kDateEditor },
    { "Show Date",    MenuItemType::VALUE_CHOICE, nullptr, kClockDateOptions,    2 },
    { "Show Seconds", MenuItemType::VALUE_CHOICE, nullptr, kClockSecondsOptions, 2 },
    { "Format",       MenuItemType::VALUE_CHOICE, nullptr, kClockFormatOptions,  2 },
};
constexpr size_t kClockSubmenuItemCount = sizeof(kClockSubmenuItems) / sizeof(kClockSubmenuItems[0]);

// --- Settings -> Calibration ---
//
// Rtotal: the Rs+Rsh the daughter's volume steps are solved for. The choice
// made here is only a request for the NEXT calibration ("Start"): it starts
// out as whatever the daughter currently uses (RAUTO / R from STATUS), and
// Start sends plain "CAL" when the choice equals that (the daughter then
// decides how much work it needs), or "CAL R=AUTO" / "CAL R=<ohms>" when it
// was changed (the daughter re-solves from its stored measurements).

enum class RChoice : uint8_t { UNSET, AUTO, MANUAL };
RChoice  g_rChoice     = RChoice::UNSET;
uint32_t g_rManualOhms = 0;

// The Start item's label names what it will do: "Start R=AUTO" or
// "Start R=650K" (MenuScreen reads the label each time it draws).
char g_startLabel[24] = "Start";

void updateStartLabel() {
    if (g_rChoice == RChoice::AUTO) {
        snprintf(g_startLabel, sizeof(g_startLabel), "Start R=AUTO");
    } else if (g_rChoice == RChoice::MANUAL) {
        snprintf(g_startLabel, sizeof(g_startLabel), "Start R=%luK", static_cast<unsigned long>((g_rManualOhms + 500) / 1000));
    } else {
        snprintf(g_startLabel, sizeof(g_startLabel), "Start");
    }
}

// Lazily adopts the daughter's current setting the first time it's needed.
void syncRChoice() {
    if (g_rChoice != RChoice::UNSET) return;
    const DaughterStatus& s = DaughterBoardLink::instance().status();
    if (!s.valid || s.busy) return;
    g_rChoice     = s.rAuto ? RChoice::AUTO : RChoice::MANUAL;
    g_rManualOhms = s.rOhms;
    updateStartLabel();
}

// "Rtotal" selector: ONE field covering both modes -- index 0 is AUTO,
// then RMIN..RMAX (the daughter's measured range) in kRStepOhms steps,
// rounded inward. Up/Down step through them (wrapping); Right saves.
constexpr uint32_t kRStepOhms = 5000;
int16_t g_rLoadedIndex = -1; // what the selector opened on (unchanged = keep the exact value)

uint32_t rFirstOhms() {
    uint32_t rmin = DaughterBoardLink::instance().status().rMinOhms;
    return ((rmin + kRStepOhms - 1) / kRStepOhms) * kRStepOhms;
}
int16_t rStepCount() {
    uint32_t first = rFirstOhms(), last = (DaughterBoardLink::instance().status().rMaxOhms / kRStepOhms) * kRStepOhms;
    return (last < first) ? 0 : static_cast<int16_t>((last - first) / kRStepOhms + 1);
}
uint32_t rIndexOhms(int16_t i) { return rFirstOhms() + static_cast<uint32_t>(i - 1) * kRStepOhms; }

bool loadRTotal(int16_t* v) {
    const DaughterStatus& s = DaughterBoardLink::instance().status();
    if (!s.valid || s.busy || s.rMinOhms == 0 || s.rMaxOhms == 0 || rStepCount() == 0) return false;
    syncRChoice();
    if (g_rChoice != RChoice::MANUAL) {
        v[0] = 0;
    } else {
        int32_t i = 1 + (static_cast<int32_t>(g_rManualOhms) - static_cast<int32_t>(rFirstOhms()) + static_cast<int32_t>(kRStepOhms / 2)) / static_cast<int32_t>(kRStepOhms);
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
    if (v[0] == g_rLoadedIndex) return true; // unchanged: keep the exact current choice
    if (v[0] == 0) {
        g_rChoice = RChoice::AUTO;
        Serial.println("[Menu] Rtotal for the next calibration: AUTO");
    } else {
        g_rChoice     = RChoice::MANUAL;
        g_rManualOhms = rIndexOhms(v[0]);
        Serial.printf("[Menu] Rtotal for the next calibration: %lu ohm\n", static_cast<unsigned long>(g_rManualOhms));
    }
    updateStartLabel();
    return true;
}
const MenuEditor kRTotalEditor = { 1, {4}, ' ', loadRTotal, limitsRTotal, saveRTotal, formatRTotal };

// Start: plain CAL if the choice matches the daughter's current setting.
void menuAction_StartCalibration() {
    syncRChoice();
    const DaughterStatus& s = DaughterBoardLink::instance().status();
    VolumeControl& vc = VolumeControl::instance();
    if (g_rChoice == RChoice::AUTO && !(s.valid && s.rAuto)) {
        vc.calibrate(VolumeControl::CalR::AUTO_R);
        Serial.println("[Menu] Calibration requested, Rtotal AUTO");
    } else if (g_rChoice == RChoice::MANUAL && g_rManualOhms && !(s.valid && !s.rAuto && s.rOhms == g_rManualOhms)) {
        vc.calibrate(VolumeControl::CalR::SET_R, g_rManualOhms);
        Serial.printf("[Menu] Calibration requested, Rtotal %lu ohm\n", static_cast<unsigned long>(g_rManualOhms));
    } else {
        vc.calibrate(VolumeControl::CalR::KEEP_R);
        Serial.println("[Menu] Calibration requested");
    }
}

// Progress page labels for each PROGRESS phase.
const char* phaseLabel(const char* phase) {
    if (!strcmp(phase, "CHECK"))        return "Checking cells";
    if (!strcmp(phase, "SWEEP_SERIES")) return "Measuring series cells";
    if (!strcmp(phase, "SWEEP_SHUNT"))  return "Measuring shunt cells";
    if (!strcmp(phase, "WALK"))         return "Mapping the dark end";
    if (!strcmp(phase, "SOLVE"))        return "Computing volume steps";
    if (!strcmp(phase, "TRIM"))         return "Trimming";
    if (!strcmp(phase, "HOLD"))         return "Settling";
    if (!strcmp(phase, "SEEK"))         return "Stereo matching";
    if (!strcmp(phase, "SAVE"))         return "Saving";
    if (!strcmp(phase, "DONE"))         return "Done";
    return "Starting";
}

// Settings -> Calibration -> Start -> "Start now" lands here: live progress
// while it runs (from PROGRESS, polled every second), the result afterwards.
size_t fillCalibrationProgress(char (*lines)[kInfoLineLen], size_t maxLines) {
    size_t n = 0;
    DaughterBoardLink& link = DaughterBoardLink::instance();
    const DaughterStatus&   s  = link.status();
    const DaughterProgress& pr = link.progress();
    link.requestEnv();

    if (!link.isStatusFresh()) {
        addLine(lines, n, maxLines, "No reply from the daughter board");
        return n;
    }

    if (s.calibrating()) {
        bool known = pr.valid && pr.kind[0] && strcmp(pr.kind, "NONE") != 0;
        addLine(lines, n, maxLines, "Calibrating%s%s%s", known ? " (" : "", known ? pr.kind : "", known ? ")" : "");
        if (pr.valid) {
            if (pr.side[0] && pr.step[0])  addLine(lines, n, maxLines, "%s %s, step %s", phaseLabel(pr.phase), pr.side, pr.step);
            else if (pr.step[0])           addLine(lines, n, maxLines, "%s %s", phaseLabel(pr.phase), pr.step);
            else                           addLine(lines, n, maxLines, "%s", phaseLabel(pr.phase));
            int pct = (pr.pct < 0) ? 0 : (pr.pct > 100 ? 100 : pr.pct);
            char bar[kInfoLineLen];
            constexpr int kBarW = 26;
            int filled = pct * kBarW / 100;
            bar[0] = '[';
            for (int i = 0; i < kBarW; ++i) bar[1 + i] = (i < filled) ? '#' : '-';
            snprintf(bar + 1 + kBarW, sizeof(bar) - 1 - kBarW, "] %d%%", pct);
            addLine(lines, n, maxLines, "%s", bar);
            if (pr.etaS >= 0) addLine(lines, n, maxLines, "About %ld min %02ld s left", static_cast<long>(pr.etaS / 60), static_cast<long>(pr.etaS % 60));
        } else {
            addLine(lines, n, maxLines, "Waiting for progress...");
        }
        char t[12];
        formatTenths(t, sizeof(t), s.tempC);
        addLine(lines, n, maxLines, "Temp %s C, block %s C", t, link.env().valid ? link.env().block : "--");
        addLine(lines, n, maxLines, "Amp muted while it runs");
        return n;
    }

    if (link.calStart() == DaughterCalStart::PENDING || VolumeControl::instance().isCalibrationPending()) {
        addLine(lines, n, maxLines, "Starting...");
        return n;
    }
    if (link.calStart() == DaughterCalStart::REJECTED) {
        addLine(lines, n, maxLines, "Did not start:");
        const char* p = link.getCalStartError();
        while (*p && n < maxLines) {
            char chunk[kInfoLineLen];
            size_t len = strlen(p) < kInfoLineLen - 3 ? strlen(p) : kInfoLineLen - 3;
            memcpy(chunk, p, len); chunk[len] = '\0';
            addLine(lines, n, maxLines, "  %s", chunk);
            p += len;
        }
        return n;
    }

    // Idle: the result of the last calibration.
    const char* result = calStateText(s.cal);
    addLine(lines, n, maxLines, "Result:  %s (last: %s)", result, s.lastCal[0] ? s.lastCal : "--");
    if (s.cal == DaughterCalState::ERR && s.hasErr) {
        char chunk[kInfoLineLen];
        size_t len = strlen(s.err) < kInfoLineLen - 3 ? strlen(s.err) : kInfoLineLen - 3;
        memcpy(chunk, s.err, len); chunk[len] = '\0';
        addLine(lines, n, maxLines, "  %s", chunk);
    }
    if (link.env().valid) addLine(lines, n, maxLines, "Stereo match: %s dB", link.env().match);
    char a[12];
    formatTenths(a, sizeof(a), s.rangeDb);
    addLine(lines, n, maxLines, "Rtotal:  %lu ohm (%s)", static_cast<unsigned long>(s.rOhms), s.rAuto ? "AUTO" : "manual");
    addLine(lines, n, maxLines, "Range:   %s dB, steps %d-%d", a, s.volMin, s.volMax);
    return n;
}

// Settings -> Calibration -> Learning: the daughter's thermal learning
// (rev 9 LEARN) -- how many resyncs it has, how far the block temperature
// has varied, and the per-side fit against the bar it must clear.
size_t fillLearning(char (*lines)[kInfoLineLen], size_t maxLines) {
    size_t n = 0;
    DaughterBoardLink& link = DaughterBoardLink::instance();
    link.requestLearn();
    const DaughterLearn& l = link.learn();
    if (!link.isStatusFresh()) {
        addLine(lines, n, maxLines, "No reply from the daughter board");
        return n;
    }
    if (!l.valid) {
        addLine(lines, n, maxLines, "Waiting for data...");
        return n;
    }
    addLine(lines, n, maxLines, "Compensation: %s", l.comp);
    addLine(lines, n, maxLines, "Resyncs:  %s (needs %s)", l.n, l.minN);
    addLine(lines, n, maxLines, "Spread:   %s C (needs %s)", l.spread, l.minSpread);
    addLine(lines, n, maxLines, "Left:     k %s, R2 %s", l.kL, l.r2L);
    addLine(lines, n, maxLines, "Right:    k %s, R2 %s", l.kR, l.r2R);
    addLine(lines, n, maxLines, "Fit used from R2 %s", l.minR2);
    addLine(lines, n, maxLines, "(k = PWM counts per C of block)");
    return n;
}

// Settings -> Calibration -> Forget (rev 9 FORGET). Cancel is first and
// focused; picking one of the others shows the Status page, where the ~3 s
// of BUSY and the resulting state can be watched.
void menuAction_ForgetCal()  { VolumeControl::instance().forget(DaughterBoardLink::Forget::CAL);  Serial.println("[Menu] FORGET CAL requested"); }
void menuAction_ForgetComp() { VolumeControl::instance().forget(DaughterBoardLink::Forget::COMP); Serial.println("[Menu] FORGET COMP requested"); }
void menuAction_ForgetAll()  { VolumeControl::instance().forget(DaughterBoardLink::Forget::ALL);  Serial.println("[Menu] FORGET ALL requested"); }

const MenuValueOption kForgetOptions[] = {
    { "Cancel",      nullptr,               nullptr },
    { "Calibration", menuAction_ForgetCal,  nullptr }, // volume muted until a new (full) calibration completes
    { "Learning",    menuAction_ForgetComp, nullptr }, // thermal learning log; compensation relearns
    { "Everything",  menuAction_ForgetAll,  nullptr },
};

// "Start" is a two-option confirmation (Cancel is focused by default), since
// a run takes minutes and holds the amp muted; "Start now" then shows the
// progress page.
const MenuValueOption kCalStartOptions[] = {
    { "Cancel",    nullptr,                     nullptr },
    { "Start now", menuAction_StartCalibration, nullptr },
};
// Settings -> Calibration -> Start: pick Rtotal, then the start item --
// labelled with the choice -- asks for confirmation and shows the progress.
const MenuItem kStartSubmenuItems[] = {
    { "Rtotal",    MenuItemType::EDITOR,       nullptr, nullptr, 0, nullptr, 0, nullptr, &kRTotalEditor },
    { g_startLabel, MenuItemType::VALUE_CHOICE, nullptr, kCalStartOptions, 2, nullptr, 0, fillCalibrationProgress },
};
constexpr size_t kStartSubmenuItemCount = sizeof(kStartSubmenuItems) / sizeof(kStartSubmenuItems[0]);

const MenuItem kCalibrationSubmenuItems[] = {
    { "Start",       MenuItemType::SUBMENU,      nullptr, nullptr, 0, kStartSubmenuItems, kStartSubmenuItemCount },
    { "Progress",    MenuItemType::INFO,         nullptr, nullptr, 0, nullptr, 0, fillCalibrationProgress },
    { "Status",      MenuItemType::INFO,         nullptr, nullptr, 0, nullptr, 0, fillCalibrationStatus },
    { "Learning",    MenuItemType::INFO,         nullptr, nullptr, 0, nullptr, 0, fillLearning },
    { "Forget",      MenuItemType::VALUE_CHOICE, nullptr, kForgetOptions, 4, nullptr, 0, fillCalibrationStatus },
};
constexpr size_t kCalibrationSubmenuItemCount = sizeof(kCalibrationSubmenuItems) / sizeof(kCalibrationSubmenuItems[0]);

// --- Settings -> Diagnostics ---
// The action only raises a request; main.cpp closes the menu and runs the
// test screen (a menu action is a plain function with no access to main's
// screen state).
void menuAction_DisplayTest() { DisplayTestScreen::requestStart(); }

const MenuItem kDiagnosticsSubmenuItems[] = {
    { "Display Test", MenuItemType::ACTION, menuAction_DisplayTest, nullptr, 0 },
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
const MenuItem kPowerSubmenuItems[] = {
    { "Standby Display", MenuItemType::VALUE_CHOICE, nullptr, kStandbyDisplayOptions, 3 },
};
constexpr size_t kPowerSubmenuItemCount = sizeof(kPowerSubmenuItems) / sizeof(kPowerSubmenuItems[0]);

// --- Settings ---
const MenuItem kSettingsSubmenuItems[] = {
    { "Brightness",      MenuItemType::VALUE_CHOICE, nullptr, kBrightnessOptions,  4 },
    { "Clock",           MenuItemType::SUBMENU, nullptr, nullptr, 0, kClockSubmenuItems,       kClockSubmenuItemCount },
    { "Power",           MenuItemType::SUBMENU, nullptr, nullptr, 0, kPowerSubmenuItems,       kPowerSubmenuItemCount },
    { "Calibration",     MenuItemType::SUBMENU, nullptr, nullptr, 0, kCalibrationSubmenuItems, kCalibrationSubmenuItemCount },
    { "Diagnostics",     MenuItemType::SUBMENU, nullptr, nullptr, 0, kDiagnosticsSubmenuItems, kDiagnosticsSubmenuItemCount },
    // Read-only: shows the running firmware version; selecting it does nothing.
    { "FW " FW_VERSION,  MenuItemType::ACTION,  nullptr, nullptr, 0 },
};
constexpr size_t kSettingsSubmenuItemCount = sizeof(kSettingsSubmenuItems) / sizeof(kSettingsSubmenuItems[0]);

const MenuItem kRootMenuItems[] = {
    { "Theme",           MenuItemType::SUBMENU,      nullptr, nullptr, 0, kThemeSubmenuItems,    kThemeSubmenuItemCount },
    { "Settings",        MenuItemType::SUBMENU,      nullptr, nullptr, 0, kSettingsSubmenuItems, kSettingsSubmenuItemCount },
};
constexpr size_t kRootMenuItemCount = sizeof(kRootMenuItems) / sizeof(kRootMenuItems[0]);

} // namespace

const MenuItem* getRootMenuItems() { return kRootMenuItems; }
size_t getRootMenuItemCount() { return kRootMenuItemCount; }
