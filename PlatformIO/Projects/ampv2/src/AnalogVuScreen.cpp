#include "AnalogVuScreen.hpp"
#include "Renderer.hpp"
#include "AudioEngine.hpp"
#include "DisplaySettings.hpp"
#include "AudioLevel.hpp"
#include <cmath>

namespace {
constexpr float    kSweepDeg      = 45.0f; // needle swings -45deg (quiet) to +45deg (loud) from vertical
constexpr int16_t  kScaleMarginPx = 12;    // gap between needle tip radius and the static scale arc's radius
}

void AnalogVuScreen::VuBallistics::update(float target, uint32_t now) {
    constexpr float kTauSeconds = 0.3f / 4.605f; // ~0.0651s baseline

    float dtSec = (now - lastUpdateMs) / 1000.0f;
    lastUpdateMs = now;

    float alpha = 1.0f - expf(-dtSec / kTauSeconds);
    value += alpha * (target - value);
}

void AnalogVuScreen::onEnter() {
    Renderer::instance().clear();

    if (DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO) {
        m_leftGauge.pivotX = SCREEN_WIDTH / 4;
        m_leftGauge.pivotY = SCREEN_HEIGHT - 8;
        m_leftGauge.radius = 90;

        m_rightGauge.pivotX = (SCREEN_WIDTH * 3) / 4;
        m_rightGauge.pivotY = SCREEN_HEIGHT - 8;
        m_rightGauge.radius = 90;

        drawStaticScale(m_leftGauge);
        drawStaticScale(m_rightGauge);
    } else {
        m_leftGauge.pivotX = SCREEN_WIDTH / 2;
        m_leftGauge.pivotY = SCREEN_HEIGHT - 8;
        m_leftGauge.radius = 110;

        drawStaticScale(m_leftGauge);
    }

    m_leftGauge.needleDrawn  = false;
    m_leftGauge.ballistics   = VuBallistics{};
    m_rightGauge.needleDrawn = false;
    m_rightGauge.ballistics  = VuBallistics{};
}

void AnalogVuScreen::drawStaticScale(const Gauge& gauge) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    int16_t scaleRadius = gauge.radius + kScaleMarginPx;
    constexpr int kArcSteps = 24;
    int16_t prevX = 0, prevY = 0;
    
    for (int step = 0; step <= kArcSteps; ++step) {
        float angleDeg = -kSweepDeg + (2.0f * kSweepDeg) * step / kArcSteps;
        float rad = angleDeg * static_cast<float>(M_PI) / 180.0f;
        int16_t x = gauge.pivotX + static_cast<int16_t>(scaleRadius * sinf(rad));
        int16_t y = gauge.pivotY - static_cast<int16_t>(scaleRadius * cosf(rad));
        if (step > 0) {
            gfx->drawLine(prevX, prevY, x, y, COLOR_DIM_GREEN);
        }
        prevX = x;
        prevY = y;
    }

    auto tickAt = [&](float angleDeg, uint16_t color, int16_t tickLen) {
        float rad = angleDeg * static_cast<float>(M_PI) / 180.0f;
        int16_t xOuter = gauge.pivotX + static_cast<int16_t>(scaleRadius * sinf(rad));
        int16_t yOuter = gauge.pivotY - static_cast<int16_t>(scaleRadius * cosf(rad));
        int16_t xInner = gauge.pivotX + static_cast<int16_t>((scaleRadius - tickLen) * sinf(rad));
        int16_t yInner = gauge.pivotY - static_cast<int16_t>((scaleRadius - tickLen) * cosf(rad));
        gfx->drawLine(xInner, yInner, xOuter, yOuter, color);
    };

    tickAt(0.0f, COLOR_WHITE, 8);             
    tickAt(kSweepDeg * 0.7f, COLOR_YELLOW, 8); 
    tickAt(kSweepDeg * 0.95f, COLOR_RED, 8);   
}

// OPTIMIZED: Synchronized needle refresh layer to prevent scale tearing
void AnalogVuScreen::updateNeedle(Gauge& gauge, float magnitude, uint32_t now) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    gauge.ballistics.update(magnitude, now);
    float angleDeg = -kSweepDeg + gauge.ballistics.value * (2.0f * kSweepDeg);

    auto calculateTip = [&](float angle, int16_t& outX, int16_t& outY) {
        float rad = angle * static_cast<float>(M_PI) / 180.0f;
        outX = gauge.pivotX + static_cast<int16_t>(gauge.radius * sinf(rad));
        outY = gauge.pivotY - static_cast<int16_t>(gauge.radius * cosf(rad));
    };

    // 1. ERASE STEP: Wipe the old line trace first
    if (gauge.needleDrawn) {
        int16_t oldX, oldY;
        calculateTip(gauge.prevAngleDeg, oldX, oldY);
        gfx->drawLine(gauge.pivotX, gauge.pivotY, oldX, oldY, COLOR_BLACK);
    }

    // 2. REDRAW STEP: Draw the updated vector cleanly into memory
    int16_t newX, newY;
    calculateTip(angleDeg, newX, newY);
    gfx->drawLine(gauge.pivotX, gauge.pivotY, newX, newY, COLOR_WHITE);

    // 3. RE-PROTECT SCALE: Redrawing ticks keeps the scale arc whole if a needle passes it
    drawStaticScale(gauge);

    gauge.prevAngleDeg = angleDeg;
    gauge.needleDrawn  = true;
}

void AnalogVuScreen::render() {
    int16_t leftWave[WAVEFORM_SAMPLES];
    int16_t rightWave[WAVEFORM_SAMPLES];
    AudioEngine::instance().getWaveform(leftWave, rightWave, WAVEFORM_SAMPLES);

    uint32_t now = millis();

    if (DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO) {
        // UNIFIED: Pointing to your clean dynamic AudioEngine decibel normalization mapper utility function
        float levelL = AudioLevel::dbNormalize(AudioLevel::computeRms(leftWave, WAVEFORM_SAMPLES));
        float levelR = AudioLevel::dbNormalize(AudioLevel::computeRms(rightWave, WAVEFORM_SAMPLES));

        updateNeedle(m_leftGauge, levelL, now);
        updateNeedle(m_rightGauge, levelR, now);
    } else {
        int16_t monoWave[WAVEFORM_SAMPLES];
        for (size_t i = 0; i < WAVEFORM_SAMPLES; ++i) {
            monoWave[i] = static_cast<int16_t>((leftWave[i] + rightWave[i]) / 2);
        }

        float levelMono = AudioLevel::dbNormalize(AudioLevel::computeRms(monoWave, WAVEFORM_SAMPLES));
        updateNeedle(m_leftGauge, levelMono, now); 
    }
}