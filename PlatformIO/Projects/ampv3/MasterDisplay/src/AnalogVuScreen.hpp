#pragma once
#include "Screen.hpp"
#include "Config.hpp"

// Analog VU meter(s), drawn to use the whole panel: the scale is a wide,
// flat elliptical arc spanning the meter's full width, with the needle's
// pivot BELOW the bottom edge -- only the upper part of the needle is seen,
// like looking through a real meter's window. Mono: one meter across the
// whole panel; stereo: two side by side (L, R).
//
// The scale reads dBFS (RMS), matching AudioLevel::dbNormalize(): -50 at the
// left end to 0 at the right, labelled every 10 dB plus -5, red from -6 dB.
// It's drawn bold (4 px arc, 3 px ticks) and never touched again: the needle
// stops short of the ticks, the labels sit outside the arc and the channel
// letters in the bottom corners, out of the needle's path -- so only the
// needle itself is erased and redrawn, and only when it moves.
class AnalogVuScreen : public Screen {
public:
    void onEnter() override;
    void render() override;
    const char* name() const override { return "ANALOG VU"; }

private:
    struct VuBallistics {
        float    value        = 0.0f; // current needle position, 0..1
        uint32_t lastUpdateMs = 0;
        void update(float target, uint32_t now);
    };

    struct Gauge {
        // area on the panel
        int16_t x = 0, y = 0, w = 0, h = 0;
        bool    compact = false;   // stereo: smaller labels and margins
        // the scale ellipse: arc point at parameter phi is
        // (pivotX + a*sin(phi), pivotY - b*cos(phi)), phi in [-phiMax, +phiMax]
        float   pivotX = 0, pivotY = 0, a = 0, b = 0;
        // the needle as last drawn (clipped to the area), for erasing
        int16_t nx0 = 0, ny0 = 0, nx1 = 0, ny1 = 0;
        bool    needleDrawn = false;
        VuBallistics ballistics;
    };

    void layoutGauge(Gauge& g, int16_t x, int16_t y, int16_t w, int16_t h, bool compact);
    void arcPoint(const Gauge& g, float pos, float inset, float& px, float& py) const;
    void drawScale(const Gauge& g, const char* channelLabel);
    void updateNeedle(Gauge& g, float level, uint32_t now);
    void drawNeedle(const Gauge& g, uint16_t color);

    Gauge m_leftGauge;  // also the single gauge in MONO mode
    Gauge m_rightGauge; // unused in MONO mode
};
