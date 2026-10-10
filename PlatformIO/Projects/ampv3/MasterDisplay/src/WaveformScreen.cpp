#include "WaveformScreen.hpp"
#include "Renderer.hpp"
#include "AudioEngine.hpp"
#include "DisplaySettings.hpp"

void WaveformScreen::onEnter() {
    Renderer::instance().clear();
    m_waveTraceValid = false; // next render() call must redraw gridlines fresh
}

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

            uint16_t x0 = static_cast<uint16_t>((i * (SCREEN_WIDTH - 1)) / (count - 1));
            uint16_t x1 = static_cast<uint16_t>((runEnd * (SCREEN_WIDTH - 1)) / (count - 1));
            gfx->drawLine(x0, y[i], x1, y[runEnd], color);

            i = runEnd;
        } else {
            uint16_t x0 = static_cast<uint16_t>((i * (SCREEN_WIDTH - 1)) / (count - 1));
            uint16_t x1 = static_cast<uint16_t>(((i + 1) * (SCREEN_WIDTH - 1)) / (count - 1));
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
    AudioEngine::instance().getWaveform(leftWave, rightWave, WAVEFORM_SAMPLES);

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

    if (!m_waveTraceValid) {
        gfx->fillScreen(COLOR_BLACK);
        gfx->drawFastHLine(0, leftMidY, SCREEN_WIDTH, COLOR_DIM_GREEN);
        gfx->drawFastHLine(0, rightMidY, SCREEN_WIDTH, COLOR_DIM_GREEN);
        gfx->drawFastHLine(0, halfHeight, SCREEN_WIDTH, COLOR_DIM_GREEN);
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

        // A. Erase previous frame traces
        if (m_waveTraceValid) {
            drawWavePolylineRange(m_prevWaveYL, sampleCount, chunkStart, chunkEnd, COLOR_BLACK);
            drawWavePolylineRange(m_prevWaveYR, sampleCount, chunkStart, chunkEnd, COLOR_BLACK);
        }

        // =================================================================
        // THE GRID FIX: Re-patch the background lines only for this chunk's width
        // =================================================================
        uint16_t x0 = static_cast<uint16_t>((chunkStart * (SCREEN_WIDTH - 1)) / (sampleCount - 1));
        uint16_t x1 = static_cast<uint16_t>(((chunkEnd - 1) * (SCREEN_WIDTH - 1)) / (sampleCount - 1));
        uint16_t segmentWidth = x1 - x0 + 1;

        gfx->drawFastHLine(x0, leftMidY,   segmentWidth, COLOR_DIM_GREEN);
        gfx->drawFastHLine(x0, rightMidY,  segmentWidth, COLOR_DIM_GREEN);
        gfx->drawFastHLine(x0, halfHeight, segmentWidth, COLOR_DIM_GREEN);

        // B. Draw new frame traces over the refreshed background
        drawWavePolylineRange(m_curWaveYL, sampleCount, chunkStart, chunkEnd, COLOR_CYAN);
        drawWavePolylineRange(m_curWaveYR, sampleCount, chunkStart, chunkEnd, COLOR_YELLOW);
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
    constexpr float ampScale = (SCREEN_HEIGHT / 2 - 4) / 2048.0f;

    if (!m_waveTraceValid) {
        gfx->fillScreen(COLOR_BLACK);
        gfx->drawFastHLine(0, midY, SCREEN_WIDTH, COLOR_DIM_GREEN);
    }

    for (size_t i = 0; i < sampleCount; ++i) {
        int16_t monoSample = static_cast<int16_t>((leftWave[i] + rightWave[i]) / 2);
        int16_t y = midY - static_cast<int16_t>(monoSample * ampScale);
        m_curWaveYMono[i] = constrain(y, 0, static_cast<int16_t>(SCREEN_HEIGHT - 1));
    }

    for (size_t chunkStart = 0; chunkStart < sampleCount - 1; chunkStart += WAVE_CHUNK_SAMPLES) {
        size_t chunkEnd = chunkStart + WAVE_CHUNK_SAMPLES + 1;
        if (chunkEnd > sampleCount) chunkEnd = sampleCount;

        // A. Erase previous frame trace
        if (m_waveTraceValid) {
            drawWavePolylineRange(m_prevWaveYMono, sampleCount, chunkStart, chunkEnd, COLOR_BLACK);
        }

        // =================================================================
        // THE GRID FIX: Re-patch the background line only for this chunk's width
        // =================================================================
        uint16_t x0 = static_cast<uint16_t>((chunkStart * (SCREEN_WIDTH - 1)) / (sampleCount - 1));
        uint16_t x1 = static_cast<uint16_t>(((chunkEnd - 1) * (SCREEN_WIDTH - 1)) / (sampleCount - 1));
        
        gfx->drawFastHLine(x0, midY, (x1 - x0 + 1), COLOR_DIM_GREEN);

        // B. Draw new frame trace over the refreshed background
        drawWavePolylineRange(m_curWaveYMono, sampleCount, chunkStart, chunkEnd, COLOR_CYAN);
    }

    for (size_t i = 0; i < sampleCount; ++i) {
        m_prevWaveYMono[i] = m_curWaveYMono[i];
    }

    m_waveTraceValid = true;
}