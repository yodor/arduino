#include "BarSpectrumScreen.hpp"
#include "Renderer.hpp"
#include "AudioEngine.hpp"
#include "AudioLevel.hpp"
#include "DisplaySettings.hpp"
#include "ColorTheme.hpp"
#include "BarPhysics.hpp"
#include <cmath>

void BarSpectrumScreen::resetPrevHeights(BarPhysicsState* state) {
    for (size_t i = 0; i < NUM_BARS; ++i) {
        state[i].prevUnits     = 0;
        state[i].prevPeakUnits = 0;
    }
}

void BarSpectrumScreen::onEnter() {
    Renderer::instance().clear();

    // Reset only the "what's currently drawn" tracking to match the now-
    // blank screen -- deliberately NOT resetting decay/peak/peakTimer, so
    // the audio-reactive smoothing state carries over continuously across
    // screen switches and channel-mode toggles (same as before this
    // refactor), rather than jumping back to zero every time.
    resetPrevHeights(m_left);
    resetPrevHeights(m_right);
    resetPrevHeights(m_mono);
}

void BarSpectrumScreen::render() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    float leftBars[NUM_BARS];
    float rightBars[NUM_BARS];
    AudioEngine::instance().getSpectrumBars(leftBars, rightBars);

    uint32_t now = millis();
    uint16_t maxBarHeight = SCREEN_HEIGHT - 10;

    // ==========================================
    // DIAGNOSTIC CHECKPOINT DEFINITIONS
    // ==========================================
    static uint32_t s_lastDebugMs    = 0;
    static float    s_debugMaxMag    = 0.0f;
    static size_t   s_debugMaxBarIdx = 0;   // FIXED: Added missing tracking index allocation
    static bool     s_debugMaxIsLeft = true;  // FIXED: Added missing left/right tracker status flag

    for (size_t i = 0; i < NUM_BARS; ++i) {
        if (leftBars[i] > s_debugMaxMag) {
            s_debugMaxMag = leftBars[i]; s_debugMaxBarIdx = i; s_debugMaxIsLeft = true;
        }
        if (rightBars[i] > s_debugMaxMag) {
            s_debugMaxMag = rightBars[i]; s_debugMaxBarIdx = i; s_debugMaxIsLeft = false;
        }
    }

    if (DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO) {
        uint16_t numBarsTotal = NUM_BARS * 2;
        uint16_t barWidth     = (SCREEN_WIDTH - (numBarsTotal + 1) * 2) / numBarsTotal;

        for (size_t i = 0; i < NUM_BARS; ++i) {
            size_t leftDrawIndex = MIRROR_LEFT_CHANNEL_BARS ? (NUM_BARS - 1 - i) : i;

            uint16_t xL = 2 + static_cast<uint16_t>(leftDrawIndex) * (barWidth + 2);
            uint16_t xR = 2 + (i + NUM_BARS) * (barWidth + 2);

            processAndDrawBar(m_left[i],  i, AudioLevel::dbNormalize(leftBars[i]),  xL, barWidth, maxBarHeight, now);
            processAndDrawBar(m_right[i], i, AudioLevel::dbNormalize(rightBars[i]), xR, barWidth, maxBarHeight, now);
        }
    } else { // MONO
        uint16_t barWidth = (SCREEN_WIDTH - (NUM_BARS + 1) * 2) / NUM_BARS;

        for (size_t i = 0; i < NUM_BARS; ++i) {
            float monoMag = (leftBars[i] + rightBars[i]) * 0.5f;
            uint16_t x = 2 + i * (barWidth + 2);

            processAndDrawBar(m_mono[i], i, AudioLevel::dbNormalize(monoMag), x, barWidth, maxBarHeight, now);
        }
    }

    // ==========================================
    // SYSTEM PEAK DATA LOG TRANSFER OUT
    // ==========================================
    uint32_t nowMs = millis();
    if (nowMs - s_lastDebugMs >= 1000) {
        s_lastDebugMs = nowMs;

        float db = (s_debugMaxMag > 0.0001f) ? 20.0f * log10f(s_debugMaxMag) : -100.0f;

        Serial.print("[Bars] loudest bin -> bar ");
        Serial.print(static_cast<int>(s_debugMaxBarIdx));
        Serial.print(s_debugMaxIsLeft ? " (L)" : " (R)");
        Serial.print(" | raw mag=");
        Serial.print(s_debugMaxMag, 4);
        Serial.print(" | dBFS=");
        Serial.println(db, 2);

        s_debugMaxMag = 0.0f; // reset for the next reporting window
    }
}

void BarSpectrumScreen::processAndDrawBar(BarPhysicsState& state, size_t i, float val, uint16_t x, uint16_t barWidth, uint16_t maxBarHeight, uint32_t now) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    // 1. Core ballistics handled natively by central engine
    BarPhysics::instance().updateState(state, val, maxBarHeight, now);

    // 2. Extract clean, integer-synchronized boundaries
    uint16_t h     = static_cast<uint16_t>(state.decay * maxBarHeight);
    uint16_t peakH = static_cast<uint16_t>(state.peak);

    // ==========================================
    // ZERO-ARTIFACT DIRECT DELTA RENDERING
    // ==========================================
    updateBarColumn(x, barWidth, state.prevUnits, h, maxBarHeight, i);

    if (state.prevPeakUnits > h) {
        gfx->drawFastHLine(x, SCREEN_HEIGHT - state.prevPeakUnits, barWidth, COLOR_BLACK);
    }

    if (peakH >= h && peakH > 0) {
        gfx->drawFastHLine(x, SCREEN_HEIGHT - peakH, barWidth, COLOR_WHITE);
    }

    // 3. Cache positions
    state.prevUnits     = h;
    state.prevPeakUnits = peakH;
}



void BarSpectrumScreen::fillGradientRange(uint16_t x, uint16_t barWidth, uint16_t loHeight, uint16_t hiHeight, uint16_t maxBarHeight) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx || hiHeight <= loHeight) return;

    if (ColorTheme::instance().getStyle() == GradientStyle::SMOOTH) {
        // Smooth (banded) gradient: color depends only on a band's fixed
        // position (t = bandLo/maxBarHeight), never on which call touches
        // it -- so a row painted once stays correct under the same
        // delta-redraw invariant the flat-zone themes rely on.
        uint16_t bandHeight = max(static_cast<uint16_t>(1),
                                   static_cast<uint16_t>(maxBarHeight / GRADIENT_BAND_COUNT));
        uint16_t bandStart = (loHeight / bandHeight) * bandHeight; // floor-align so repeated calls agree on band boundaries

        for (uint16_t bandLo = bandStart; bandLo < hiHeight; bandLo += bandHeight) {
            uint16_t bandHi = min(static_cast<uint16_t>(bandLo + bandHeight), hiHeight);
            if (bandHi <= loHeight) continue; // band falls entirely below the requested range

            uint16_t clippedLo = max(bandLo, loHeight);
            float t = static_cast<float>(bandLo) / static_cast<float>(maxBarHeight);
            uint16_t color = ColorTheme::instance().interpolatedColor(t);

            gfx->fillRect(x, SCREEN_HEIGHT - bandHi, barWidth, bandHi - clippedLo, color);
        }
        return;
    }

    // Flat 3-zone fill (Classic/Ocean) -- unchanged.
    uint16_t yellowStart = static_cast<uint16_t>(maxBarHeight * BAR_COLOR_YELLOW_THRESHOLD);
    uint16_t redStart    = static_cast<uint16_t>(maxBarHeight * BAR_COLOR_RED_THRESHOLD);

    uint16_t greenLo = loHeight;
    uint16_t greenHi = min(hiHeight, yellowStart);
    if (greenHi > greenLo) {
        gfx->fillRect(x, SCREEN_HEIGHT - greenHi, barWidth, greenHi - greenLo, ColorTheme::instance().lowColor());
    }

    uint16_t yellowLo = max(loHeight, yellowStart);
    uint16_t yellowHi = min(hiHeight, redStart);
    if (yellowHi > yellowLo) {
        gfx->fillRect(x, SCREEN_HEIGHT - yellowHi, barWidth, yellowHi - yellowLo, ColorTheme::instance().midColor());
    }

    uint16_t redLo = max(loHeight, redStart);
    if (hiHeight > redLo) {
        gfx->fillRect(x, SCREEN_HEIGHT - hiHeight, barWidth, hiHeight - redLo, ColorTheme::instance().highColor());
    }
}

void BarSpectrumScreen::updateBarColumn(uint16_t x, uint16_t barWidth, uint16_t oldHeight, uint16_t newHeight, uint16_t maxBarHeight, size_t barIndex) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    if (ColorTheme::instance().getStyle() == GradientStyle::PER_BAR_FREQUENCY) {
        // Color depends only on which bar this is (fixed for the life of
        // the program), never on height -- so a simple flat fill is
        // correct and sufficient, no zone/band logic needed.
        uint16_t color = ColorTheme::instance().colorForBarIndex(barIndex, NUM_BARS);

        if (newHeight > oldHeight) {
            // Same reclaim-one-row trick as the level-based path below: the
            // peak indicator's own erase step (in processAndDrawBar, run
            // just before this call) can blacken exactly the row at
            // oldHeight when the peak had pinned there, and that erase
            // happens before we know the bar is about to grow.
            uint16_t repaintFloor = (oldHeight > 0) ? (oldHeight - 1) : 0;
            gfx->fillRect(x, SCREEN_HEIGHT - newHeight, barWidth, newHeight - repaintFloor, color);
        } else if (newHeight < oldHeight) {
            gfx->fillRect(x, SCREEN_HEIGHT - oldHeight, barWidth, oldHeight - newHeight, COLOR_BLACK);
        }
        return;
    }

    // Level-based styles (FLAT_ZONES / SMOOTH) -- unchanged.
    if (newHeight > oldHeight) {
        uint16_t repaintFloor = (oldHeight > 0) ? (oldHeight - 1) : 0;
        fillGradientRange(x, barWidth, repaintFloor, newHeight, maxBarHeight);
    } else if (newHeight < oldHeight) {
        gfx->fillRect(x, SCREEN_HEIGHT - oldHeight, barWidth, oldHeight - newHeight, COLOR_BLACK);
    }
}