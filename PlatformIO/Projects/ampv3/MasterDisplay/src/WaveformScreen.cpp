#include "WaveformScreen.hpp"
#include "Renderer.hpp"
#include "AudioEngine.hpp"
#include "DisplaySettings.hpp"
#include "ColorTheme.hpp"
#include <cmath>

void WaveformScreen::onEnter() {
    Renderer::instance().clear();
    m_valid = false;
    setupLanes();
}

// Lane geometry and colours: two lanes in stereo (the theme's first colour
// on top, its last below), one lane of the mix in mono (its middle mix).
void WaveformScreen::setupLanes() {
    ColorTheme& theme = ColorTheme::instance();
    bool stereo = DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO;
    m_wasStereo = stereo;
    if (stereo) {
        m_laneCount = 2;
        m_lanes[0].top = 0;                 m_lanes[0].bottom = SCREEN_HEIGHT / 2 - 2;
        m_lanes[1].top = SCREEN_HEIGHT / 2 + 1; m_lanes[1].bottom = SCREEN_HEIGHT - 1;
        m_lanes[0].color = theme.lowColor();
        m_lanes[1].color = theme.highColor();
    } else {
        m_laneCount = 1;
        m_lanes[0].top = 0; m_lanes[0].bottom = SCREEN_HEIGHT - 1;
        m_lanes[0].color = theme.mixColor();
    }
    for (Lane& l : m_lanes) l.peak = 0.0f;
}

// Auto-gain: the full scale follows the frame's peak up at once and decays
// slowly; never below WAVEFORM_MIN_FULL_SCALE. Returns pixels per count.
float WaveformScreen::gainFor(Lane& lane, int32_t framePeak) {
    float half = (lane.bottom - lane.top) / 2.0f - 2.0f;
    if (!DisplaySettings::instance().getWaveformAutoGain()) return half / 2048.0f;
    float p = static_cast<float>(framePeak);
    lane.peak = (p > lane.peak) ? p : lane.peak * WAVEFORM_GAIN_DECAY;
    float fullScale = (lane.peak > WAVEFORM_MIN_FULL_SCALE) ? lane.peak : WAVEFORM_MIN_FULL_SCALE;
    return half / (fullScale * 1.1f); // ~10% headroom above the peak
}

void WaveformScreen::spansFromLine(Lane& lane, const int16_t* v, int16_t* lo, int16_t* hi) {
    int32_t peak = 0;
    for (int i = 0; i <= kCols; ++i) { int32_t a = v[i] < 0 ? -v[i] : v[i]; if (a > peak) peak = a; }
    float k   = gainFor(lane, peak);
    float mid = (lane.top + lane.bottom) / 2.0f;
    auto y = [&](int16_t s) {
        int16_t r = static_cast<int16_t>(lroundf(mid - s * k));
        return r < lane.top ? lane.top : (r > lane.bottom ? lane.bottom : r);
    };
    int16_t prev = y(v[0]);
    for (int i = 0; i < kCols; ++i) {
        int16_t next = y(v[i + 1]);
        lo[i] = prev < next ? prev : next;
        hi[i] = prev < next ? next : prev;
        prev = next;
    }
}

void WaveformScreen::spansFromEnvelope(Lane& lane, const int16_t* vmin, const int16_t* vmax, int16_t* lo, int16_t* hi) {
    int32_t peak = 0;
    for (int i = 0; i < kCols; ++i) {
        int32_t a = vmax[i] < 0 ? -vmax[i] : vmax[i], b = vmin[i] < 0 ? -vmin[i] : vmin[i];
        if (a > peak) peak = a;
        if (b > peak) peak = b;
    }
    float k   = gainFor(lane, peak);
    float mid = (lane.top + lane.bottom) / 2.0f;
    auto y = [&](int16_t s) {
        int16_t r = static_cast<int16_t>(lroundf(mid - s * k));
        return r < lane.top ? lane.top : (r > lane.bottom ? lane.bottom : r);
    };
    for (int i = 0; i < kCols; ++i) {
        lo[i] = y(vmax[i]);  // higher value = higher on screen = smaller y
        hi[i] = y(vmin[i]);
    }
}

// Each column is one pixel pair (2 px) wide and only its edges are touched:
// the part of the old span the new one no longer covers is erased, and only
// the part of the new span that wasn't already lit is drawn. A column that
// barely moved costs one or two short writes.
void WaveformScreen::drawLane(Lane& lane, const int16_t* lo, const int16_t* hi) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    for (int i = 0; i < kCols; ++i) {
        int16_t x = static_cast<int16_t>(i * 2);
        int16_t nLo = lo[i], nHi = hi[i];
        if (!m_valid) {
            gfx->fillRect(x, nLo, 2, nHi - nLo + 1, lane.color);
        } else {
            int16_t oLo = lane.lo[i], oHi = lane.hi[i];
            if (oLo == nLo && oHi == nHi) continue; // unchanged column
            // erase: old rows above / below the new span
            if (oLo < nLo) { int16_t to = (oHi < nLo ? oHi : nLo - 1); gfx->fillRect(x, oLo, 2, to - oLo + 1, COLOR_BLACK); }
            if (oHi > nHi) { int16_t from = (oLo > nHi ? oLo : nHi + 1); gfx->fillRect(x, from, 2, oHi - from + 1, COLOR_BLACK); }
            // draw: new rows above / below the old span (the overlap is lit already)
            if (nLo < oLo) { int16_t to = (nHi < oLo ? nHi : oLo - 1); gfx->fillRect(x, nLo, 2, to - nLo + 1, lane.color); }
            if (nHi > oHi) { int16_t from = (nLo > oHi ? nLo : oHi + 1); gfx->fillRect(x, from, 2, nHi - from + 1, lane.color); }
        }
        lane.lo[i] = nLo;
        lane.hi[i] = nHi;
    }
}

void WaveformScreen::render() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;

    bool stereo = DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO;
    if (stereo != m_wasStereo) { onEnter(); }        // layout changed under us
    if (!m_valid) gfx->fillScreen(COLOR_BLACK);

    AudioEngine& audio = AudioEngine::instance();
    WaveformPipeline::WindowMode mode = audio.getWaveformWindowMode();
    // Working buffers live in the screen object, not on the stack.
    int16_t* lo = m_spanLo;
    int16_t* hi = m_spanHi;

    if (mode == WaveformPipeline::WindowMode::DAW) {
        int16_t* minA = m_bufA;
        int16_t* maxA = m_bufB;
        int16_t* minB = m_bufC;
        int16_t* maxB = m_bufD;
        if (stereo) {
            audio.getEnvelope(minA, maxA, minB, maxB, kCols, false);
            spansFromEnvelope(m_lanes[0], minA, maxA, lo, hi); drawLane(m_lanes[0], lo, hi);
            spansFromEnvelope(m_lanes[1], minB, maxB, lo, hi); drawLane(m_lanes[1], lo, hi);
        } else {
            audio.getEnvelope(minA, maxA, nullptr, nullptr, kCols, true);
            spansFromEnvelope(m_lanes[0], minA, maxA, lo, hi); drawLane(m_lanes[0], lo, hi);
        }
    } else {
        // The oscilloscope: kCols+1 points over the scale's span, so column i
        // joins point i to point i+1; triggered (a still picture of the cycles).
        int16_t* a = m_bufA;   // kCols + 1 points per lane
        int16_t* b = m_bufB;
        size_t span = WaveformPipeline::scopeSamplesForScale(audio.getWaveformScopeScale());
        audio.getWaveform(a, b, kCols + 1, WAVEFORM_TRIGGER, span);
        if (stereo) {
            spansFromLine(m_lanes[0], a, lo, hi); drawLane(m_lanes[0], lo, hi);
            spansFromLine(m_lanes[1], b, lo, hi); drawLane(m_lanes[1], lo, hi);
        } else {
            for (int i = 0; i <= kCols; ++i) a[i] = static_cast<int16_t>((static_cast<int32_t>(a[i]) + b[i]) / 2);
            spansFromLine(m_lanes[0], a, lo, hi); drawLane(m_lanes[0], lo, hi);
        }
    }
    m_valid = true;
}
