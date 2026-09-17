#include "PersistentSettings.hpp"
#include "DisplaySettings.hpp"
#include "ColorTheme.hpp"
#include "AudioEngine.hpp"
#include "SpectrumPipeline.hpp"
#include "WaveformPipeline.hpp"
#include "Renderer.hpp"
#include "hardware/flash.h"
#include "pico/platform.h"
#include <cstring>

namespace {

constexpr uint32_t kMagic   = 0x53504B31; // arbitrary sentinel ("SPK1"), confirms this flash region was ever written by us
constexpr uint8_t  kVersion = 3;          // bump whenever PersistentData's layout changes incompatibly -- 3: added brightnessPercent

// Last flash sector -- same convention the Arduino-Pico EEPROM library
// itself uses, chosen so this doesn't collide with program flash or a
// filesystem region if one is ever added later. Offset is flash-relative
// (as flash_range_erase()/flash_range_program() expect), not an absolute
// memory address.
constexpr uint32_t kFlashOffset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

#pragma pack(push, 1)
struct PersistentData {
    uint32_t magic;
    uint8_t  version;
    uint8_t  channelMode;
    uint8_t  startingScreenIndex;
    uint8_t  stereoLayout;
    uint8_t  mirrorLeftBars;
    uint8_t  colorPreset;
    uint8_t  bandLayoutMode;
    uint8_t  energyMode;
    uint8_t  waveformWindowMode;
    uint8_t  brightnessPercent;
    uint8_t  checksum; // simple additive checksum over every other byte -- catches blank/corrupt/torn-write flash, not cryptographic integrity
};
#pragma pack(pop)

uint8_t computeChecksum(const PersistentData& d) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&d);
    uint8_t sum = 0;
    for (size_t i = 0; i < sizeof(PersistentData) - 1; ++i) sum += bytes[i]; // -1: exclude the checksum field itself
    return sum;
}

const PersistentData* storedData() {
    return reinterpret_cast<const PersistentData*>(XIP_BASE + kFlashOffset);
}

// The cross-core interlock's two flags. File-local since both sides that
// touch them (commitToFlash() below and checkAndPauseForFlashWrite())
// live in this one translation unit -- AudioEngine only ever calls the
// static method, never these directly.
volatile bool g_core0WantsFlashPause = false;
volatile bool g_core1Paused          = false;

} // namespace

PersistentSettings& PersistentSettings::instance() {
    static PersistentSettings inst;
    return inst;
}

void PersistentSettings::loadAndApply() {
    const PersistentData* stored = storedData();

    if (stored->magic != kMagic || stored->version != kVersion) {
        Serial.println("[Settings] No valid saved settings found (fresh device or version change) -- using defaults");
        return;
    }
    if (computeChecksum(*stored) != stored->checksum) {
        Serial.println("[Settings] Saved settings failed checksum -- using defaults");
        return;
    }

    DisplaySettings::instance().setChannelMode(static_cast<ChannelMode>(stored->channelMode));
    DisplaySettings::instance().setStartingScreenIndex(stored->startingScreenIndex); // main.cpp's setup() clamps this if it's out of range for the current screen count
    DisplaySettings::instance().setStereoLayout(static_cast<StereoLayout>(stored->stereoLayout));
    DisplaySettings::instance().setMirrorLeftBars(stored->mirrorLeftBars != 0);
    ColorTheme::instance().setPreset(static_cast<ColorPreset>(stored->colorPreset));
    AudioEngine::instance().setSpectrumBandLayoutMode(static_cast<SpectrumPipeline::BandLayoutMode>(stored->bandLayoutMode));
    AudioEngine::instance().setSpectrumEnergyMode(static_cast<SpectrumPipeline::EnergyMode>(stored->energyMode));
    AudioEngine::instance().setWaveformWindowMode(static_cast<WaveformPipeline::WindowMode>(stored->waveformWindowMode));
    DisplaySettings::instance().setBrightnessPercent(stored->brightnessPercent);
    Renderer::instance().setBacklightPercent(stored->brightnessPercent); // Renderer::init() already ran with a temporary default -- this pushes the real saved value to the pin

    Serial.println("[Settings] Loaded saved settings from flash");
}

void PersistentSettings::saveIfDirty() {
    if (!m_dirty) return;
    commitToFlash();
    m_dirty = false;
}

void PersistentSettings::commitToFlash() {
    PersistentData data{};
    data.magic               = kMagic;
    data.version             = kVersion;
    data.channelMode         = static_cast<uint8_t>(DisplaySettings::instance().getChannelMode());
    data.startingScreenIndex = DisplaySettings::instance().getStartingScreenIndex();
    data.stereoLayout        = static_cast<uint8_t>(DisplaySettings::instance().getStereoLayout());
    data.mirrorLeftBars      = DisplaySettings::instance().getMirrorLeftBars() ? 1 : 0;
    data.colorPreset         = static_cast<uint8_t>(ColorTheme::instance().getPreset());
    data.bandLayoutMode      = static_cast<uint8_t>(AudioEngine::instance().getSpectrumBandLayoutMode());
    data.energyMode          = static_cast<uint8_t>(AudioEngine::instance().getSpectrumEnergyMode());
    data.waveformWindowMode  = static_cast<uint8_t>(AudioEngine::instance().getWaveformWindowMode());
    data.brightnessPercent   = DisplaySettings::instance().getBrightnessPercent();
    data.checksum            = computeChecksum(data);

    // flash_range_program() requires a page-aligned, page-sized source --
    // pad with zeros; the padding is never read back as meaningful data.
    alignas(4) uint8_t page[FLASH_PAGE_SIZE] = {};
    memcpy(page, &data, sizeof(data));

    Serial.println("[Settings] Saving to flash...");

    // Signal core1 and wait for its confirmation that it's actually
    // parked, before touching flash at all.
    g_core0WantsFlashPause = true;
    while (!g_core1Paused) {
        tight_loop_contents();
    }

    noInterrupts();
    flash_range_erase(kFlashOffset, FLASH_SECTOR_SIZE);
    flash_range_program(kFlashOffset, page, sizeof(page));
    interrupts();

    // Release core1 and wait for it to actually resume before returning,
    // so nothing downstream assumes the pause is over prematurely.
    g_core0WantsFlashPause = false;
    while (g_core1Paused) {
        tight_loop_contents();
    }

    Serial.println("[Settings] Save complete");
}

// Deliberately a true leaf function living entirely in RAM
// (__not_in_flash_func): while core0 is mid-erase/program, flash cannot
// be read at all -- neither for data nor for instruction fetch -- so
// EVERYTHING this function touches while core0's pause flag is set must
// already be in RAM. That rules out calling any other flash-resident
// function from inside the wait loop below, including Serial.print, or
// referencing any const data that the compiler placed in flash. The only
// things read/written here are the two volatile flags themselves and the
// SDK's own tight_loop_contents() (an inline hint, not a flash-resident
// call).
void __not_in_flash_func(PersistentSettings::checkAndPauseForFlashWrite)() {
    if (!g_core0WantsFlashPause) return; // fast path: true on every call except the rare moment a save is actually happening
    g_core1Paused = true;
    while (g_core0WantsFlashPause) {
        tight_loop_contents();
    }
    g_core1Paused = false;
}