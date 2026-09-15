#pragma once
#include "FftEngine.hpp"
#include "Config.hpp"

// 9-bar doubling-scheme spectrum pipeline. Owns its own FftEngine
// instance (BAR9_FFT_SIZE) and the aggregation logic specific to this
// screen -- peak-bin-per-range, unlike OctavePipeline's quadrature sum.
class Bar9Pipeline {
public:
    Bar9Pipeline();

    // Feeds one physical capture chunk through this pipeline's FFT
    // engine; re-aggregates m_barsL/R whenever the engine reports a
    // fresh FFT ran.
    void pushChunk(const uint16_t* interleavedChunk);

    void getBars(float* leftBars, float* rightBars) const;

    uint32_t lastFftDurationUs() const { return m_engine.lastFftDurationUs(); }
    float    observedRefreshHz() const { return m_engine.observedRefreshHz(); }

private:
    void recomputeBinRanges();

    FftEngine m_engine;

    size_t m_barStartBin[NUM_BARS] = {};
    size_t m_barEndBin[NUM_BARS]   = {};

    float m_barsL[NUM_BARS] = {};
    float m_barsR[NUM_BARS] = {};
};