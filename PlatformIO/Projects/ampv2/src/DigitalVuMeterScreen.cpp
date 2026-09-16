#include "DigitalVuMeterScreen.hpp"
#include "Renderer.hpp"
#include "AudioEngine.hpp"
#include "DisplaySettings.hpp"
#include "AudioLevel.hpp"
#include "ColorTheme.hpp"
#include "BarPhysics.hpp"

namespace {
constexpr uint16_t kBarX        = 2;
constexpr uint16_t kMaxBarWidth = SCREEN_WIDTH - 2 * kBarX;
}

void DigitalVuMeterScreen::resetBarState(BarPhysicsState& state) {
    state.prevUnits     = 0;
    state.prevPeakUnits = 0;
}

void DigitalVuMeterScreen::onEnter() {
    Renderer::instance().clear();
    // Deliberately not resetting decay/peak/peakTimer -- same reasoning as
    // SpectrumScreen: audio-reactive smoothing carries over continuously
    // across screen switches and channel-mode toggles.
    resetBarState(m_left);
    resetBarState(m_right);
    resetBarState(m_mono);
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
    }
}

void DigitalVuMeterScreen::processAndDrawBar(BarPhysicsState& state, float val, uint16_t y, uint16_t barHeight, uint16_t maxBarWidth) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    // 1. Fire the centralized physics calculation pass
    BarPhysics::instance().updateState(state, val, maxBarWidth, millis());

    // 2. Extract clean, integer-synchronized drawable boundaries
    uint16_t w     = static_cast<uint16_t>(state.decay * maxBarWidth);
    uint16_t peakW = static_cast<uint16_t>(state.peak);

    // ==========================================
    // ZERO-ARTIFACT RENDERING LOOP
    // ==========================================
    updateBarRow(y, barHeight, state.prevUnits, w, maxBarWidth);

    if (state.prevPeakUnits > w) {
        gfx->drawFastVLine(kBarX + state.prevPeakUnits, y, barHeight, COLOR_BLACK);
    }
    
    if (peakW >= w && peakW > 0) {
        gfx->drawFastVLine(kBarX + peakW, y, barHeight, COLOR_WHITE);
    }

    // 3. Cache state track points for the next loop frame step
    state.prevUnits     = w;
    state.prevPeakUnits = peakW;
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