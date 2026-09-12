#include "MenuContent.hpp"
#include "ScreenRegistry.hpp"
#include "VolumeControl.hpp"
#include "DisplaySettings.hpp"
#include "ColorTheme.hpp"
#include <Arduino.h>

namespace {

void menuAction_Calibrate() {
    VolumeControl::instance().calibrate();
    Serial.println("[Menu] Calibration requested");
}

void menuSet_Stereo() { DisplaySettings::instance().setChannelMode(ChannelMode::STEREO); }
void menuSet_Mono()   { DisplaySettings::instance().setChannelMode(ChannelMode::MONO); }
bool menuIsActive_Stereo() { return DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO; }
bool menuIsActive_Mono()   { return DisplaySettings::instance().getChannelMode() == ChannelMode::MONO; }

void menuSet_ColorClassic()  { ColorTheme::instance().setPreset(ColorPreset::CLASSIC); }
void menuSet_ColorOcean()    { ColorTheme::instance().setPreset(ColorPreset::OCEAN); }
void menuSet_ColorColorful() { ColorTheme::instance().setPreset(ColorPreset::COLORFUL); }
void menuSet_ColorNastyFFT() { ColorTheme::instance().setPreset(ColorPreset::NASTYFFT); }
bool menuIsActive_ColorClassic()  { return ColorTheme::instance().getPreset() == ColorPreset::CLASSIC; }
bool menuIsActive_ColorOcean()    { return ColorTheme::instance().getPreset() == ColorPreset::OCEAN; }
bool menuIsActive_ColorColorful() { return ColorTheme::instance().getPreset() == ColorPreset::COLORFUL; }
bool menuIsActive_ColorNastyFFT() { return ColorTheme::instance().getPreset() == ColorPreset::NASTYFFT; }

// Template rather than one hand-written function per screen index -- each
// instantiation is still a genuinely distinct function pointer (required
// since MenuValueOption uses plain `void(*)()`, which can't carry a bound
// argument), but adding a screen is now just one more line in
// kStartScreenOptions[] below instead of a new pair of named functions.
template <uint8_t N>
void menuSet_StartScreen() { DisplaySettings::instance().setStartingScreenIndex(N); }

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

// Order/count must match g_screens[] in main.cpp -- enforced there via
// static_assert(kNumScreens == kScreenCount, ...) rather than silently.
static_assert(kScreenCount == 5, "kStartScreenOptions below is hand-written for exactly 5 screens -- add/remove a row here too if kScreenCount changes");
const MenuValueOption kStartScreenOptions[] = {
    { kScreenNames[0], menuSet_StartScreen<0>, menuIsActive_StartScreen<0> },
    { kScreenNames[1], menuSet_StartScreen<1>, menuIsActive_StartScreen<1> },
    { kScreenNames[2], menuSet_StartScreen<2>, menuIsActive_StartScreen<2> },
    { kScreenNames[3], menuSet_StartScreen<3>, menuIsActive_StartScreen<3> },
    { kScreenNames[4], menuSet_StartScreen<4>, menuIsActive_StartScreen<4> },
};

const MenuItem kRootMenuItems[] = {
    { "Colors",          MenuItemType::VALUE_CHOICE, nullptr,              kColorOptions,       4 },
    { "Calibration",     MenuItemType::ACTION,       menuAction_Calibrate, nullptr,             0 },
    { "Stereo / Mono",   MenuItemType::VALUE_CHOICE, nullptr,              kChannelModeOptions, 2 },
    { "Starting Screen", MenuItemType::VALUE_CHOICE, nullptr,              kStartScreenOptions, kScreenCount },
};
constexpr size_t kRootMenuItemCount = sizeof(kRootMenuItems) / sizeof(kRootMenuItems[0]);

} // namespace

const MenuItem* getRootMenuItems() { return kRootMenuItems; }
size_t getRootMenuItemCount() { return kRootMenuItemCount; }