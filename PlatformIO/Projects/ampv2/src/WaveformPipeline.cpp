#include "WaveformPipeline.hpp"
#include <cstdlib>
#include <cstring>

void WaveformPipeline::pushChunk(const uint16_t* interleavedChunk) {
    constexpr size_t kChunkPerChannel = CAPTURE_CHUNK_SAMPLES / 2;

    memmove(m_slideL, m_slideL + kChunkPerChannel,
            (WAVEFORM_WINDOW_SAMPLES - kChunkPerChannel) * sizeof(uint16_t));
    memmove(m_slideR, m_slideR + kChunkPerChannel,
            (WAVEFORM_WINDOW_SAMPLES - kChunkPerChannel) * sizeof(uint16_t));
    for (size_t i = 0; i < kChunkPerChannel; ++i) {
        m_slideL[WAVEFORM_WINDOW_SAMPLES - kChunkPerChannel + i] = interleavedChunk[i * 2]     & 0x0FFF;
        m_slideR[WAVEFORM_WINDOW_SAMPLES - kChunkPerChannel + i] = interleavedChunk[i * 2 + 1] & 0x0FFF;
    }

    float meanL = 0.0f, meanR = 0.0f;
    for (size_t i = 0; i < WAVEFORM_WINDOW_SAMPLES; ++i) {
        meanL += static_cast<float>(m_slideL[i]);
        meanR += static_cast<float>(m_slideR[i]);
    }
    meanL /= static_cast<float>(WAVEFORM_WINDOW_SAMPLES);
    meanR /= static_cast<float>(WAVEFORM_WINDOW_SAMPLES);

    for (size_t i = 0; i < WAVEFORM_WINDOW_SAMPLES; ++i) {
        int16_t rawL = static_cast<int16_t>(static_cast<float>(m_slideL[i]) - meanL);
        int16_t rawR = static_cast<int16_t>(static_cast<float>(m_slideR[i]) - meanR);
        m_waveL[i] = (abs(rawL) < WAVE_NOISE_GATE) ? 0 : rawL;
        m_waveR[i] = (abs(rawR) < WAVE_NOISE_GATE) ? 0 : rawR;
    }
}

void WaveformPipeline::getWaveform(int16_t* leftWave, int16_t* rightWave, size_t count) const {
    if (!leftWave && !rightWave) return;
    if (count == 0) return;

    size_t limit  = (count < WAVEFORM_WINDOW_SAMPLES) ? count : WAVEFORM_WINDOW_SAMPLES;
    float  stride = static_cast<float>(WAVEFORM_WINDOW_SAMPLES) / static_cast<float>(limit);

    for (size_t i = 0; i < limit; ++i) {
        size_t srcIdx = static_cast<size_t>(i * stride);
        if (srcIdx >= WAVEFORM_WINDOW_SAMPLES) srcIdx = WAVEFORM_WINDOW_SAMPLES - 1;

        if (leftWave)  leftWave[i]  = m_waveL[srcIdx];
        if (rightWave) rightWave[i] = m_waveR[srcIdx];
    }

    for (size_t i = limit; i < count; ++i) {
        if (leftWave)  leftWave[i]  = leftWave[limit - 1];
        if (rightWave) rightWave[i] = rightWave[limit - 1];
    }
}