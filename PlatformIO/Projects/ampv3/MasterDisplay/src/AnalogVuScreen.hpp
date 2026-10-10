#pragma once
#include "Screen.hpp"
#include "Config.hpp"

// Analog-style VU meter(s). STEREO: two needle gauges side by side. MONO:
// one larger, centered gauge, using the averaged (not summed) L+R signal.
// Level source is RMS of the raw waveform samples (not FFT bin data) --
// RMS/average power is what a real VU meter actually responds to, unlike
// the peak-bin-per-band approach the spectrum bars use.
class AnalogVuScreen : public Screen {
public:
    void onEnter() override;
    void render() override;
    const char* name() const override { return "ANALOG VU"; }

private:
    // Single-pole IIR filter approximating classic analog VU meter
    // response (ANSI C16.5: ~300ms to reach 99% of a step input), driven
    // by real elapsed time each call rather than assuming a fixed frame
    // rate -- so it behaves consistently even if actual frame timing
    // varies.
    struct VuBallistics {
        float    value        = 0.0f; // current needle position, 0..1
        uint32_t lastUpdateMs = 0;

        void update(float target, uint32_t now);
    };

    struct Gauge {
        int16_t pivotX = 0;
        int16_t pivotY = 0;
        int16_t radius = 0; // needle length; the static scale arc is drawn further out (see kScaleMarginPx) so erasing the needle never touches it
        float   prevAngleDeg = 0.0f;
        bool    needleDrawn  = false;
        VuBallistics ballistics;
    };

    void drawStaticScale(const Gauge& gauge);
    void updateNeedle(Gauge& gauge, float magnitude, uint32_t now);

    Gauge m_leftGauge;  // also used as the single gauge in MONO mode
    Gauge m_rightGauge; // unused in MONO mode
};