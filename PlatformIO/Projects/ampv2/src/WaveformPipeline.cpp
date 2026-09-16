#include "WaveformPipeline.hpp"
#include <cstdlib>
#include <cstring>

void WaveformPipeline::applyWindowModeChange() {
    m_activeWindowSamples = (m_windowMode == WindowMode::OSCILLOSCOPE)
                                 ? kOscilloscopeSamples
                                 : kTrackPreviewSamples;

    // Clear rather than carry over stale content from a differently-sized
    // (or previous same-mode) window -- avoids old data briefly
    // reappearing in the newly-exposed portion of the buffer right after
    // a switch. The window fills up naturally over the next several
    // chunks instead, same as it does at boot.
    memset(m_slideL, 0, sizeof(m_slideL));
    memset(m_slideR, 0, sizeof(m_slideR));
    memset(m_waveL, 0, sizeof(m_waveL));
    memset(m_waveR, 0, sizeof(m_waveR));

    m_appliedWindowMode = m_windowMode;
}

void WaveformPipeline::pushChunk(const uint16_t* interleavedChunk) {
    // Pick up a mode change from core0 (menu) before processing -- cheap
    // to check every call regardless of whether one actually happened.
    if (m_windowMode != m_appliedWindowMode) {
        applyWindowModeChange();
    }

    constexpr size_t kChunkPerChannel = CAPTURE_CHUNK_SAMPLES / 2;
    size_t windowSamples = m_activeWindowSamples;

    // Cost here scales linearly with windowSamples and runs on EVERY
    // physical capture chunk (no staged/batched shifting like the FFT
    // engines use, by design -- see this class's header comment on why).
    // At kTrackPreviewSamples (4096) this is 8x the per-chunk work of
    // kOscilloscopeSamples (512); the "[Timing] per-chunk dispatch avg --
    // waveform=..." serial diagnostic in AudioEngine::runCore1() shows
    // the real measured cost of whichever mode is active.
    memmove(m_slideL, m_slideL + kChunkPerChannel,
            (windowSamples - kChunkPerChannel) * sizeof(uint16_t));
    memmove(m_slideR, m_slideR + kChunkPerChannel,
            (windowSamples - kChunkPerChannel) * sizeof(uint16_t));
    for (size_t i = 0; i < kChunkPerChannel; ++i) {
        m_slideL[windowSamples - kChunkPerChannel + i] = interleavedChunk[i * 2]     & 0x0FFF;
        m_slideR[windowSamples - kChunkPerChannel + i] = interleavedChunk[i * 2 + 1] & 0x0FFF;
    }

    float meanL = 0.0f, meanR = 0.0f;
    for (size_t i = 0; i < windowSamples; ++i) {
        meanL += static_cast<float>(m_slideL[i]);
        meanR += static_cast<float>(m_slideR[i]);
    }
    meanL /= static_cast<float>(windowSamples);
    meanR /= static_cast<float>(windowSamples);

    for (size_t i = 0; i < windowSamples; ++i) {
        int16_t rawL = static_cast<int16_t>(static_cast<float>(m_slideL[i]) - meanL);
        int16_t rawR = static_cast<int16_t>(static_cast<float>(m_slideR[i]) - meanR);
        m_waveL[i] = (abs(rawL) < WAVE_NOISE_GATE) ? 0 : rawL;
        m_waveR[i] = (abs(rawR) < WAVE_NOISE_GATE) ? 0 : rawR;
    }
}

void WaveformPipeline::getWaveform(int16_t* leftWave, int16_t* rightWave, size_t count) const {
    if (!leftWave && !rightWave) return;
    if (count == 0) return;

    size_t windowSamples = m_activeWindowSamples;
    size_t limit  = (count < windowSamples) ? count : windowSamples;
    float  stride = static_cast<float>(windowSamples) / static_cast<float>(limit);

    for (size_t i = 0; i < limit; ++i) {
        size_t srcIdx = static_cast<size_t>(i * stride);
        if (srcIdx >= windowSamples) srcIdx = windowSamples - 1;

        if (leftWave)  leftWave[i]  = m_waveL[srcIdx];
        if (rightWave) rightWave[i] = m_waveR[srcIdx];
    }

    for (size_t i = limit; i < count; ++i) {
        if (leftWave)  leftWave[i]  = leftWave[limit - 1];
        if (rightWave) rightWave[i] = rightWave[limit - 1];
    }
}