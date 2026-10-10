#include "SpectrumPipeline.hpp"
#include <cmath>

SpectrumPipeline::SpectrumPipeline() : m_engine(FFT_SIZE) {
    recomputeBinRanges();
}

void SpectrumPipeline::recomputeBinRanges() {
    if (m_bandLayoutMode == BandLayoutMode::DOUBLING) {
        m_activeBandCount = DOUBLING_BAND_COUNT;

        // Doubling scheme (1,2,4,8,...), starting from bin 0 -- see the
        // history notes on why this doesn't skip bins 0-1 anymore (the
        // per-window mean subtraction already zeroes true DC content
        // before the FFT sees it).
        size_t startBin = 0;
        for (size_t bar = 0; bar < DOUBLING_BAND_COUNT; ++bar) {
            size_t binCount = 1u << bar;
            size_t endBin   = startBin + binCount;
            if (endBin > (FFT_SIZE / 2)) endBin = FFT_SIZE / 2;
            if (endBin <= startBin) endBin = startBin + 1;

            m_startBin[bar] = startBin;
            m_endBin[bar]   = endBin;

            startBin += binCount;
        }
    } else if (m_bandLayoutMode == BandLayoutMode::FIXED_BANDS) {
        m_activeBandCount = FIXED_BAND_COUNT;

        float binWidthHz = AUDIO_SAMPLE_RATE_HZ / static_cast<float>(FFT_SIZE);
        for (size_t band = 0; band < FIXED_BAND_COUNT; ++band) {
            size_t startBin = static_cast<size_t>(kOctaveBands[band].loHz / binWidthHz);
            size_t endBin   = static_cast<size_t>(kOctaveBands[band].hiHz / binWidthHz);

            if (endBin <= startBin) endBin = startBin + 1;
            if (endBin > (FFT_SIZE / 2)) endBin = FFT_SIZE / 2;

            m_startBin[band] = startBin;
            m_endBin[band]   = endBin;
        }
    } else { // FULL -- linear slicing across ALL usable bins, evenly
             // divided as possible. Unlike FIXED_BANDS's perceptually-
             // weighted ISO bands or DOUBLING's exponential growth, this
             // is uncompressed, uniform FFT resolution -- as much of it
             // as the screen can actually show (see FULL_BAND_COUNT's
             // comment for how that limit was derived).
        m_activeBandCount = FULL_BAND_COUNT;

        size_t usableBins = FFT_SIZE / 2;
        for (size_t band = 0; band < FULL_BAND_COUNT; ++band) {
            size_t startBin = (band * usableBins) / FULL_BAND_COUNT;
            size_t endBin   = ((band + 1) * usableBins) / FULL_BAND_COUNT;

            if (endBin <= startBin) endBin = startBin + 1;
            if (endBin > usableBins) endBin = usableBins;

            m_startBin[band] = startBin;
            m_endBin[band]   = endBin;
        }
    }

    m_appliedBandLayoutMode = m_bandLayoutMode;
}

void SpectrumPipeline::pushChunk(const uint16_t* interleavedChunk) {
    // Pick up a mode change from core0 (menu) before processing -- cheap
    // to check every call regardless of whether one actually happened.
    if (m_bandLayoutMode != m_appliedBandLayoutMode) {
        recomputeBinRanges();
    }

    if (!m_engine.pushChunk(interleavedChunk)) return;

    const float* binsL    = m_engine.magnitudeBinsL();
    const float* binsR    = m_engine.magnitudeBinsR();
    size_t       binCount = m_engine.binCount();

    EnergyMode energyMode = m_energyMode; // snapshot once per call

    for (size_t band = 0; band < m_activeBandCount; ++band) {
        size_t startBin = m_startBin[band];
        size_t endBin   = m_endBin[band];

        float valueL = 0.0f, valueR = 0.0f;

        if (energyMode == EnergyMode::PEAK) {
            for (size_t binIdx = startBin; binIdx < endBin; ++binIdx) {
                if (binIdx >= binCount) break;
                if (binsL[binIdx] > valueL) valueL = binsL[binIdx];
                if (binsR[binIdx] > valueR) valueR = binsR[binIdx];
            }
        } else { // QUADRATURE_SUM -- sqrt(sum of magnitude^2), proportional
                 // to total band energy via Parseval's theorem, NOT an
                 // average -- see the history notes on why averaging
                 // under-represented wider (higher-frequency) bands at
                 // equal input level.
            float sumSqL = 0.0f, sumSqR = 0.0f;
            for (size_t binIdx = startBin; binIdx < endBin; ++binIdx) {
                if (binIdx >= binCount) break;
                sumSqL += binsL[binIdx] * binsL[binIdx];
                sumSqR += binsR[binIdx] * binsR[binIdx];
            }
            valueL = sqrtf(sumSqL);
            valueR = sqrtf(sumSqR);
        }

        m_barsL[band] = (valueL > SPECTRUM_NOISE_GATE) ? valueL : 0.0f;
        m_barsR[band] = (valueR > SPECTRUM_NOISE_GATE) ? valueR : 0.0f;
    }
}

void SpectrumPipeline::getBars(float* leftBars, float* rightBars) const {
    for (size_t i = 0; i < m_activeBandCount; ++i) {
        if (leftBars)  leftBars[i]  = m_barsL[i];
        if (rightBars) rightBars[i] = m_barsR[i];
    }
}