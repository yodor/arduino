#pragma once
#include "Screen.hpp"
#include "Config.hpp"

// The waveform screen, in three modes (Theme > Waveform):
//   OSCILLOSCOPE   a short window, triggered at a rising zero crossing:
//                  individual cycles, standing still
//   TRACK_PREVIEW  the long window as a line
//   DAW            the long window as a min/max ENVELOPE: each column is the
//                  span from the lowest to the highest sample in its slice
//                  of the window (~19 samples), so every peak and transient
//                  shows, with no aliasing -- the solid look of a DAW clip
//
// One column renderer draws all of them: WAVEFORM_SAMPLES columns, each one
// NV3007 pixel pair wide (2 px), drawn as a vertical span. In the line modes
// a column's span joins its point to the next one (a connected trace); in
// DAW mode it's the min..max range. Each frame a column only erases the part
// of its old span the new one no longer covers and draws the new span -- a
// handful of small writes per column, nothing left behind, no flicker.
//
// Stereo: two lanes (L top, R bottom) in the theme's first and last colours;
// mono: one lane of the L+R mix in its middle mix. Optional auto-gain
// (Theme > Wave > Auto Gain) keeps the trace a readable size at any level.
class WaveformScreen : public Screen {
public:
    void onEnter() override;
    void render() override;
    const char* name() const override { return "WAVEFORM"; }

private:
    static constexpr int kCols = WAVEFORM_SAMPLES;

    struct Lane {
        int16_t  top = 0, bottom = 0;   // the lane's pixel rows, inclusive
        uint16_t color = 0;
        int16_t  lo[kCols] = {};        // what each column shows now
        int16_t  hi[kCols] = {};
        float    peak = 0.0f;           // auto-gain state (ADC counts)
    };

    void setupLanes();
    // The new spans for a lane from raw values: a line (point i joined to
    // point i+1; `v` holds kCols+1 points) or an envelope (vmin/vmax per column).
    void spansFromLine(Lane& lane, const int16_t* v, int16_t* lo, int16_t* hi);
    void spansFromEnvelope(Lane& lane, const int16_t* vmin, const int16_t* vmax, int16_t* lo, int16_t* hi);
    void drawLane(Lane& lane, const int16_t* lo, const int16_t* hi);
    float gainFor(Lane& lane, int32_t framePeak);

    Lane m_lanes[2];
    int  m_laneCount = 1;
    bool m_valid     = false;   // the lanes on the panel match lo/hi
    bool m_wasStereo = false;
};
