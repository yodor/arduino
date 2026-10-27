#include "WaveformScreen.hpp"
#include "ColorTheme.hpp"
#include "Renderer.hpp"
#include "AudioEngine.hpp"
#include "DisplaySettings.hpp"

void WaveformScreen::onEnter() {
    // Trace colours from the active theme: its first colour for the top
    // (left) trace, its last for the bottom (right), the middle mix for mono.
    // (A theme change happens in the menu, and closing it re-enters here.)
    ColorTheme& theme = ColorTheme::instance();
    m_colorTop    = theme.lowColor();
    m_colorBottom = theme.highColor();
    m_colorMono   = theme.interpolatedColor(0.5f);

    Renderer::instance().clear();
    m_waveTraceValid = false; // next render() call must redraw gridlines fresh
}

namespace {
// x position of sample i when `count` samples span the panel width.
uint16_t sampleX(size_t i, size_t count) {
    return static_cast<uint16_t>((i * (SCREEN_WIDTH - 1)) / (count - 1));
}
} // namespace

void WaveformScreen::drawWavePolylineRange(const int16_t* y, size_t count, size_t rangeStart, size_t rangeEndExclusive, uint16_t color) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx || !y || rangeEndExclusive <= rangeStart + 1) return;

    size_t i = rangeStart;
    while (i < rangeEndExclusive - 1) {
        if (y[i] == y[i + 1]) {
            size_t runEnd = i + 1;
            while (runEnd < rangeEndExclusive - 1 && y[runEnd] == y[runEnd + 1]) {
                ++runEnd;
            }

            uint16_t x0 = sampleX(i, count);
            uint16_t x1 = sampleX(runEnd, count);
            gfx->drawLine(x0, y[i], x1, y[runEnd], color);

            i = runEnd;
        } else {
            uint16_t x0 = sampleX(i, count);
            uint16_t x1 = sampleX(i + 1, count);
            gfx->drawLine(x0, y[i], x1, y[i + 1], color);
            ++i;
        }
    }
}

void WaveformScreen::render() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    int16_t leftWave[WAVEFORM_SAMPLES];
    int16_t rightWave[WAVEFORM_SAMPLES];
    // Triggered in Oscilloscope mode (a stable picture of the wave shape);
    // free-running in Track Preview, which shows an envelope, not cycles.
    bool trigger = WAVEFORM_TRIGGER &&
                   AudioEngine::instance().getWaveformWindowMode() == WaveformPipeline::WindowMode::OSCILLOSCOPE;
    AudioEngine::instance().getWaveform(leftWave, rightWave, WAVEFORM_SAMPLES, trigger);

    if (WAVEFORM_SAMPLES < 2) return;

    if (DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO) {
        renderStereo(leftWave, rightWave, WAVEFORM_SAMPLES);
    } else {
        renderMono(leftWave, rightWave, WAVEFORM_SAMPLES);
    }
}

void WaveformScreen::renderStereo(const int16_t* leftWave, const int16_t* rightWave, size_t sampleCount) {
    Arduino_GFX* gfx = Renderer::instance().gfx();

    constexpr uint16_t halfHeight = SCREEN_HEIGHT / 2;
    constexpr uint16_t leftMidY   = halfHeight / 2;
    constexpr uint16_t rightMidY  = halfHeight + halfHeight / 2;
    constexpr float ampScale = (halfHeight / 2 - 4) / 2048.0f;

    // Plain black background, no gridlines -- nothing for the trace's erase
    // pass to cut through and repair.
    if (!m_waveTraceValid) {
        gfx->fillScreen(COLOR_BLACK);
    }

    for (size_t i = 0; i < sampleCount; ++i) {
        int16_t yL = leftMidY  - static_cast<int16_t>(leftWave[i]  * ampScale);
        int16_t yR = rightMidY - static_cast<int16_t>(rightWave[i] * ampScale);

        m_curWaveYL[i] = constrain(yL, 0, static_cast<int16_t>(halfHeight - 1));
        m_curWaveYR[i] = constrain(yR, static_cast<int16_t>(halfHeight), static_cast<int16_t>(SCREEN_HEIGHT - 1));
    }

    for (size_t chunkStart = 0; chunkStart < sampleCount - 1; chunkStart += WAVE_CHUNK_SAMPLES) {
        size_t chunkEnd = chunkStart + WAVE_CHUNK_SAMPLES + 1;
        if (chunkEnd > sampleCount) chunkEnd = sampleCount;

        if (m_waveTraceValid) {
            drawWavePolylineRange(m_prevWaveYL, sampleCount, chunkStart, chunkEnd, COLOR_BLACK);
            drawWavePolylineRange(m_prevWaveYR, sampleCount, chunkStart, chunkEnd, COLOR_BLACK);
        }

        // Start one point early: that also redraws the previous chunk's last
        // segment, which this chunk's erase may have nicked where the two
        // chunks meet.
        size_t drawFrom = (chunkStart > 0) ? chunkStart - 1 : 0;
        drawWavePolylineRange(m_curWaveYL, sampleCount, drawFrom, chunkEnd, m_colorTop);
        drawWavePolylineRange(m_curWaveYR, sampleCount, drawFrom, chunkEnd, m_colorBottom);
    }

    for (size_t i = 0; i < sampleCount; ++i) {
        m_prevWaveYL[i] = m_curWaveYL[i];
        m_prevWaveYR[i] = m_curWaveYR[i];
    }

    m_waveTraceValid = true;
}

void WaveformScreen::renderMono(const int16_t* leftWave, const int16_t* rightWave, size_t sampleCount) {
    Arduino_GFX* gfx = Renderer::instance().gfx();

    constexpr uint16_t midY = SCREEN_HEIGHT / 2;
    // Nearly the full height is available for a single trace, vs. half of
    // it per channel in stereo mode.
    constexpr float ampScale = (SCREEN_HEIGHT / 2 - 4) / 2048.0f;

    if (!m_waveTraceValid) {
        gfx->fillScreen(COLOR_BLACK); // plain background, see renderStereo()
    }

    for (size_t i = 0; i < sampleCount; ++i) {
        // Average, not sum, so a mono-duplicated stereo source (L==R)
        // reads at the same amplitude as either channel alone.
        int16_t monoSample = static_cast<int16_t>((leftWave[i] + rightWave[i]) / 2);
        int16_t y = midY - static_cast<int16_t>(monoSample * ampScale);
        m_curWaveYMono[i] = constrain(y, 0, static_cast<int16_t>(SCREEN_HEIGHT - 1));
    }

    for (size_t chunkStart = 0; chunkStart < sampleCount - 1; chunkStart += WAVE_CHUNK_SAMPLES) {
        size_t chunkEnd = chunkStart + WAVE_CHUNK_SAMPLES + 1;
        if (chunkEnd > sampleCount) chunkEnd = sampleCount;

        if (m_waveTraceValid) {
            drawWavePolylineRange(m_prevWaveYMono, sampleCount, chunkStart, chunkEnd, COLOR_BLACK);
        }

        size_t drawFrom = (chunkStart > 0) ? chunkStart - 1 : 0;
        drawWavePolylineRange(m_curWaveYMono, sampleCount, drawFrom, chunkEnd, m_colorMono);
    }

    for (size_t i = 0; i < sampleCount; ++i) {
        m_prevWaveYMono[i] = m_curWaveYMono[i];
    }

    m_waveTraceValid = true;
}
