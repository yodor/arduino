#include "MenuContent.hpp"
#include "ScreenRegistry.hpp"
#include "VolumeControl.hpp"
#include "DisplaySettings.hpp"
#include "ColorTheme.hpp"
#include "AudioEngine.hpp"
#include "SpectrumPipeline.hpp"
#include "WaveformPipeline.hpp"
#include "PersistentSettings.hpp"
#include <Arduino.h>

namespace {

void menuAction_Calibrate() {
    VolumeControl::instance().calibrate();
    Serial.println("[Menu] Calibration requested");
}

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

// Wave Window Mode: Oscilloscope (kOscilloscopeSamples, a genuine "few
// cycles" view of waveform shape) vs Track Preview (kTrackPreviewSamples,
// a much longer span showing amplitude envelope instead -- see
// WaveformPipeline.hpp for the full reasoning behind both). Same core0-
// sets/core1-applies pattern as the FFT settings above.
void menuSet_WaveOscilloscope() { AudioEngine::instance().setWaveformWindowMode(WaveformPipeline::WindowMode::OSCILLOSCOPE);  PersistentSettings::instance().markDirty(); }
void menuSet_WaveTrackPreview() { AudioEngine::instance().setWaveformWindowMode(WaveformPipeline::WindowMode::TRACK_PREVIEW); PersistentSettings::instance().markDirty(); }
bool menuIsActive_WaveOscilloscope() { return AudioEngine::instance().getWaveformWindowMode() == WaveformPipeline::WindowMode::OSCILLOSCOPE; }
bool menuIsActive_WaveTrackPreview() { return AudioEngine::instance().getWaveformWindowMode() == WaveformPipeline::WindowMode::TRACK_PREVIEW; }

// Template rather than one hand-written function per screen index -- each
// instantiation is still a genuinely distinct function pointer (required
// since MenuValueOption uses plain `void(*)()`, which can't carry a bound
// argument), but adding a screen is now just one more line in
// kStartScreenOptions[] below instead of a new pair of named functions.
template <uint8_t N>
void menuSet_StartScreen() { DisplaySettings::instance().setStartingScreenIndex(N); PersistentSettings::instance().markDirty(); }

template <uint8_t N>
bool menuIsActive_StartScreen() { return DisplaySettings::instance().getStartingScreenIndex() == N; }

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

// Order/count must match g_screens[] in main.cpp -- enforced there via
// static_assert(kNumScreens == kScreenCount, ...) rather than silently.
static_assert(kScreenCount == 4, "kStartScreenOptions below is hand-written for exactly 4 screens -- add/remove a row here too if kScreenCount changes");
const MenuValueOption kStartScreenOptions[] = {
    { kScreenNames[0], menuSet_StartScreen<0>, menuIsActive_StartScreen<0> },
    { kScreenNames[1], menuSet_StartScreen<1>, menuIsActive_StartScreen<1> },
    { kScreenNames[2], menuSet_StartScreen<2>, menuIsActive_StartScreen<2> },
    { kScreenNames[3], menuSet_StartScreen<3>, menuIsActive_StartScreen<3> },
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

// "Theme" submenu, nested at root -- display/appearance settings grouped
// together, with "FFT" and "Wave" themselves one further level in.
const MenuItem kThemeSubmenuItems[] = {
    { "Stereo / Mono", MenuItemType::VALUE_CHOICE, nullptr, kChannelModeOptions, 2 },
    { "Colors",        MenuItemType::VALUE_CHOICE, nullptr, kColorOptions,       4 },
    { "FFT",           MenuItemType::SUBMENU,      nullptr, nullptr, 0, kFftSubmenuItems,  kFftSubmenuItemCount },
    { "Wave",          MenuItemType::SUBMENU,      nullptr, nullptr, 0, kWaveSubmenuItems, kWaveSubmenuItemCount },
};
constexpr size_t kThemeSubmenuItemCount = sizeof(kThemeSubmenuItems) / sizeof(kThemeSubmenuItems[0]);

const MenuItem kRootMenuItems[] = {
    { "Starting Screen", MenuItemType::VALUE_CHOICE, nullptr,              kStartScreenOptions, kScreenCount },
    { "Theme",           MenuItemType::SUBMENU,      nullptr,              nullptr, 0, kThemeSubmenuItems, kThemeSubmenuItemCount },
    { "Calibration",     MenuItemType::ACTION,       menuAction_Calibrate, nullptr, 0 },
};
constexpr size_t kRootMenuItemCount = sizeof(kRootMenuItems) / sizeof(kRootMenuItems[0]);

} // namespace

const MenuItem* getRootMenuItems() { return kRootMenuItems; }
size_t getRootMenuItemCount() { return kRootMenuItemCount; }