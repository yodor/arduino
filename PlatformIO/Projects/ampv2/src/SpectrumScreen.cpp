#include "SpectrumScreen.hpp"
#include "Renderer.hpp"
#include "AudioEngine.hpp"
#include "AudioLevel.hpp"
#include "DisplaySettings.hpp"
#include "ColorTheme.hpp"
#include "BarPhysics.hpp"
#include <cmath>

namespace {
// Computes bar width for `totalBars` bars (with `totalBars+1` gaps total --
// one leading, one trailing, one between each pair) fit into `screenWidth`
// pixels, never returning less than 1. Uses signed arithmetic internally
// so a pathologically large totalBars/gap combination clamps safely
// instead of silently underflowing the unsigned subtraction this used to
// be computed with directly (an underflow there wraps to a huge positive
// number rather than 0, corrupting the whole render rather than just
// looking cramped).
uint16_t computeBarWidth(uint16_t screenWidth, uint16_t totalBars, uint16_t gap) {
    if (totalBars == 0) return 1;
    int32_t available = static_cast<int32_t>(screenWidth) - static_cast<int32_t>(totalBars + 1) * gap;
    int32_t width     = available / static_cast<int32_t>(totalBars);
    return (width < 1) ? 1 : static_cast<uint16_t>(width);
}
} // namespace

void SpectrumScreen::resetPrevHeights(BarPhysicsState* state) {
    for (size_t i = 0; i < MAX_BAND_COUNT; ++i) {
        state[i].prevUnits     = 0;
        state[i].prevPeakUnits = 0;
    }
}

void SpectrumScreen::onEnter() {
    Renderer::instance().clear();

    // Reset only the "what's currently drawn" tracking to match the now-
    // blank screen -- deliberately NOT resetting decay/peak/peakTimer, so
    // the audio-reactive smoothing state carries over continuously across
    // screen switches and channel-mode/layout toggles, rather than
    // jumping back to zero every time.
    resetPrevHeights(m_left);
    resetPrevHeights(m_right);
    resetPrevHeights(m_mono);
}

void SpectrumScreen::render() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    size_t totalBars = AudioEngine::instance().getSpectrumBandCount();
    if (totalBars == 0 || totalBars > MAX_BAND_COUNT) return; // defensive -- shouldn't happen, but never index past our arrays

    float leftBars[MAX_BAND_COUNT];
    float rightBars[MAX_BAND_COUNT];
    AudioEngine::instance().getSpectrumBars(leftBars, rightBars);

    uint32_t now = millis();

    // --- Diagnostic: which bin is currently loudest, once/sec ---
    static uint32_t s_lastDebugMs    = 0;
    static float    s_debugMaxMag    = 0.0f;
    static size_t   s_debugMaxBarIdx = 0;
    static bool     s_debugMaxIsLeft = true;

    for (size_t i = 0; i < totalBars; ++i) {
        if (leftBars[i] > s_debugMaxMag) {
            s_debugMaxMag = leftBars[i]; s_debugMaxBarIdx = i; s_debugMaxIsLeft = true;
        }
        if (rightBars[i] > s_debugMaxMag) {
            s_debugMaxMag = rightBars[i]; s_debugMaxBarIdx = i; s_debugMaxIsLeft = false;
        }
    }

    // Full mode's whole point is maximum density -- zero gap between
    // bars, unlike Doubling/Fixed Bands which keep a visible 1-2px
    // separation for readability at their much lower band counts.
    bool isFullMode = (AudioEngine::instance().getSpectrumBandLayoutMode() == SpectrumPipeline::BandLayoutMode::FULL);

    if (DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO) {
        if (DisplaySettings::instance().getStereoLayout() == StereoLayout::SIDE_BY_SIDE) {
            // Original BarSpectrumScreen layout: L and R groups side by
            // side across the width, both growing up from the bottom.
            uint16_t gap          = isFullMode ? 0 : 2;
            uint16_t numBarsTotal = static_cast<uint16_t>(totalBars) * 2;
            uint16_t barWidth     = computeBarWidth(SCREEN_WIDTH, numBarsTotal, gap);
            uint16_t maxBarHeight = SCREEN_HEIGHT - 10;
            constexpr uint16_t baseline = SCREEN_HEIGHT;

            bool mirrorLeft = DisplaySettings::instance().getMirrorLeftBars();

            for (size_t i = 0; i < totalBars; ++i) {
                size_t leftDrawIndex = mirrorLeft ? (totalBars - 1 - i) : i;

                uint16_t xL = gap + static_cast<uint16_t>(leftDrawIndex) * (barWidth + gap);
                uint16_t xR = gap + static_cast<uint16_t>(i + totalBars) * (barWidth + gap);

                processAndDrawBar(m_left[i],  i, totalBars, AudioLevel::dbNormalize(leftBars[i]),  xL, barWidth, maxBarHeight, baseline, true, now);
                processAndDrawBar(m_right[i], i, totalBars, AudioLevel::dbNormalize(rightBars[i]), xR, barWidth, maxBarHeight, baseline, true, now);
            }
        } else { // TOP_BOTTOM
            // Original OctaveScreen layout: L grows up, R grows down, both
            // meeting at a shared centerline.
            uint16_t gap      = isFullMode ? 0 : 1;
            uint16_t barWidth = computeBarWidth(SCREEN_WIDTH, static_cast<uint16_t>(totalBars), gap);
            constexpr uint16_t baseline    = SCREEN_HEIGHT / 2;
            constexpr uint16_t margin      = 5;
            constexpr uint16_t maxBarHeight = baseline - margin;

            for (size_t i = 0; i < totalBars; ++i) {
                uint16_t x = gap + static_cast<uint16_t>(i) * (barWidth + gap);

                processAndDrawBar(m_left[i],  i, totalBars, AudioLevel::dbNormalize(leftBars[i]),  x, barWidth, maxBarHeight, baseline, true,  now);
                processAndDrawBar(m_right[i], i, totalBars, AudioLevel::dbNormalize(rightBars[i]), x, barWidth, maxBarHeight, baseline, false, now);
            }
        }
    } else { // MONO -- identical regardless of Stereo Layout setting, matching how both original screens' mono fallback was already the same shape
        uint16_t gap      = isFullMode ? 0 : 1;
        uint16_t barWidth = computeBarWidth(SCREEN_WIDTH, static_cast<uint16_t>(totalBars), gap);
        uint16_t maxBarHeight = SCREEN_HEIGHT - 10;
        constexpr uint16_t baseline = SCREEN_HEIGHT;

        for (size_t i = 0; i < totalBars; ++i) {
            // Average, not sum -- same mono convention as every other
            // screen: a mono-duplicated stereo source reads at the same
            // level as either channel alone, not double.
            float monoMag = (leftBars[i] + rightBars[i]) * 0.5f;
            uint16_t x = gap + static_cast<uint16_t>(i) * (barWidth + gap);

            processAndDrawBar(m_mono[i], i, totalBars, AudioLevel::dbNormalize(monoMag), x, barWidth, maxBarHeight, baseline, true, now);
        }
    }

    uint32_t nowMs = millis();
    if (nowMs - s_lastDebugMs >= 1000) {
        s_lastDebugMs = nowMs;

        float db = (s_debugMaxMag > 0.0001f) ? 20.0f * log10f(s_debugMaxMag) : -100.0f;

        Serial.print("[Spectrum] loudest bin -> band ");
        Serial.print(static_cast<int>(s_debugMaxBarIdx));
        Serial.print(s_debugMaxIsLeft ? " (L)" : " (R)");
        Serial.print(" | raw mag=");
        Serial.print(s_debugMaxMag, 4);
        Serial.print(" | dBFS=");
        Serial.println(db, 2);

        s_debugMaxMag = 0.0f; // reset for the next reporting window
    }
}

void SpectrumScreen::processAndDrawBar(BarPhysicsState& state, size_t i, size_t totalBars, float val, uint16_t x, uint16_t barWidth, uint16_t maxBarHeight, uint16_t baseline, bool growUp, uint32_t now) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    BarPhysics::instance().updateState(state, val, maxBarHeight, now);

    uint16_t h     = static_cast<uint16_t>(state.decay * maxBarHeight);
    uint16_t peakH = static_cast<uint16_t>(state.peak);

    if (growUp) {
        updateBarColumnUp(x, barWidth, state.prevUnits, h, maxBarHeight, baseline, i, totalBars);

        if (state.prevPeakUnits > h) {
            gfx->drawFastHLine(x, baseline - state.prevPeakUnits, barWidth, COLOR_BLACK);
        }
        if (peakH >= h && peakH > 0) {
            gfx->drawFastHLine(x, baseline - peakH, barWidth, COLOR_WHITE);
        }
    } else {
        updateBarColumnDown(x, barWidth, state.prevUnits, h, maxBarHeight, baseline, i, totalBars);

        if (state.prevPeakUnits > h) {
            gfx->drawFastHLine(x, baseline + state.prevPeakUnits, barWidth, COLOR_BLACK);
        }
        if (peakH >= h && peakH > 0) {
            gfx->drawFastHLine(x, baseline + peakH, barWidth, COLOR_WHITE);
        }
    }

    state.prevUnits     = h;
    state.prevPeakUnits = peakH;
}

void SpectrumScreen::fillGradientRangeUp(uint16_t x, uint16_t barWidth, uint16_t loHeight, uint16_t hiHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex, size_t totalBars) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx || hiHeight <= loHeight) return;

    ColorTheme& theme = ColorTheme::instance();
    GradientStyle style = theme.getStyle();

    if (style == GradientStyle::PER_BAR_FREQUENCY) {
        uint16_t color = theme.colorForBarIndex(barIndex, totalBars);
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

void SpectrumScreen::fillGradientRangeDown(uint16_t x, uint16_t barWidth, uint16_t loHeight, uint16_t hiHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex, size_t totalBars) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx || hiHeight <= loHeight) return;

    ColorTheme& theme = ColorTheme::instance();
    GradientStyle style = theme.getStyle();

    if (style == GradientStyle::PER_BAR_FREQUENCY) {
        uint16_t color = theme.colorForBarIndex(barIndex, totalBars);
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

void SpectrumScreen::updateBarColumnUp(uint16_t x, uint16_t barWidth, uint16_t oldHeight, uint16_t newHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex, size_t totalBars) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    if (newHeight > oldHeight) {
        uint16_t repaintFloor = (oldHeight > 0) ? (oldHeight - 1) : 0;
        fillGradientRangeUp(x, barWidth, repaintFloor, newHeight, maxBarHeight, baseline, barIndex, totalBars);
    } else if (newHeight < oldHeight) {
        gfx->fillRect(x, baseline - oldHeight, barWidth, oldHeight - newHeight, COLOR_BLACK);
    }
}

void SpectrumScreen::updateBarColumnDown(uint16_t x, uint16_t barWidth, uint16_t oldHeight, uint16_t newHeight, uint16_t maxBarHeight, uint16_t baseline, size_t barIndex, size_t totalBars) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    if (newHeight > oldHeight) {
        uint16_t repaintFloor = (oldHeight > 0) ? (oldHeight - 1) : 0;
        fillGradientRangeDown(x, barWidth, repaintFloor, newHeight, maxBarHeight, baseline, barIndex, totalBars);
    } else if (newHeight < oldHeight) {
        gfx->fillRect(x, baseline + newHeight, barWidth, oldHeight - newHeight, COLOR_BLACK);
    }
}