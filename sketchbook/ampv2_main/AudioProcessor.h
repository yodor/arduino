#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include <Arduino.h>
#include <math.h>

#ifndef KISS_FFT_SCALAR
#define KISS_FFT_SCALAR float
#endif

#include "kiss_fftr.h"

template <size_t N>
class AudioProcessor {
public:
    static_assert((N & (N - 1)) == 0, "Buffer size N must be a power of 2 for FFT.");

    AudioProcessor() {
        for (size_t i = 0; i < N; i++) {
            m_hanningWindow[i] = 0.5f * (1.0f - cosf((2.0f * M_PI * static_cast<float>(i)) / static_cast<float>(N - 1)));
        }
        m_kissCfg = kiss_fftr_alloc(static_cast<int>(N), 0, NULL, NULL);
    }

    ~AudioProcessor() {
        if (m_kissCfg) free(m_kissCfg);
    }

    void process(
        const uint16_t* rawSamples, 
        uint8_t* fftOutHeights, 
        uint8_t numBands, 
        uint16_t maxDisplayHeight, 
        float sampleRateHz,
        uint8_t linearBars = 3,       // Number of dedicated bass/sub-bass bars
        size_t binsPerLinearBar = 2,  // Minimum FFT bins per linear bar
        float maxFreqHz = 18000.0f    // High-end cutoff
    ) {
        if (!m_kissCfg) return;

        // 1. Calculate DC Bias
        uint32_t sum = 0;
        for (size_t i = 0; i < N; i++) sum += rawSamples[i];
        float avg = static_cast<float>(sum) / static_cast<float>(N);

        // 2. Highpass Filter + Normalization + Hanning Window
        float prevInput = (static_cast<float>(rawSamples[0]) - avg) / 2048.0f;
        float prevOutput = 0.0f;
        constexpr float R = 0.965f;
        float signalEnergy = 0.0f;

        for (size_t i = 0; i < N; i++) {
            float input = (static_cast<float>(rawSamples[i]) - avg) / 2048.0f;
            float hpFiltered = input - prevInput + (R * prevOutput);
            prevInput = input;
            prevOutput = hpFiltered;

            signalEnergy += (hpFiltered * hpFiltered);
            m_fftIn[i] = hpFiltered * m_hanningWindow[i];
        }

        // Noise gate check
        if (sqrtf(signalEnergy / static_cast<float>(N)) < 0.008f) {
            for (uint8_t b = 0; b < numBands; b++) fftOutHeights[b] = 2;
            return;
        }

        // 3. Execute Real FFT
        kiss_fftr(m_kissCfg, m_fftIn, m_fftOut);

        // 4. Generalized Bin Boundary Calculation
        float binWidthHz = sampleRateHz / static_cast<float>(N);
        
        // Crossover frequency where linear bin allocation ends and logarithmic begins
        size_t linearEndBin = 1 + (linearBars * binsPerLinearBar);
        float crossoverFreq = static_cast<float>(linearEndBin) * binWidthHz;

        uint8_t logBars = (numBands > linearBars) ? (numBands - linearBars) : 0;

        for (uint8_t b = 0; b < numBands; b++) {
            size_t startBin, endBin;

            if (b < linearBars) {
                // LINEAR REGION: Assign fixed, non-overlapping bin chunks
                startBin = 1 + (b * binsPerLinearBar);
                endBin   = startBin + binsPerLinearBar;
            } else {
                // LOGARITHMIC REGION: Scale exponentially from crossoverFreq to maxFreqHz
                uint8_t logIdx = b - linearBars;

                float fLow  = crossoverFreq * powf(maxFreqHz / crossoverFreq, static_cast<float>(logIdx)     / static_cast<float>(logBars));
                float fHigh = crossoverFreq * powf(maxFreqHz / crossoverFreq, static_cast<float>(logIdx + 1) / static_cast<float>(logBars));

                startBin = static_cast<size_t>(fLow / binWidthHz);
                endBin   = static_cast<size_t>(fHigh / binWidthHz);
            }

            // Enforce safe array bounds
            if (startBin < 1) startBin = 1;
            if (endBin <= startBin) endBin = startBin + 1;
            if (endBin > (N / 2)) endBin = N / 2;

            // Extract peak magnitude in calculated range
            float maxMagSq = 0.0f;
            for (size_t k = startBin; k < endBin; k++) {
                float r = m_fftOut[k].r;
                float i = m_fftOut[k].i;
                float magSq = (r * r) + (i * i);
                if (magSq > maxMagSq) maxMagSq = magSq;
            }

            // Normalization & dB mapping
            float mag = (sqrtf(maxMagSq) * 2.0f) / static_cast<float>(N);
            float db = 20.0f * log10f(mag + 1e-5f);

            constexpr float MIN_DB = -45.0f;
            constexpr float MAX_DB = 0.0f;

            float normalized = (db - MIN_DB) / (MAX_DB - MIN_DB);
            normalized = constrain(normalized, 0.0f, 1.0f);

            uint8_t height = static_cast<uint8_t>(normalized * static_cast<float>(maxDisplayHeight));
            fftOutHeights[b] = max(static_cast<uint8_t>(2), height);
        }
    }

private:
    kiss_fftr_cfg m_kissCfg = nullptr;
    float m_hanningWindow[N];
    kiss_fft_scalar m_fftIn[N];
    kiss_fft_cpx m_fftOut[(N / 2) + 1];
};

#endif
