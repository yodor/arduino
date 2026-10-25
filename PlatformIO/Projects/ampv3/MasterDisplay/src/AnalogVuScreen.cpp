#include "AnalogVuScreen.hpp"
#include "Renderer.hpp"
#include "AudioEngine.hpp"
#include "DisplaySettings.hpp"
#include "AudioLevel.hpp"
#include <cmath>
#include <cstring>

namespace {
constexpr float kPivotBelow = 6.0f; // the pivot sits at least this far below the meter's bottom edge
constexpr float kFloorDb = -50.0f; // must match AudioLevel::dbNormalize(): -50 dB -> 0, 0 dB -> 1
constexpr float kRedFromDb = -6.0f;

struct Mark { float db; const char* label; };
const Mark kLabelled[] = { {-40, "-40"}, {-30, "-30"}, {-20, "-20"}, {-10, "-10"}, {-5, "-5"}, {0, "0"} };

constexpr int16_t  kArcThick   = 4;   // px, the arc's thickness (drawn inward)
constexpr int16_t  kMajorLen   = 13;  // tick lengths, inward from the arc
constexpr int16_t  kMinorLen   = 7;
constexpr int16_t  kTickWidth  = 3;   // px
constexpr int16_t  kNeedleGap  = 4;   // the needle's tip stops this far inside the longest ticks
constexpr uint16_t kScaleColor = COLOR_WHITE;
constexpr uint16_t kRedColor   = COLOR_RED;
constexpr uint16_t kNeedleColor = 0xFD20; // orange
constexpr uint16_t kLabelColor = 0xC618; // light gray

float posForDb(float db) { return (db - kFloorDb) / -kFloorDb; }
} // namespace

void AnalogVuScreen::VuBallistics::update(float target, uint32_t now) {
    constexpr float kTauSeconds = 0.3f / 4.605f; // ~0.0651s baseline (VU-like 300 ms rise)

    float dtSec = (now - lastUpdateMs) / 1000.0f;
    lastUpdateMs = now;

    float alpha = 1.0f - expf(-dtSec / kTauSeconds);
    value += alpha * (target - value);
}

// Fits the scale circle to the area: a chord spanning the width (less a
// margin), the arc's top just below the labels and its ends at about 3/4 of
// the height. If that circle's centre (the pivot) would land inside the
// area, the radius grows until it sits just below the bottom edge -- a
// flatter arc, but the needle still comes up from below.
void AnalogVuScreen::layoutGauge(Gauge& g, int16_t x, int16_t y, int16_t w, int16_t h, bool compact) {
    g.x = x; g.y = y; g.w = w; g.h = h;
    g.compact = compact;
    float margin = compact ? 12.0f : 18.0f;
    float yTop   = y + (compact ? 22.0f : 30.0f);  // room for the labels above the arc
    float c      = w / 2.0f - margin;               // half the chord
    float sag    = y + h * 0.75f - yTop;            // arc depth wanted: ends at ~3/4 of the height
    float r      = (c * c + sag * sag) / (2.0f * sag);
    float rMin   = (y + h + kPivotBelow) - yTop;    // keeps the pivot below the area
    if (r < rMin) r = rMin;
    g.r      = r;
    g.phiMax = asinf(c / r);
    g.pivotX = x + w / 2.0f;
    g.pivotY = yTop + r;
    g.needleDrawn = false;
    g.ballistics  = VuBallistics{};
}

// A point on (or, with inset > 0, inside / with inset < 0, outside) the
// scale circle at scale position pos (0 = left end, 1 = right end); inset
// runs along the radius, i.e. square to the scale.
void AnalogVuScreen::arcPoint(const Gauge& g, float pos, float inset, float& px, float& py) const {
    float theta = (-1.0f + 2.0f * pos) * g.phiMax;
    px = g.pivotX + (g.r - inset) * sinf(theta);
    py = g.pivotY - (g.r - inset) * cosf(theta);
}

void AnalogVuScreen::drawScale(const Gauge& g, const char* channelLabel) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    float redFrom = posForDb(kRedFromDb);

    // The arc, kArcThick px thick (concentric copies, inward), red at the top end.
    constexpr int kSteps = 60;
    for (int16_t k = 0; k < kArcThick; ++k) {
        float px0, py0;
        arcPoint(g, 0.0f, k, px0, py0);
        for (int s = 1; s <= kSteps; ++s) {
            float pos = static_cast<float>(s) / kSteps, px1, py1;
            arcPoint(g, pos, k, px1, py1);
            uint16_t c = (pos > redFrom) ? kRedColor : kScaleColor;
            gfx->drawLine(lroundf(px0), lroundf(py0), lroundf(px1), lroundf(py1), c);
            px0 = px1; py0 = py1;
        }
    }

    // Ticks every 5 dB (labelled ones longer), kTickWidth px wide.
    for (int db = static_cast<int>(kFloorDb); db <= 0; db += 5) {
        bool major = false;
        for (const Mark& m : kLabelled) major |= (static_cast<int>(m.db) == db);
        float pos = posForDb(static_cast<float>(db)), ox, oy, ix, iy;
        arcPoint(g, pos, 0.0f, ox, oy);
        arcPoint(g, pos, major ? kMajorLen : kMinorLen, ix, iy);
        uint16_t c = (pos > redFrom + 0.001f) ? kRedColor : kScaleColor;
        for (int16_t d = 0; d < kTickWidth; ++d) {
            int16_t o = d - kTickWidth / 2;
            gfx->drawLine(lroundf(ix) + o, lroundf(iy), lroundf(ox) + o, lroundf(oy), c);
        }
    }

    // Labels just outside the arc.
    uint8_t size = g.compact ? 1 : 2;
    gfx->setTextSize(size);
    for (const Mark& m : kLabelled) {
        float pos = posForDb(m.db), lx, ly;
        arcPoint(g, pos, -(g.compact ? 4.0f : 6.0f), lx, ly);
        int16_t tw = static_cast<int16_t>(strlen(m.label)) * 6 * size;
        int16_t tx = static_cast<int16_t>(lroundf(lx)) - tw / 2;
        if (tx < g.x) tx = g.x;
        if (tx + tw > g.x + g.w) tx = g.x + g.w - tw;
        gfx->setTextColor(pos > redFrom ? kRedColor : kLabelColor, COLOR_BLACK);
        gfx->setCursor(tx, static_cast<int16_t>(lroundf(ly)) - 8 * size);
        gfx->print(m.label);
    }

    // Channel letter (or "dBFS") in the bottom-left corner, out of the needle's path.
    gfx->setTextSize(g.compact ? 2 : 1);
    gfx->setTextColor(kLabelColor, COLOR_BLACK);
    gfx->setCursor(g.x + 6, g.y + g.h - (g.compact ? 18 : 10));
    gfx->print(channelLabel);
}

// The needle: from where its line enters the area (its pivot is below it)
// up to just inside the ticks, 2 px wide.
void AnalogVuScreen::drawNeedle(const Gauge& g, uint16_t color) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    gfx->drawLine(g.nx0, g.ny0, g.nx1, g.ny1, color);
    gfx->drawLine(g.nx0 + 1, g.ny0, g.nx1 + 1, g.ny1, color);
}

void AnalogVuScreen::updateNeedle(Gauge& g, float level, uint32_t now) {
    g.ballistics.update(level, now);
    float pos = g.ballistics.value;
    if (pos < 0.0f) pos = 0.0f;
    if (pos > 1.0f) pos = 1.0f;

    float tx, ty;
    arcPoint(g, pos, kMajorLen + kNeedleGap, tx, ty);

    // Clip the line tip -> pivot to the area: the pivot is below the bottom
    // edge, so the visible part ends where the line leaves the area.
    float dx = g.pivotX - tx, dy = g.pivotY - ty;
    float t  = 1.0f;
    float bottom = g.y + g.h - 1;
    if (dy > 0.0f && ty + dy > bottom) t = (bottom - ty) / dy;
    float left = g.x, right = g.x + g.w - 2; // -2: the needle is 2 px wide
    float sx = tx + t * dx;
    if (sx < left && dx < 0.0f)  t = (left - tx) / dx;
    if (sx > right && dx > 0.0f) t = (right - tx) / dx;

    int16_t x0 = static_cast<int16_t>(lroundf(tx + t * dx)), y0 = static_cast<int16_t>(lroundf(ty + t * dy));
    int16_t x1 = static_cast<int16_t>(lroundf(tx)),          y1 = static_cast<int16_t>(lroundf(ty));
    if (g.needleDrawn && x0 == g.nx0 && y0 == g.ny0 && x1 == g.nx1 && y1 == g.ny1) return; // didn't move

    if (g.needleDrawn) drawNeedle(g, COLOR_BLACK);
    g.nx0 = x0; g.ny0 = y0; g.nx1 = x1; g.ny1 = y1;
    drawNeedle(g, kNeedleColor);
    g.needleDrawn = true;
}

void AnalogVuScreen::onEnter() {
    Renderer::instance().clear();
    if (DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO) {
        int16_t half = SCREEN_WIDTH / 2;
        layoutGauge(m_leftGauge,  0,    0, half, SCREEN_HEIGHT, true);
        layoutGauge(m_rightGauge, half, 0, half, SCREEN_HEIGHT, true);
        drawScale(m_leftGauge, "L");
        drawScale(m_rightGauge, "R");
    } else {
        layoutGauge(m_leftGauge, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, false);
        drawScale(m_leftGauge, "dBFS");
    }
}

void AnalogVuScreen::render() {
    int16_t leftWave[WAVEFORM_SAMPLES];
    int16_t rightWave[WAVEFORM_SAMPLES];
    AudioEngine::instance().getWaveform(leftWave, rightWave, WAVEFORM_SAMPLES);

    uint32_t now = millis();

    if (DisplaySettings::instance().getChannelMode() == ChannelMode::STEREO) {
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
