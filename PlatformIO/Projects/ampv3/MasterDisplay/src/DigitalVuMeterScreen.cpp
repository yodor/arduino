#include "DigitalVuMeterScreen.hpp"
#include <climits>
#include <cmath>
#include <cstdio>
#include "Renderer.hpp"
#include "AudioEngine.hpp"
#include "DisplaySettings.hpp"
#include "AudioLevel.hpp"
#include "ColorTheme.hpp"
#include "BarPhysics.hpp"

namespace {
// Layout across the panel: channel letter | bar | dB readout.
constexpr uint8_t  kTextSize    = 2;   // letters and dB readout: 12x16 px characters
constexpr int16_t  kLetterX     = 4;
constexpr uint16_t kBarX        = 22;  // bars start after the letter column
constexpr uint8_t  kDbChars     = 5;   // "-23.5"
constexpr int16_t  kDbX         = SCREEN_WIDTH - 2 - kDbChars * 6 * kTextSize;
constexpr uint16_t kMaxBarWidth = static_cast<uint16_t>(kDbX - 6 - kBarX); // the peak marker's farthest spot stays clear of the readout
constexpr float    kFloorDb     = -50.0f; // AudioLevel::dbNormalize(): -50 dB -> 0, 0 dB -> 1
constexpr uint32_t kDbRefreshMs = 200;
constexpr uint16_t kTextColor   = 0xC618; // light gray
}

void DigitalVuMeterScreen::resetBarState(BarPhysicsState& state) {
    state.prevUnits      = 0;
    state.prevPeakUnits  = 0;
}

void DigitalVuMeterScreen::onEnter() {
    Renderer::instance().clear();
    // Deliberately not resetting decay/peak/peakTimer -- same reasoning as
    // SpectrumScreen: audio-reactive smoothing carries over continuously
    // across screen switches and channel-mode toggles.
    resetBarState(m_left);
    resetBarState(m_right);
    resetBarState(m_mono);
    m_dbL = m_dbR = m_dbM = DbReadout{};
    if (DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO) {
        drawChannelLetter("L", 10, 55);
        drawChannelLetter("R", SCREEN_HEIGHT - 10 - 55, 55);
    }
}

// "L" / "R" in the left column, centred on the bar.
void DigitalVuMeterScreen::drawChannelLetter(const char* letter, uint16_t y, uint16_t barHeight) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    gfx->setTextSize(kTextSize);
    gfx->setTextColor(kTextColor, COLOR_BLACK);
    gfx->setCursor(kLetterX, y + (barHeight - 8 * kTextSize) / 2);
    gfx->print(letter);
}

// The bar's smoothed level in dBFS at the right, e.g. "-23.5"; "---" at the
// floor (silence). Fixed width, right-aligned, printed over its own background.
void DigitalVuMeterScreen::drawDb(DbReadout& r, const BarPhysicsState& state, uint16_t y, uint16_t barHeight) {
    uint32_t now = millis();
    if (r.drawnTenths != INT32_MIN && now - r.lastMs < kDbRefreshMs) return;
    r.lastMs = now;

    float   db     = kFloorDb * (1.0f - state.decay);
    int32_t tenths = (state.decay < 0.005f) ? INT32_MIN + 1 : static_cast<int32_t>(lroundf(db * 10.0f));
    if (tenths == r.drawnTenths) return;
    r.drawnTenths = tenths;

    char text[8];
    if (tenths == INT32_MIN + 1) {
        snprintf(text, sizeof(text), "  ---");
    } else {
        int32_t a = tenths < 0 ? -tenths : tenths;
        snprintf(text, sizeof(text), "%5s", "");
        char num[8];
        snprintf(num, sizeof(num), "%s%ld.%ld", tenths < 0 ? "-" : "", static_cast<long>(a / 10), static_cast<long>(a % 10));
        snprintf(text, sizeof(text), "%5s", num);
    }
    Arduino_GFX* gfx = Renderer::instance().gfx();
    gfx->setTextSize(kTextSize);
    gfx->setTextColor(kTextColor, COLOR_BLACK);
    gfx->setCursor(kDbX, y + (barHeight - 8 * kTextSize) / 2);
    gfx->print(text);
}

void DigitalVuMeterScreen::render() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    int16_t leftWave[WAVEFORM_SAMPLES];
    int16_t rightWave[WAVEFORM_SAMPLES];
    AudioEngine::instance().getWaveform(leftWave, rightWave, WAVEFORM_SAMPLES);

    if (DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO) {
        constexpr uint16_t barHeight = 55;
        constexpr uint16_t topY      = 10;
        constexpr uint16_t bottomY   = SCREEN_HEIGHT - 10 - barHeight;

        float levelL = AudioLevel::dbNormalize(AudioLevel::computeRms(leftWave, WAVEFORM_SAMPLES));
        float levelR = AudioLevel::dbNormalize(AudioLevel::computeRms(rightWave, WAVEFORM_SAMPLES));

        processAndDrawBar(m_left,  levelL, topY,    barHeight, kMaxBarWidth);
        processAndDrawBar(m_right, levelR, bottomY, barHeight, kMaxBarWidth);
        drawDb(m_dbL, m_left,  topY,    barHeight);
        drawDb(m_dbR, m_right, bottomY, barHeight);
    } else {
        int16_t monoWave[WAVEFORM_SAMPLES];
        for (size_t i = 0; i < WAVEFORM_SAMPLES; ++i) {
            // Average, not sum -- same mono convention as every other
            // screen: a mono-duplicated stereo source reads at the same
            // level as either channel alone, not double.
            monoWave[i] = static_cast<int16_t>((leftWave[i] + rightWave[i]) / 2);
        }

        float levelMono = AudioLevel::dbNormalize(AudioLevel::computeRms(monoWave, WAVEFORM_SAMPLES));

        constexpr uint16_t barHeight = SCREEN_HEIGHT - 20;
        constexpr uint16_t y         = 10;

        processAndDrawBar(m_mono, levelMono, y, barHeight, kMaxBarWidth);
        drawDb(m_dbM, m_mono, y, barHeight);
    }
}

void DigitalVuMeterScreen::processAndDrawBar(BarPhysicsState& state, float val, uint16_t y, uint16_t barHeight, uint16_t maxBarWidth) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    // 1. Fire the centralized physics calculation pass
    BarPhysics::instance().updateState(state, val, maxBarWidth, millis());

    // 2. Extract clean, integer-synchronized drawable boundaries
    uint16_t w     = static_cast<uint16_t>(state.decay * maxBarWidth);
    // A bar never goes fully dark: at silence it keeps a 1 px floor in its
    // colour -- a short vertical line at the left of each meter.
    if (w < 1) w = 1;
    uint16_t peakW = static_cast<uint16_t>(state.peak);
    if (peakW > maxBarWidth) peakW = maxBarWidth; // defensive: stay inside the meter

    updateBarRow(y, barHeight, state.prevUnits, w, maxBarWidth);

    bool peaksOn = DisplaySettings::instance().getPeaksEnabled();

    // Peak marker: touched only when it moved -- same reasoning as
    // SpectrumScreen::processAndDrawBar() (the peak is never left of the
    // bar's end, so the bar update can't paint over an unmoved marker).
    if (peaksOn && peakW != state.prevPeakUnits) {
        uint16_t oldPeak = state.prevPeakUnits;
        // The marker sits at index peak, just past the bar's last column
        // (bar = indices 0..w-1). Erase it unless the bar grew over it.
        if (oldPeak >= w && oldPeak > 0) {
            gfx->fillRect(kBarX + oldPeak, y, 1, barHeight, COLOR_BLACK);
        }
        if (peakW > 0) {
            gfx->fillRect(kBarX + peakW, y, 1, barHeight, COLOR_WHITE);
        }
    }

    // Tracked even while peaks are disabled -- see SpectrumScreen.
    state.prevUnits      = w;
    state.prevPeakUnits  = peakW;
}

void DigitalVuMeterScreen::fillGradientRangeHorizontal(uint16_t y, uint16_t barHeight, uint16_t loWidth, uint16_t hiWidth, uint16_t maxBarWidth) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx || hiWidth <= loWidth) return;

    // This screen has no frequency axis to index into (just one overall-
    // level bar per channel), so PER_BAR_FREQUENCY presets fall back to
    // the same level-based banded gradient as SMOOTH -- the closest
    // sensible behavior for a meter that isn't a spectrum display.
    GradientStyle style = ColorTheme::instance().getStyle();
    if (style == GradientStyle::SMOOTH || style == GradientStyle::PER_BAR_FREQUENCY) {
        uint16_t bandWidth = max(static_cast<uint16_t>(1),
                                  static_cast<uint16_t>(maxBarWidth / GRADIENT_BAND_COUNT));
        uint16_t bandStart = (loWidth / bandWidth) * bandWidth;

        for (uint16_t bandLo = bandStart; bandLo < hiWidth; bandLo += bandWidth) {
            uint16_t bandHi = min(static_cast<uint16_t>(bandLo + bandWidth), hiWidth);
            if (bandHi <= loWidth) continue;

            uint16_t clippedLo = max(bandLo, loWidth);
            float t = static_cast<float>(bandLo) / static_cast<float>(maxBarWidth);
            uint16_t color = ColorTheme::instance().interpolatedColor(t);

            gfx->fillRect(kBarX + clippedLo, y, bandHi - clippedLo, barHeight, color);
        }
        return;
    }

    uint16_t yellowStart = static_cast<uint16_t>(maxBarWidth * BAR_COLOR_YELLOW_THRESHOLD);
    uint16_t redStart    = static_cast<uint16_t>(maxBarWidth * BAR_COLOR_RED_THRESHOLD);

    uint16_t greenLo = loWidth;
    uint16_t greenHi = min(hiWidth, yellowStart);
    if (greenHi > greenLo) {
        gfx->fillRect(kBarX + greenLo, y, greenHi - greenLo, barHeight, ColorTheme::instance().lowColor());
    }

    uint16_t yellowLo = max(loWidth, yellowStart);
    uint16_t yellowHi = min(hiWidth, redStart);
    if (yellowHi > yellowLo) {
        gfx->fillRect(kBarX + yellowLo, y, yellowHi - yellowLo, barHeight, ColorTheme::instance().midColor());
    }

    uint16_t redLo = max(loWidth, redStart);
    if (hiWidth > redLo) {
        gfx->fillRect(kBarX + redLo, y, hiWidth - redLo, barHeight, ColorTheme::instance().highColor());
    }
}

void DigitalVuMeterScreen::updateBarRow(uint16_t y, uint16_t barHeight, uint16_t oldWidth, uint16_t newWidth, uint16_t maxBarWidth) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    if (newWidth > oldWidth) {
        uint16_t repaintFloor = (oldWidth > 0) ? (oldWidth - 1) : 0;
        fillGradientRangeHorizontal(y, barHeight, repaintFloor, newWidth, maxBarWidth);
    } else if (newWidth < oldWidth) {
        gfx->fillRect(kBarX + newWidth, y, oldWidth - newWidth, barHeight, COLOR_BLACK);
    }
}
