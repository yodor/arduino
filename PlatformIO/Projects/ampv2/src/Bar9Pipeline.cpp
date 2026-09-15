#include "Bar9Pipeline.hpp"

Bar9Pipeline::Bar9Pipeline() : m_engine(BAR9_FFT_SIZE) {
    recomputeBinRanges();
}

void Bar9Pipeline::recomputeBinRanges() {
    // Bin count doubles per bar (1,2,4,8,...), skipping bins 0-1 (DC and
    // the bin needing different scaling).
    size_t startBin = 0;
    for (size_t bar = 0; bar < NUM_BARS; ++bar) {
        size_t binCount = 1u << bar; // 1, 2, 4, 8, 16, 32, 64, 128
        size_t endBin   = startBin + binCount;
        if (endBin > (BAR9_FFT_SIZE / 2)) endBin = BAR9_FFT_SIZE / 2;
        if (endBin <= startBin) endBin = startBin + 1;

        m_barStartBin[bar] = startBin;
        m_barEndBin[bar]   = endBin;

        startBin += binCount;
    }
}

void Bar9Pipeline::pushChunk(const uint16_t* interleavedChunk) {
    if (!m_engine.pushChunk(interleavedChunk)) return;

    const float* binsL     = m_engine.magnitudeBinsL();
    const float* binsR     = m_engine.magnitudeBinsR();
    size_t       binCount  = m_engine.binCount();

    for (size_t bar = 0; bar < NUM_BARS; ++bar) {
        size_t startBin = m_barStartBin[bar];
        size_t endBin   = m_barEndBin[bar];

        float maxL = 0.0f, maxR = 0.0f;
        for (size_t binIdx = startBin; binIdx < endBin; ++binIdx) {
            if (binIdx >= binCount) break;
            if (binsL[binIdx] > maxL) maxL = binsL[binIdx];
            if (binsR[binIdx] > maxR) maxR = binsR[binIdx];
        }

        // Apply noise gate to zero out low-level hardware noise (see
        // BAR_NOISE_GATE in Config.hpp for tuning notes).
        m_barsL[bar] = (maxL > BAR_NOISE_GATE) ? maxL : 0.0f;
        m_barsR[bar] = (maxR > BAR_NOISE_GATE) ? maxR : 0.0f;
    }
}

void Bar9Pipeline::getBars(float* leftBars, float* rightBars) const {
    for (size_t i = 0; i < NUM_BARS; ++i) {
        if (leftBars)  leftBars[i]  = m_barsL[i];
        if (rightBars) rightBars[i] = m_barsR[i];
    }
}