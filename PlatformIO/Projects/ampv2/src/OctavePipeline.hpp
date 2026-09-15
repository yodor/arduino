#pragma once
#include "FftEngine.hpp"
#include "Config.hpp"

// 31-band 1/3-octave spectrum pipeline. Owns its own FftEngine instance
// (OCTAVE_FFT_SIZE, much larger than Bar9Pipeline's) and the aggregation
// logic specific to this screen -- quadrature sum per band, not peak or
// average (see recomputeBinRanges()'s companion aggregation in
// pushChunk() for why).
class OctavePipeline {
public:
    OctavePipeline();

    void pushChunk(const uint16_t* interleavedChunk);
    void getBars(float* leftBars, float* rightBars) const;

    uint32_t lastFftDurationUs() const { return m_engine.lastFftDurationUs(); }
    float    observedRefreshHz() const { return m_engine.observedRefreshHz(); }

private:
    void recomputeBinRanges();

    FftEngine m_engine;

    size_t m_octaveStartBin[OCTAVE_BAND_COUNT] = {};
    size_t m_octaveEndBin[OCTAVE_BAND_COUNT]   = {};

    float m_octaveBarsL[OCTAVE_BAND_COUNT] = {};
    float m_octaveBarsR[OCTAVE_BAND_COUNT] = {};
};