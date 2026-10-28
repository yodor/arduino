#include "WaveformPipeline.hpp"
#include <climits>
#include <cstdlib>
#include <cstring>
#include "hardware/sync.h" // __dmb()

void WaveformPipeline::applyWindowModeChange() {
    // DAW draws from its own timeline, so the short window does (cheaper).
    m_activeWindowSamples = (m_windowMode == WindowMode::TRACK_PREVIEW)
                                 ? kTrackPreviewSamples
                                 : kOscilloscopeSamples;

    // Clear rather than carry over stale content from a differently-sized
    // (or previous same-mode) window -- avoids old data briefly
    // reappearing in the newly-exposed portion of the buffer right after
    // a switch. The window fills up naturally over the next several
    // chunks instead, same as it does at boot.
    memset(m_slideL, 0, sizeof(m_slideL));
    memset(m_slideR, 0, sizeof(m_slideR));
    // The output buffers aren't cleared: the published one stays valid
    // (and keeps its own size in m_waveSamples) until the next chunk
    // publishes a buffer for the new window size.

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

    accumulateEnvelope(interleavedChunk, meanL, meanR);

    uint8_t  buf  = m_writeBuf;
    int16_t* outL = m_waveL[buf];
    int16_t* outR = m_waveR[buf];
    for (size_t i = 0; i < windowSamples; ++i) {
        int16_t rawL = static_cast<int16_t>(static_cast<float>(m_slideL[i]) - meanL);
        int16_t rawR = static_cast<int16_t>(static_cast<float>(m_slideR[i]) - meanR);
        outL[i] = rawL;
        outR[i] = rawR;
    }
    m_waveSamples[buf] = windowSamples;

    __dmb();            // the buffer's contents are visible to core0 before it's published
    m_readyBuf = buf;
    m_writeBuf = buf ^ 1;
}

void WaveformPipeline::getWaveform(int16_t* leftWave, int16_t* rightWave, size_t count, bool trigger) const {
    if (!leftWave && !rightWave) return;
    if (count == 0) return;

    // Read only the published buffer -- see m_waveL's comment in the header.
    uint8_t buf = m_readyBuf;
    __dmb();
    const int16_t* srcL = m_waveL[buf];
    const int16_t* srcR = m_waveR[buf];
    size_t windowSamples = m_waveSamples[buf];

    size_t start = 0;
    size_t span  = windowSamples;
    if (trigger && windowSamples >= 8) {
        size_t search = windowSamples / 4;
        span = windowSamples - search;

        // Hysteresis: the signal must first dip below -hyst before a rise
        // through zero counts, so noise around zero can't trigger.
        int32_t peak = 0;
        for (size_t i = 0; i < windowSamples; i += 4) {
            int32_t v = static_cast<int32_t>(srcL[i]) + srcR[i];
            if (v < 0) v = -v;
            if (v > peak) peak = v;
        }
        int32_t hyst  = (peak / 8 > 16) ? peak / 8 : 16;
        bool    armed = false;
        for (size_t i = 0; i < search; ++i) {
            int32_t v = static_cast<int32_t>(srcL[i]) + srcR[i];
            if (v < -hyst) {
                armed = true;
            } else if (armed && v >= 0) {
                start = i;
                break;
            }
        }
    }

    size_t limit  = (count < span) ? count : span;
    float  stride = static_cast<float>(span) / static_cast<float>(limit);

    for (size_t i = 0; i < limit; ++i) {
        size_t srcIdx = start + static_cast<size_t>(i * stride);
        if (srcIdx >= windowSamples) srcIdx = windowSamples - 1;

        if (leftWave)  leftWave[i]  = srcL[srcIdx];
        if (rightWave) rightWave[i] = srcR[srcIdx];
    }

    for (size_t i = limit; i < count; ++i) {
        if (leftWave)  leftWave[i]  = leftWave[limit - 1];
        if (rightWave) rightWave[i] = rightWave[limit - 1];
    }
}

void WaveformPipeline::getScopeMono(int16_t* out, size_t count, size_t spanSamples) const {
    if (!out || count == 0) return;

    uint8_t buf = m_readyBuf;
    __dmb();
    const int16_t* srcL = m_waveL[buf];
    const int16_t* srcR = m_waveR[buf];
    size_t windowSamples = m_waveSamples[buf];

    size_t maxSpan = windowSamples - windowSamples / 4;
    if (spanSamples > maxSpan) spanSamples = maxSpan;
    if (spanSamples == 0) spanSamples = 1;
    size_t search = windowSamples - spanSamples; // where the trigger may sit

    // Rising zero crossing with hysteresis, as in getWaveform().
    int32_t peak = 0;
    for (size_t i = 0; i < windowSamples; i += 4) {
        int32_t v = static_cast<int32_t>(srcL[i]) + srcR[i];
        if (v < 0) v = -v;
        if (v > peak) peak = v;
    }
    int32_t hyst  = (peak / 8 > 16) ? peak / 8 : 16;
    bool    armed = false;
    size_t  start = 0;
    for (size_t i = 0; i < search; ++i) {
        int32_t v = static_cast<int32_t>(srcL[i]) + srcR[i];
        if (v < -hyst) {
            armed = true;
        } else if (armed && v >= 0) {
            start = i;
            break;
        }
    }

    float stride = static_cast<float>(spanSamples) / static_cast<float>(count);
    for (size_t i = 0; i < count; ++i) {
        size_t idx = start + static_cast<size_t>(i * stride);
        if (idx >= windowSamples) idx = windowSamples - 1;
        out[i] = static_cast<int16_t>((static_cast<int32_t>(srcL[idx]) + srcR[idx]) / 2);
    }
}

// Core 1, every chunk: this chunk's min/max (DC removed with the window's
// mean) folded into the current column; every
// WAVEFORM_DAW_CHUNKS_PER_COLUMN chunks the column is stored in the ring.
void WaveformPipeline::accumulateEnvelope(const uint16_t* chunk, float meanL, float meanR) {
    constexpr size_t kChunkPerChannel = CAPTURE_CHUNK_SAMPLES / 2;
    if (m_accChunks == 0) {
        m_accMinL = m_accMinR = m_accMinM = INT16_MAX;
        m_accMaxL = m_accMaxR = m_accMaxM = INT16_MIN;
    }
    for (size_t i = 0; i < kChunkPerChannel; ++i) {
        int16_t l = static_cast<int16_t>(static_cast<float>(chunk[i * 2]     & 0x0FFF) - meanL);
        int16_t r = static_cast<int16_t>(static_cast<float>(chunk[i * 2 + 1] & 0x0FFF) - meanR);
        int16_t m = static_cast<int16_t>((static_cast<int32_t>(l) + r) / 2);
        if (l < m_accMinL) m_accMinL = l;
        if (l > m_accMaxL) m_accMaxL = l;
        if (r < m_accMinR) m_accMinR = r;
        if (r > m_accMaxR) m_accMaxR = r;
        if (m < m_accMinM) m_accMinM = m;
        if (m > m_accMaxM) m_accMaxM = m;
    }
    if (++m_accChunks < static_cast<int>(WAVEFORM_DAW_CHUNKS_PER_COLUMN)) return;
    m_accChunks = 0;
    uint32_t col = m_envColumns % kEnvCols;
    m_envMinL[col] = m_accMinL; m_envMaxL[col] = m_accMaxL;
    m_envMinR[col] = m_accMinR; m_envMaxR[col] = m_accMaxR;
    m_envMinM[col] = m_accMinM; m_envMaxM[col] = m_accMaxM;
    __dmb();
    m_envColumns = m_envColumns + 1;
}

// Core 0: the timeline, oldest column first. (Core 1 may store one column
// meanwhile -- at worst that column shows its newer value a frame early.)
void WaveformPipeline::getEnvelope(int16_t* minL, int16_t* maxL, int16_t* minR, int16_t* maxR,
                                   size_t count, bool mono) const {
    if (!minL || !maxL || count != static_cast<size_t>(kEnvCols)) return;
    uint32_t n = m_envColumns;
    __dmb();
    for (int i = 0; i < kEnvCols; ++i) {
        uint32_t col = (n + static_cast<uint32_t>(i)) % kEnvCols; // n % kEnvCols is the oldest
        if (mono) {
            minL[i] = m_envMinM[col]; maxL[i] = m_envMaxM[col];
        } else {
            minL[i] = m_envMinL[col]; maxL[i] = m_envMaxL[col];
            if (minR && maxR) { minR[i] = m_envMinR[col]; maxR[i] = m_envMaxR[col]; }
        }
    }
}
