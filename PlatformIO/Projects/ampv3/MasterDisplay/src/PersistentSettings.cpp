#include "PersistentSettings.hpp"
#include "DisplaySettings.hpp"
#include "ColorTheme.hpp"
#include "AudioEngine.hpp"
#include "SpectrumPipeline.hpp"
#include "WaveformPipeline.hpp"
#include "Renderer.hpp"
#include "BarPhysics.hpp"
#include "hardware/flash.h"
#include "pico/platform.h"
#include <cstring>

namespace {

constexpr uint32_t kMagic   = 0x53504B31; // arbitrary sentinel ("SPK1"), confirms this flash region was ever written by us
constexpr uint8_t  kVersion = 10;         // bump whenever PersistentData's layout changes -- 5: clock options, 6: standby display,
                                           // 7: + volume step (dropped again), 8: = the v6 layout, 9: clock mode + color,
                                           // auto standby, 10: auto standby silence level. 4-9 are still read, see loadAndApply().

// Last flash sector -- same convention the Arduino-Pico EEPROM library
// itself uses, chosen so this doesn't collide with program flash or a
// filesystem region if one is ever added later. Offset is flash-relative
// (as flash_range_erase()/flash_range_program() expect), not an absolute
// memory address.
constexpr uint32_t kFlashOffset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

#pragma pack(push, 1)
// Version 4 layout, kept so an older save is migrated instead of discarded.
struct PersistentDataV4 {
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
    uint8_t  peaksEnabled;
    uint8_t  peakDecaySpeed;
    uint8_t  checksum;
};

// Version 5 layout (v4 + the clock options), likewise kept for migration.
struct PersistentDataV5 {
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
    uint8_t  peaksEnabled;
    uint8_t  peakDecaySpeed;
    uint8_t  clockShowDate;
    uint8_t  clockShowSeconds;
    uint8_t  clock24h;
    uint8_t  checksum;
};

// Version 6 layout (v5 + standby display), likewise kept for migration.
struct PersistentDataV6 {
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
    uint8_t  peaksEnabled;
    uint8_t  peakDecaySpeed;
    uint8_t  clockShowDate;
    uint8_t  clockShowSeconds;
    uint8_t  clock24h;
    uint8_t  standbyDisplay;
    uint8_t  checksum;
};

// Version 7 layout: v6 + a volume step that is no longer used (the volume
// is the daughter's alone) -- kept only so such a save is still read.
struct PersistentDataV7 {
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
    uint8_t  peaksEnabled;
    uint8_t  peakDecaySpeed;
    uint8_t  clockShowDate;
    uint8_t  clockShowSeconds;
    uint8_t  clock24h;
    uint8_t  standbyDisplay;
    uint8_t  volumeStep;
    uint8_t  checksum;
};

// Version 9 layout (clock mode/color, auto standby minutes), kept for migration.
struct PersistentDataV9 {
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
    uint8_t  peaksEnabled;
    uint8_t  peakDecaySpeed;
    uint8_t  clockShowDate;
    uint8_t  clockShowSeconds;
    uint8_t  clock24h;
    uint8_t  standbyDisplay;
    uint8_t  clockMode;        // ClockMode (clockShowDate/clockShowSeconds above are no longer used)
    uint16_t clockColor;       // RGB565
    uint8_t  autoStandbyMin;   // 0 = off
    uint8_t  checksum; // simple additive checksum over every other byte -- catches blank/corrupt/torn-write flash, not cryptographic integrity
};

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
    uint8_t  peaksEnabled;
    uint8_t  peakDecaySpeed;
    uint8_t  clockShowDate;
    uint8_t  clockShowSeconds;
    uint8_t  clock24h;
    uint8_t  standbyDisplay;
    uint8_t  clockMode;        // ClockMode (clockShowDate/clockShowSeconds above are no longer used)
    uint16_t clockColor;       // RGB565
    uint8_t  autoStandbyMin;   // 0 = off
    uint16_t autoStandbyThreshold; // input peak, ADC counts
    uint8_t  checksum; // simple additive checksum over every other byte -- catches blank/corrupt/torn-write flash, not cryptographic integrity
};
#pragma pack(pop)

// Additive checksum over every byte but the last (the checksum field itself).
template <typename T>
uint8_t computeChecksum(const T& d) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&d);
    uint8_t sum = 0;
    for (size_t i = 0; i < sizeof(T) - 1; ++i) sum += bytes[i];
    return sum;
}

template <typename T>
const T* storedAs() {
    return reinterpret_cast<const T*>(XIP_BASE + kFlashOffset);
}

// Fields every layout version shares (v4 and v5 agree on all of these).
template <typename T>
void applyCommon(const T* stored) {
    DisplaySettings::instance().setChannelMode(static_cast<ChannelMode>(stored->channelMode));
    DisplaySettings::instance().setStereoLayout(static_cast<StereoLayout>(stored->stereoLayout));
    DisplaySettings::instance().setMirrorLeftBars(stored->mirrorLeftBars != 0);
    ColorTheme::instance().setPreset(static_cast<ColorPreset>(stored->colorPreset));
    AudioEngine::instance().setSpectrumBandLayoutMode(static_cast<SpectrumPipeline::BandLayoutMode>(stored->bandLayoutMode));
    AudioEngine::instance().setSpectrumEnergyMode(static_cast<SpectrumPipeline::EnergyMode>(stored->energyMode));
    AudioEngine::instance().setWaveformWindowMode(static_cast<WaveformPipeline::WindowMode>(stored->waveformWindowMode));
    DisplaySettings::instance().setBrightnessPercent(stored->brightnessPercent);
    Renderer::instance().setBacklightPercent(stored->brightnessPercent); // Renderer::init() already ran with a temporary default -- this pushes the real saved value to the pin
    DisplaySettings::instance().setPeaksEnabled(stored->peaksEnabled != 0);
    BarPhysics::instance().setPeakDecaySpeed(static_cast<PeakDecaySpeed>(stored->peakDecaySpeed));
}

// The clock options (v5 and v6).
template <typename T>
void applyClock(const T* stored) {
    // Layouts 5-8 had two switches; Seconds won over Date if both were on.
    DisplaySettings::instance().setClockMode(stored->clockShowSeconds ? ClockMode::SECONDS
                                             : stored->clockShowDate  ? ClockMode::DATE
                                                                      : ClockMode::CLOCK_ONLY);
    DisplaySettings::instance().setClock24h(stored->clock24h != 0);
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
    const PersistentData* stored = storedAs<PersistentData>();

    if (stored->magic != kMagic) {
        Serial.println("[Settings] No saved settings found (fresh device) -- using defaults");
        return;
    }

    if (stored->version == kVersion) {
        if (computeChecksum(*stored) != stored->checksum) {
            Serial.println("[Settings] Saved settings failed checksum -- using defaults");
            return;
        }
        applyCommon(stored);
        DisplaySettings::instance().setClockMode(static_cast<ClockMode>(stored->clockMode));
        DisplaySettings::instance().setClock24h(stored->clock24h != 0);
        DisplaySettings::instance().setClockColor(stored->clockColor);
        DisplaySettings::instance().setStandbyDisplay(static_cast<StandbyDisplay>(stored->standbyDisplay));
        DisplaySettings::instance().setAutoStandbyMinutes(stored->autoStandbyMin <= 60 ? stored->autoStandbyMin : 0);
        if (stored->autoStandbyThreshold >= 1 && stored->autoStandbyThreshold <= 2047) {
            DisplaySettings::instance().setAutoStandbyThreshold(stored->autoStandbyThreshold);
        }
        Serial.println("[Settings] Loaded saved settings from flash");
        return;
    }

    if (stored->version == 9) {
        // Previous layout: everything but the silence level (default).
        const PersistentDataV9* old = storedAs<PersistentDataV9>();
        if (computeChecksum(*old) != old->checksum) {
            Serial.println("[Settings] Saved settings (v9) failed checksum -- using defaults");
            return;
        }
        applyCommon(old);
        DisplaySettings::instance().setClockMode(static_cast<ClockMode>(old->clockMode));
        DisplaySettings::instance().setClock24h(old->clock24h != 0);
        DisplaySettings::instance().setClockColor(old->clockColor);
        DisplaySettings::instance().setStandbyDisplay(static_cast<StandbyDisplay>(old->standbyDisplay));
        DisplaySettings::instance().setAutoStandbyMinutes(old->autoStandbyMin <= 60 ? old->autoStandbyMin : 0);
        Serial.println("[Settings] Loaded v9 settings from flash (silence level at default)");
        return;
    }

    if (stored->version == 7) {
        // Short-lived layout with a volume step -- everything else is used.
        const PersistentDataV7* old = storedAs<PersistentDataV7>();
        if (computeChecksum(*old) != old->checksum) {
            Serial.println("[Settings] Saved settings (v7) failed checksum -- using defaults");
            return;
        }
        applyCommon(old);
        applyClock(old);
        DisplaySettings::instance().setStandbyDisplay(static_cast<StandbyDisplay>(old->standbyDisplay));
        Serial.println("[Settings] Loaded v7 settings from flash");
        return;
    }

    if (stored->version == 6 || stored->version == 8) {
        // The v6 layout (v8 used it too): no clock color / auto standby yet.
        const PersistentDataV6* old = storedAs<PersistentDataV6>();
        if (computeChecksum(*old) != old->checksum) {
            Serial.println("[Settings] Saved settings (v6) failed checksum -- using defaults");
            return;
        }
        applyCommon(old);
        applyClock(old);
        DisplaySettings::instance().setStandbyDisplay(static_cast<StandbyDisplay>(old->standbyDisplay));
        Serial.printf("[Settings] Loaded v%u settings from flash (clock color, auto standby at defaults)\n", stored->version);
        return;
    }

    if (stored->version == 5) {
        // Previous layout: everything but the standby display, which starts
        // at its default; the next save writes v6.
        const PersistentDataV5* old = storedAs<PersistentDataV5>();
        if (computeChecksum(*old) != old->checksum) {
            Serial.println("[Settings] Saved settings (v5) failed checksum -- using defaults");
            return;
        }
        applyCommon(old);
        applyClock(old);
        Serial.println("[Settings] Loaded v5 settings from flash (standby display at default)");
        return;
    }

    if (stored->version == 4) {
        // Previous layout: everything but the clock options. Keep it; the
        // clock options start at their defaults, and the next save writes v5.
        const PersistentDataV4* old = storedAs<PersistentDataV4>();
        if (computeChecksum(*old) != old->checksum) {
            Serial.println("[Settings] Saved settings (v4) failed checksum -- using defaults");
            return;
        }
        applyCommon(old);
        Serial.println("[Settings] Loaded v4 settings from flash (clock options at defaults)");
        return;
    }

    Serial.println("[Settings] Saved settings are from an incompatible firmware version -- using defaults");
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
    data.startingScreenIndex = 0;   // unused since the unit always boots on the Clock (kept so the layout stays put)
    data.stereoLayout        = static_cast<uint8_t>(DisplaySettings::instance().getStereoLayout());
    data.mirrorLeftBars      = DisplaySettings::instance().getMirrorLeftBars() ? 1 : 0;
    data.colorPreset         = static_cast<uint8_t>(ColorTheme::instance().getPreset());
    data.bandLayoutMode      = static_cast<uint8_t>(AudioEngine::instance().getSpectrumBandLayoutMode());
    data.energyMode          = static_cast<uint8_t>(AudioEngine::instance().getSpectrumEnergyMode());
    data.waveformWindowMode  = static_cast<uint8_t>(AudioEngine::instance().getWaveformWindowMode());
    data.brightnessPercent   = DisplaySettings::instance().getBrightnessPercent();
    data.peaksEnabled        = DisplaySettings::instance().getPeaksEnabled() ? 1 : 0;
    data.peakDecaySpeed      = static_cast<uint8_t>(BarPhysics::instance().getPeakDecaySpeed());
    data.clockShowDate       = 0;   // unused since v9 (clockMode)
    data.clockShowSeconds    = 0;
    data.clock24h            = DisplaySettings::instance().getClock24h() ? 1 : 0;
    data.standbyDisplay      = static_cast<uint8_t>(DisplaySettings::instance().getStandbyDisplay());
    data.clockMode           = static_cast<uint8_t>(DisplaySettings::instance().getClockMode());
    data.clockColor          = DisplaySettings::instance().getClockColor();
    data.autoStandbyMin      = DisplaySettings::instance().getAutoStandbyMinutes();
    data.autoStandbyThreshold = DisplaySettings::instance().getAutoStandbyThreshold();
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
