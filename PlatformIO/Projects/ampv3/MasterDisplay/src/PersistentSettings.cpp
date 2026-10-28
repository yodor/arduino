#include "PersistentSettings.hpp"
#include "VolumeControl.hpp"
#include "DisplaySettings.hpp"
#include "ColorTheme.hpp"
#include "AudioEngine.hpp"
#include "SpectrumPipeline.hpp"
#include "WaveformPipeline.hpp"
#include "Renderer.hpp"
#include "BarPhysics.hpp"
#include "Version.hpp"
#include "hardware/flash.h"
#include <LittleFS.h>
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace {

const char* const kPath    = "/settings.txt";
const char* const kTmpPath = "/settings.tmp";

// --- Enum <-> name ---------------------------------------------------------
// Names, in enum order. Stored by name, so reordering an enum in code can
// never change what a saved setting means.
const char* const kChannelModes[]   = { "STEREO", "MONO" };
const char* const kStereoLayouts[]  = { "SIDE_BY_SIDE", "TOP_BOTTOM" };
const char* const kThemes[]         = { "CLASSIC", "SUNSET", "AURORA", "NASTYFFT" };
const char* const kBandLayouts[]    = { "DOUBLING", "FIXED_BANDS", "FULL" };
const char* const kEnergyModes[]    = { "PEAK", "QUADRATURE_SUM" };
const char* const kWindowModes[]    = { "OSCILLOSCOPE", "TRACK_PREVIEW" };
const char* const kPeakDecays[]     = { "SLOW", "NORMAL", "FAST", "OFF" };
const char* const kStandbyDisplays[] = { "CLOCK", "DIM_CLOCK", "OFF" };
const char* const kClockModes[]     = { "CLOCK_ONLY", "SECONDS", "DATE" };
const char* const kVolumeDisplays[] = { "DB", "STEPS" };
const char* const kColorSources[]   = { "CUSTOM", "THEME" };

template <size_t N>
int nameIndex(const char* const (&names)[N], const char* value) {
    for (size_t i = 0; i < N; ++i) if (!strcasecmp(names[i], value)) return static_cast<int>(i);
    return -1;
}
template <size_t N>
const char* nameOf(const char* const (&names)[N], unsigned i) { return i < N ? names[i] : "?"; }

bool parseInt(const char* v, long lo, long hi, long& out) {
    char* end = nullptr;
    long x = strtol(v, &end, 0);
    if (end == v || *end != '\0' || x < lo || x > hi) return false;
    out = x;
    return true;
}

// --- The keys ---------------------------------------------------------------
// One entry per setting: how to write its current value, and how to apply a
// value read from the file (false = not valid; the default stays).
struct Key {
    const char* name;
    void (*write)(char* out, size_t n);
    bool (*read)(const char* v);
};

#define ENUM_KEY(keyName, names, getExpr, setStmt)                                           \
    { keyName,                                                                               \
      [](char* o, size_t n) { snprintf(o, n, "%s", nameOf(names, static_cast<unsigned>(getExpr))); }, \
      [](const char* v) { int i = nameIndex(names, v); if (i < 0) return false; setStmt; return true; } }
#define BOOL_KEY(keyName, getExpr, setStmt)                                                  \
    { keyName,                                                                               \
      [](char* o, size_t n) { snprintf(o, n, "%d", (getExpr) ? 1 : 0); },                    \
      [](const char* v) { long x; if (!parseInt(v, 0, 1, x)) return false; bool b = x != 0; setStmt; return true; } }
#define INT_KEY(keyName, lo, hi, getExpr, setStmt)                                           \
    { keyName,                                                                               \
      [](char* o, size_t n) { snprintf(o, n, "%ld", static_cast<long>(getExpr)); },          \
      [](const char* v) { long x; if (!parseInt(v, lo, hi, x)) return false; setStmt; return true; } }

DisplaySettings& ds() { return DisplaySettings::instance(); }

const Key kKeys[] = {
    ENUM_KEY("theme",           kThemes,         ColorTheme::instance().getPreset(),
             ColorTheme::instance().setPreset(static_cast<ColorPreset>(i))),
    INT_KEY ("brightness",      1, 100,          ds().getBrightnessPercent(),
             { ds().setBrightnessPercent(static_cast<uint8_t>(x)); Renderer::instance().setBacklightPercent(static_cast<uint8_t>(x)); }),
    ENUM_KEY("channel_mode",    kChannelModes,   ds().getChannelMode(),
             ds().setChannelMode(static_cast<ChannelMode>(i))),
    ENUM_KEY("stereo_layout",   kStereoLayouts,  ds().getStereoLayout(),
             ds().setStereoLayout(static_cast<StereoLayout>(i))),
    BOOL_KEY("mirror_left",     ds().getMirrorLeftBars(), ds().setMirrorLeftBars(b)),
    ENUM_KEY("spectrum_bands",  kBandLayouts,    AudioEngine::instance().getSpectrumBandLayoutMode(),
             AudioEngine::instance().setSpectrumBandLayoutMode(static_cast<SpectrumPipeline::BandLayoutMode>(i))),
    ENUM_KEY("spectrum_energy", kEnergyModes,    AudioEngine::instance().getSpectrumEnergyMode(),
             AudioEngine::instance().setSpectrumEnergyMode(static_cast<SpectrumPipeline::EnergyMode>(i))),
    ENUM_KEY("waveform_window", kWindowModes,    AudioEngine::instance().getWaveformWindowMode(),
             AudioEngine::instance().setWaveformWindowMode(static_cast<WaveformPipeline::WindowMode>(i))),
    BOOL_KEY("peaks",           ds().getPeaksEnabled(), ds().setPeaksEnabled(b)),
    ENUM_KEY("peak_decay",      kPeakDecays,     BarPhysics::instance().getPeakDecaySpeed(),
             BarPhysics::instance().setPeakDecaySpeed(static_cast<PeakDecaySpeed>(i))),
    ENUM_KEY("volume_display",  kVolumeDisplays, ds().getVolumeDisplay(),
             ds().setVolumeDisplay(static_cast<VolumeDisplay>(i))),
    INT_KEY ("balance",         -16, 16,         VolumeControl::instance().balance(),
             VolumeControl::instance().restoreBalance(static_cast<int8_t>(x))),
    ENUM_KEY("clock_mode",      kClockModes,     ds().getClockMode(),
             ds().setClockMode(static_cast<ClockMode>(i))),
    BOOL_KEY("clock_24h",       ds().getClock24h(), ds().setClock24h(b)),
    ENUM_KEY("clock_color_source", kColorSources, ds().getClockFollowTheme() ? 1 : 0,
             ds().setClockFollowTheme(i == 1)),
    { "clock_color",   // RGB565 as hex, e.g. 07E0 (green)
      [](char* o, size_t n) { snprintf(o, n, "%04X", static_cast<unsigned>(ds().getClockColor())); },
      [](const char* v) { char* e = nullptr; unsigned long c = strtoul(v, &e, 16);
                          if (e == v || *e != '\0' || c > 0xFFFF) return false;
                          ds().setClockColor(static_cast<uint16_t>(c)); return true; } },
    ENUM_KEY("standby_display", kStandbyDisplays, ds().getStandbyDisplay(),
             ds().setStandbyDisplay(static_cast<StandbyDisplay>(i))),
    INT_KEY ("auto_standby_min", 0, 60,          ds().getAutoStandbyMinutes(),
             ds().setAutoStandbyMinutes(static_cast<uint8_t>(x))),
    INT_KEY ("silence_level",   1, 2047,         ds().getAutoStandbyThreshold(),
             ds().setAutoStandbyThreshold(static_cast<uint16_t>(x))),
};

char* trim(char* s) {
    while (isspace(static_cast<unsigned char>(*s))) ++s;
    char* e = s + strlen(s);
    while (e > s && isspace(static_cast<unsigned char>(e[-1]))) --e;
    *e = '\0';
    return s;
}

// --- The old binary settings, read once for the import -----------------------
// Layout v13 in the last flash sector (the Arduino-Pico EEPROM sector,
// outside the LittleFS area), exactly as the previous firmware wrote it.
constexpr uint32_t kLegacyMagic   = 0x53504B31;
constexpr uint8_t  kLegacyVersion = 13;
constexpr uint32_t kLegacyOffset  = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

#pragma pack(push, 1)
struct LegacyV13 {
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
    uint8_t  volumeDisplay;        // VolumeDisplay (Info screen: dB or steps)
    uint8_t  clockFollowTheme;     // 1 = clock colour from the theme, 0 = clockColor
    int8_t   balance;              // -16..+16 (0.25 dB, positive lowers the left) -- sent to the daughter at its power-up
    uint8_t  checksum; // simple additive checksum over every other byte -- catches blank/corrupt/torn-write flash, not cryptographic integrity
};
#pragma pack(pop)

uint8_t legacyChecksum(const LegacyV13& d) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&d);
    uint8_t sum = 0;
    for (size_t i = 0; i < offsetof(LegacyV13, checksum); ++i) sum = static_cast<uint8_t>(sum + p[i]);
    return sum;
}

} // namespace

PersistentSettings& PersistentSettings::instance() {
    static PersistentSettings inst;
    return inst;
}

void PersistentSettings::applyLine(char* line, int lineNo) {
    char* hash = strchr(line, '#');
    if (hash) *hash = '\0';
    char* s = trim(line);
    if (!*s) return;
    char* eq = strchr(s, '=');
    if (!eq) {
        Serial.printf("[Settings] line %d ignored (no '=')\n", lineNo);
        return;
    }
    *eq = '\0';
    char* key = trim(s);
    char* val = trim(eq + 1);
    for (const Key& k : kKeys) {
        if (strcasecmp(k.name, key)) continue;
        if (!k.read(val)) Serial.printf("[Settings] %s=%s: not a valid value -- default kept\n", key, val);
        return;
    }
    Serial.printf("[Settings] unknown key '%s' ignored\n", key);
}

void PersistentSettings::loadAndApply() {
    m_mounted = LittleFS.begin();
    if (!m_mounted) {
        Serial.println("[Settings] LittleFS not available -- defaults in use, nothing will be saved. "
                       "Choose a Flash Size with an FS partition (Tools menu).");
        return;
    }

    File f = LittleFS.open(kPath, "r");
    if (!f) {
        if (importLegacy()) {
            Serial.println("[Settings] Imported the old binary settings (v13) into " "/settings.txt");
            save();
        } else {
            Serial.println("[Settings] No settings file yet -- defaults in use");
        }
        return;
    }

    char line[128];
    size_t len = 0;
    int lineNo = 0, applied = 0;
    while (true) {
        int c = f.read();
        if (c < 0 || c == '\n') {
            line[len] = '\0';
            ++lineNo;
            if (len) { applyLine(line, lineNo); ++applied; }
            len = 0;
            if (c < 0) break;
        } else if (c != '\r' && len < sizeof(line) - 1) {
            line[len++] = static_cast<char>(c);
        }
    }
    f.close();
    Serial.printf("[Settings] Loaded %s (%d lines)\n", kPath, lineNo);
    (void)applied;
}

bool PersistentSettings::importLegacy() {
    const LegacyV13* d = reinterpret_cast<const LegacyV13*>(XIP_BASE + kLegacyOffset);
    if (d->magic != kLegacyMagic || d->version != kLegacyVersion || legacyChecksum(*d) != d->checksum) return false;

    DisplaySettings& s = ds();
    s.setChannelMode(static_cast<ChannelMode>(d->channelMode));
    s.setStereoLayout(static_cast<StereoLayout>(d->stereoLayout));
    s.setMirrorLeftBars(d->mirrorLeftBars != 0);
    ColorTheme::instance().setPreset(static_cast<ColorPreset>(d->colorPreset));
    AudioEngine::instance().setSpectrumBandLayoutMode(static_cast<SpectrumPipeline::BandLayoutMode>(d->bandLayoutMode));
    AudioEngine::instance().setSpectrumEnergyMode(static_cast<SpectrumPipeline::EnergyMode>(d->energyMode));
    AudioEngine::instance().setWaveformWindowMode(static_cast<WaveformPipeline::WindowMode>(d->waveformWindowMode));
    s.setBrightnessPercent(d->brightnessPercent);
    Renderer::instance().setBacklightPercent(d->brightnessPercent);
    s.setPeaksEnabled(d->peaksEnabled != 0);
    BarPhysics::instance().setPeakDecaySpeed(static_cast<PeakDecaySpeed>(d->peakDecaySpeed));
    s.setClock24h(d->clock24h != 0);
    s.setStandbyDisplay(static_cast<StandbyDisplay>(d->standbyDisplay));
    s.setClockMode(static_cast<ClockMode>(d->clockMode));
    s.setClockColor(d->clockColor);
    s.setAutoStandbyMinutes(d->autoStandbyMin <= 60 ? d->autoStandbyMin : 0);
    if (d->autoStandbyThreshold >= 1 && d->autoStandbyThreshold <= 2047) s.setAutoStandbyThreshold(d->autoStandbyThreshold);
    s.setVolumeDisplay(d->volumeDisplay == 1 ? VolumeDisplay::STEPS : VolumeDisplay::DB);
    s.setClockFollowTheme(d->clockFollowTheme != 0);
    if (d->balance >= -16 && d->balance <= 16) VolumeControl::instance().restoreBalance(d->balance);
    return true;
}

void PersistentSettings::saveIfDirty() {
    if (!m_dirty) return;
    save();
}

// Written to a temporary file, then renamed over the real one: a power cut
// mid-write leaves the previous file intact.
void PersistentSettings::save() {
    m_dirty = false;
    if (!m_mounted) return;
    File f = LittleFS.open(kTmpPath, "w");
    if (!f) {
        Serial.println("[Settings] Could not write the settings file");
        return;
    }
    f.print("# " FW_NAME " settings -- key=value; unknown keys are ignored, missing ones use defaults\n");
    f.print("# saved by firmware " FW_VERSION "\n");
    char value[24], line[64];
    for (const Key& k : kKeys) {
        k.write(value, sizeof(value));
        snprintf(line, sizeof(line), "%s=%s\n", k.name, value);
        f.print(line);
    }
    f.close();
    if (!LittleFS.rename(kTmpPath, kPath)) {
        Serial.println("[Settings] Could not replace the settings file");
        return;
    }
    Serial.println("[Settings] Saved");
}

void PersistentSettings::printFile() {
    if (!m_mounted) { Serial.println("[Settings] LittleFS not available"); return; }
    File f = LittleFS.open(kPath, "r");
    if (!f) { Serial.println("[Settings] No settings file -- defaults in use"); return; }
    while (f.available()) Serial.write(static_cast<uint8_t>(f.read()));
    f.close();
}

bool PersistentSettings::resetFile() {
    if (!m_mounted) return false;
    m_dirty = false;
    return LittleFS.remove(kPath);
}
