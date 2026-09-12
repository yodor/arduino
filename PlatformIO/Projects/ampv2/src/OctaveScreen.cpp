#include "OctaveScreen.hpp"
#include "Renderer.hpp"
#include "AudioEngine.hpp"
#include "AudioLevel.hpp"
#include "DisplaySettings.hpp"
#include "ColorTheme.hpp"
#include "BarPhysics.hpp"
#include <cmath>

namespace {
constexpr uint16_t kBarGapX = 1; // Dropped to 1 pixel to save spacing room for 31 bars
// Math: (428 - (32 * 1)) / 31 = (396) / 31 = ~12 pixels wide per bar!
constexpr uint16_t kBarWidth = (SCREEN_WIDTH - (OCTAVE_BAND_COUNT + 1) * kBarGapX) / OCTAVE_BAND_COUNT;
}

void OctaveScreen::resetPrevHeights(BarPhysicsState* state) {
    for (size_t i = 0; i < OCTAVE_BAND_COUNT; ++i) {
        state[i].prevUnits     = 0;
        state[i].prevPeakUnits = 0;
    }
}

void OctaveScreen::onEnter() {
    Renderer::instance().clear();
    // Deliberately not resetting decay/peak/peakTimer -- same reasoning as
    // BarSpectrumScreen: audio-reactive smoothing carries over continuously
    // across screen switches and channel-mode toggles.
    resetPrevHeights(m_left);
    resetPrevHeights(m_right);
    resetPrevHeights(m_mono);
}

void OctaveScreen::render() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    float leftBars[OCTAVE_BAND_COUNT];
    float rightBars[OCTAVE_BAND_COUNT];
    AudioEngine::instance().getOctaveBars(leftBars, rightBars);

    uint32_t now = millis();

    if (DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO) {
        constexpr uint16_t baseline    = SCREEN_HEIGHT / 2;
        constexpr uint16_t margin      = 5;
        constexpr uint16_t maxBarHeight = baseline - margin;

        for (size_t i = 0; i < OCTAVE_BAND_COUNT; ++i) {
            uint16_t x = kBarGapX + i * (kBarWidth + kBarGapX);

            processAndDrawBar(m_left[i],  i, AudioLevel::dbNormalize(leftBars[i]),  x, kBarWidth, maxBarHeight, baseline, true,  now);
            processAndDrawBar(m_right[i], i, AudioLevel::dbNormalize(rightBars[i]), x, kBarWidth, maxBarHeight, baseline, false, now);
        }
    } else { // MONO
        constexpr uint16_t baseline    = SCREEN_HEIGHT;
        constexpr uint16_t maxBarHeight = SCREEN_HEIGHT - 10;

        for (size_t i = 0; i < OCTAVE_BAND_COUNT; ++i) {
            float monoMag = (leftBars[i] + rightBars[i]) * 0.5f;
            uint16_t x = kBarGapX + i * (kBarWidth + kBarGapX);

            processAndDrawBar(m_mono[i], i, AudioLevel::dbNormalize(monoMag), x, kBarWidth, maxBarHeight, baseline, true, now);
        }
    }
}

void OctaveScreen::processAndDrawBar(BarPhysicsState& state, size_t i, float val, uint16_t x, uint16_t barWidth, uint16_t maxBarHeight, uint16_t baseline, bool growUp, uint32_t now) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    // 1. Centralized execution loop computes ballistics natively
    BarPhysics::instance().updateState(state, val, maxBarHeight, now);

    // 2. Extract perfectly synchronized integer coordinates
    uint16_t h     = static_cast<uint16_t>(state.decay * maxBarHeight);
    uint16_t peakH = static_cast<uint16_t>(state.peak);

    // ==========================================
    // BI-DIRECTIONAL ZERO-ARTIFACT ORDER OF OPERATIONS
    // ==========================================
    if (growUp) {
        updateBarColumnUp(x, barWidth, state.prevUnits, h, maxBarHeight, baseline, i);

        if (state.prevPeakUnits > h) {
            gfx->drawFastHLine(x, baseline - state.prevPeakUnits, barWidth, COLOR_BLACK);
        }
        
        if (peakH >= h && peakH > 0) {
            gfx->drawFastHLine(x, baseline - peakH, barWidth, COLOR_WHITE);
        }
    } else { // Downward vector drawing loop pass
        updateBarColumnDown(x, barWidth, state.prevUnits, h, maxBarHeight, baseline, i);

        if (state.prevPeakUnits > h) {
            gfx->drawFastHLine(x, baseline + state.prevPeakUnits, barWidth, COLOR_BLACK);
        }
        
        if (peakH >= h && peakH > 0) {
            gfx->drawFastHLine(x, baseline + peakH, barWidth, COLOR_WHITE);
        }
    }

    // 3. Cache parameters
    state.prevUnits     = h;
    state.prevPeakUnits = peakH;
}

void OctaveScreen::fillGradientRangeUp(uint16_t x, uint16_t barWidth, uint16_t loHeight, uint16_t hiHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx || hiHeight <= loHeight) return;

    ColorTheme& theme = ColorTheme::instance();
    GradientStyle style = theme.getStyle();

    if (style == GradientStyle::PER_BAR_FREQUENCY) {
        uint16_t color = theme.colorForBarIndex(barIndex, OCTAVE_BAND_COUNT);
        gfx->fillRect(x, baseline - hiHeight, barWidth, hiHeight - loHeight, color);
        return;
    }

    if (style == GradientStyle::SMOOTH) {
        uint16_t bandHeight = max(static_cast<uint16_t>(1), static_cast<uint16_t>(maxBarHeight / GRADIENT_BAND_COUNT));
        uint16_t bandStart = (loHeight / bandHeight) * bandHeight;
        for (uint16_t bandLo = bandStart; bandLo < hiHeight; bandLo += bandHeight) {
            uint16_t bandHi = min(static_cast<uint16_t>(bandLo + bandHeight), hiHeight);
            if (bandHi <= loHeight) continue;
            uint16_t clippedLo = max(bandLo, loHeight);
            float t = static_cast<float>(bandLo) / static_cast<float>(maxBarHeight);
            uint16_t color = theme.interpolatedColor(t);
            gfx->fillRect(x, baseline - bandHi, barWidth, bandHi - clippedLo, color);
        }
        return;
    }

    // FLAT_ZONES
    uint16_t yellowStart = static_cast<uint16_t>(maxBarHeight * BAR_COLOR_YELLOW_THRESHOLD);
    uint16_t redStart    = static_cast<uint16_t>(maxBarHeight * BAR_COLOR_RED_THRESHOLD);

    uint16_t greenLo = loHeight;
    uint16_t greenHi = min(hiHeight, yellowStart);
    if (greenHi > greenLo) gfx->fillRect(x, baseline - greenHi, barWidth, greenHi - greenLo, theme.lowColor());

    uint16_t yellowLo = max(loHeight, yellowStart);
    uint16_t yellowHi = min(hiHeight, redStart);
    if (yellowHi > yellowLo) gfx->fillRect(x, baseline - yellowHi, barWidth, yellowHi - yellowLo, theme.midColor());

    uint16_t redLo = max(loHeight, redStart);
    if (hiHeight > redLo) gfx->fillRect(x, baseline - hiHeight, barWidth, hiHeight - redLo, theme.highColor());
}

void OctaveScreen::fillGradientRangeDown(uint16_t x, uint16_t barWidth, uint16_t loHeight, uint16_t hiHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx || hiHeight <= loHeight) return;

    ColorTheme& theme = ColorTheme::instance();
    GradientStyle style = theme.getStyle();

    if (style == GradientStyle::PER_BAR_FREQUENCY) {
        uint16_t color = theme.colorForBarIndex(barIndex, OCTAVE_BAND_COUNT);
        gfx->fillRect(x, baseline + loHeight, barWidth, hiHeight - loHeight, color);
        return;
    }

    if (style == GradientStyle::SMOOTH) {
        uint16_t bandHeight = max(static_cast<uint16_t>(1), static_cast<uint16_t>(maxBarHeight / GRADIENT_BAND_COUNT));
        uint16_t bandStart = (loHeight / bandHeight) * bandHeight;
        for (uint16_t bandLo = bandStart; bandLo < hiHeight; bandLo += bandHeight) {
            uint16_t bandHi = min(static_cast<uint16_t>(bandLo + bandHeight), hiHeight);
            if (bandHi <= loHeight) continue;
            uint16_t clippedLo = max(bandLo, loHeight);
            float t = static_cast<float>(bandLo) / static_cast<float>(maxBarHeight);
            uint16_t color = theme.interpolatedColor(t);
            gfx->fillRect(x, baseline + clippedLo, barWidth, bandHi - clippedLo, color);
        }
        return;
    }

    // FLAT_ZONES
    uint16_t yellowStart = static_cast<uint16_t>(maxBarHeight * BAR_COLOR_YELLOW_THRESHOLD);
    uint16_t redStart    = static_cast<uint16_t>(maxBarHeight * BAR_COLOR_RED_THRESHOLD);

    uint16_t greenLo = loHeight;
    uint16_t greenHi = min(hiHeight, yellowStart);
    if (greenHi > greenLo) gfx->fillRect(x, baseline + greenLo, barWidth, greenHi - greenLo, theme.lowColor());

    uint16_t yellowLo = max(loHeight, yellowStart);
    uint16_t yellowHi = min(hiHeight, redStart);
    if (yellowHi > yellowLo) gfx->fillRect(x, baseline + yellowLo, barWidth, yellowHi - yellowLo, theme.midColor());

    uint16_t redLo = max(loHeight, redStart);
    if (hiHeight > redLo) gfx->fillRect(x, baseline + redLo, barWidth, hiHeight - redLo, theme.highColor());
}

void OctaveScreen::updateBarColumnUp(uint16_t x, uint16_t barWidth, uint16_t oldHeight, uint16_t newHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    if (newHeight > oldHeight) {
        uint16_t repaintFloor = (oldHeight > 0) ? (oldHeight - 1) : 0;
        fillGradientRangeUp(x, barWidth, repaintFloor, newHeight, maxBarHeight, baseline, barIndex);
    } else if (newHeight < oldHeight) {
        gfx->fillRect(x, baseline - oldHeight, barWidth, oldHeight - newHeight, COLOR_BLACK);
    }
}

void OctaveScreen::updateBarColumnDown(uint16_t x, uint16_t barWidth, uint16_t oldHeight, uint16_t newHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    if (newHeight > oldHeight) {
        uint16_t repaintFloor = (oldHeight > 0) ? (oldHeight - 1) : 0;
        fillGradientRangeDown(x, barWidth, repaintFloor, newHeight, maxBarHeight, baseline, barIndex);
    } else if (newHeight < oldHeight) {
        gfx->fillRect(x, baseline + newHeight, barWidth, oldHeight - newHeight, COLOR_BLACK);
    }
}