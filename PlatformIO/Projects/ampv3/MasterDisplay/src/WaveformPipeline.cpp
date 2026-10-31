#include "WaveformPipeline.hpp"
#include <climits>
#include <cstdlib>
#include <cstring>
#include "hardware/sync.h" // __dmb()

// Core 1, every chunk: append it to the ring -- the same small cost at any
// scale -- keep the running DC level of the newest kLevelSamples, and feed
// the DAW timeline. Readers take what they need from the ring when they draw.
void WaveformPipeline::pushChunk(const uint16_t* interleavedChunk) {
    constexpr size_t   kChunkPerChannel = CAPTURE_CHUNK_SAMPLES / 2;
    constexpr uint32_t kMask = kRingSamples - 1;
    uint32_t w = m_written;
    for (size_t i = 0; i < kChunkPerChannel; ++i, ++w) {
        uint16_t l = interleavedChunk[i * 2]     & 0x0FFF;
        uint16_t r = interleavedChunk[i * 2 + 1] & 0x0FFF;
        // The running sums over the newest kLevelSamples: add the new sample,
        // drop the one leaving that window (still in the ring: it's 2x longer).
        if (w >= kLevelSamples) {
            m_dcSumL -= m_ringL[(w - kLevelSamples) & kMask];
            m_dcSumR -= m_ringR[(w - kLevelSamples) & kMask];
        }
        m_ringL[w & kMask] = l;
        m_ringR[w & kMask] = r;
        m_dcSumL += l;
        m_dcSumR += r;
    }
    __dmb();            // the samples are in the ring before the count says so
    m_written = w;

    uint32_t n = (w < kLevelSamples) ? w : kLevelSamples;
    accumulateEnvelope(interleavedChunk, static_cast<float>(m_dcSumL) / n, static_cast<float>(m_dcSumR) / n);
}

// Core 0: the newest `window` samples and their mean (the DC level). The ring
// is twice the longest window, so core 1 can't reach these samples while
// they're read (it would need ~185 ms of audio to do so).
bool WaveformPipeline::view(size_t window, View& v) const {
    uint32_t w = m_written;
    __dmb();
    if (w == 0) return false;
    if (window > kMaxWindowSamples) window = kMaxWindowSamples;
    if (window > w) window = w;                       // just after boot
    v.p = this;
    v.base = w - static_cast<uint32_t>(window);
    v.window = window;
    uint32_t sumL = 0, sumR = 0;
    for (size_t i = 0; i < window; ++i) {
        sumL += m_ringL[(v.base + i) & (kRingSamples - 1)];
        sumR += m_ringR[(v.base + i) & (kRingSamples - 1)];
    }
    v.meanL = static_cast<float>(sumL) / static_cast<float>(window);
    v.meanR = static_cast<float>(sumR) / static_cast<float>(window);
    return true;
}

void WaveformPipeline::getWaveform(int16_t* leftWave, int16_t* rightWave, size_t count, bool trigger,
                                   size_t spanSamples) const {
    if (!leftWave && !rightWave) return;
    if (count == 0) return;

    // The newest `span` samples, DC removed with the mean of a window at least
    // kLevelSamples long (as the VU meters always had).
    size_t span = spanSamples ? spanSamples : kLevelSamples;
    View v;
    if (!view(span > kLevelSamples ? span : kLevelSamples, v)) return;
    size_t windowSamples = v.window;
    if (span > windowSamples) span = windowSamples;
    size_t base = windowSamples - span;

    size_t start = base;
    size_t shown = span;
    if (trigger && span >= 8) {
        // Look for a rising zero crossing in the first quarter of the span.
        // Hysteresis: the signal must first dip below -hyst before a rise
        // through zero counts, so noise around zero can't trigger.
        size_t search = span / 4;
        shown = span - search;

        int32_t peak = 0;
        for (size_t i = base; i < windowSamples; ++i) {
            int32_t x = static_cast<int32_t>(v.l(i)) + v.r(i);
            if (x < 0) x = -x;
            if (x > peak) peak = x;
        }
        int32_t hyst  = (peak / 8 > 16) ? peak / 8 : 16;
        bool    armed = false;
        for (size_t i = base; i < base + search; ++i) {
            int32_t x = static_cast<int32_t>(v.l(i)) + v.r(i);
            if (x < -hyst) {
                armed = true;
            } else if (armed && x >= 0) {
                start = i;
                break;
            }
        }
    }

    // `count` points over `shown` samples: linear interpolation, so a span
    // with fewer samples than points is stretched smoothly across the screen.
    size_t last = windowSamples - 1;
    float  step = (count > 1) ? static_cast<float>(shown - 1) / static_cast<float>(count - 1) : 0.0f;
    for (size_t i = 0; i < count; ++i) {
        float  pos  = static_cast<float>(start) + i * step;
        size_t i0   = static_cast<size_t>(pos);
        if (i0 > last) i0 = last;
        size_t i1   = (i0 < last) ? i0 + 1 : last;
        float  frac = pos - static_cast<float>(i0);
        int16_t l0 = v.l(i0), l1 = v.l(i1), r0 = v.r(i0), r1 = v.r(i1);
        if (leftWave)  leftWave[i]  = static_cast<int16_t>(l0 + (l1 - l0) * frac);
        if (rightWave) rightWave[i] = static_cast<int16_t>(r0 + (r1 - r0) * frac);
    }
}

void WaveformPipeline::getScopeMono(int16_t* out, size_t count, size_t spanSamples) const {
    if (!out || count == 0) return;

    // Its own fixed window (kLevelSamples), whatever the waveform's scale.
    View v;
    if (!view(kLevelSamples, v)) return;
    size_t windowSamples = v.window;

    size_t maxSpan = windowSamples - windowSamples / 4;
    if (spanSamples > maxSpan) spanSamples = maxSpan;
    if (spanSamples == 0) spanSamples = 1;
    size_t search = windowSamples - spanSamples; // where the trigger may sit

    // Rising zero crossing with hysteresis, as in getWaveform().
    int32_t peak = 0;
    for (size_t i = 0; i < windowSamples; ++i) {
        int32_t x = static_cast<int32_t>(v.l(i)) + v.r(i);
        if (x < 0) x = -x;
        if (x > peak) peak = x;
    }
    int32_t hyst  = (peak / 8 > 16) ? peak / 8 : 16;
    bool    armed = false;
    size_t  start = 0;
    for (size_t i = 0; i < search; ++i) {
        int32_t x = static_cast<int32_t>(v.l(i)) + v.r(i);
        if (x < -hyst) {
            armed = true;
        } else if (armed && x >= 0) {
            start = i;
            break;
        }
    }

    float stride = static_cast<float>(spanSamples) / static_cast<float>(count);
    for (size_t i = 0; i < count; ++i) {
        size_t idx = start + static_cast<size_t>(i * stride);
        if (idx >= windowSamples) idx = windowSamples - 1;
        out[i] = static_cast<int16_t>((static_cast<int32_t>(v.l(idx)) + v.r(idx)) / 2);
    }
}

// Core 1, every chunk: this chunk's min/max (DC removed with the window's
// mean) folded into the current column; every
// dawChunksForScale() chunks the column is stored in the ring. A new DAW scale
// clears the timeline first (it refills at the new speed).
void WaveformPipeline::accumulateEnvelope(const uint16_t* chunk, float meanL, float meanR) {
    constexpr size_t kChunkPerChannel = CAPTURE_CHUNK_SAMPLES / 2;
    if (m_dawScale != m_appliedDawScale) {
        m_appliedDawScale = m_dawScale;
        memset(m_envMinL, 0, sizeof(m_envMinL)); memset(m_envMaxL, 0, sizeof(m_envMaxL));
        memset(m_envMinR, 0, sizeof(m_envMinR)); memset(m_envMaxR, 0, sizeof(m_envMaxR));
        memset(m_envMinM, 0, sizeof(m_envMinM)); memset(m_envMaxM, 0, sizeof(m_envMaxM));
        m_accChunks = 0;
    }
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
    if (++m_accChunks < static_cast<int>(dawChunksForScale(m_appliedDawScale))) return;
    m_accChunks = 0;
    uint32_t col = m_envWrite;
    m_envMinL[col] = m_accMinL; m_envMaxL[col] = m_accMaxL;
    m_envMinR[col] = m_accMinR; m_envMaxR[col] = m_accMaxR;
    m_envMinM[col] = m_accMinM; m_envMaxM[col] = m_accMaxM;
    __dmb();
    m_envWrite = (col + 1 < kEnvCols) ? col + 1 : 0;   // wraps with the ring itself
}

// Core 0: the timeline, oldest column first. (Core 1 may store one column
// meanwhile -- at worst that column shows its newer value a frame early.)
void WaveformPipeline::getEnvelope(int16_t* minL, int16_t* maxL, int16_t* minR, int16_t* maxR,
                                   size_t count, bool mono) const {
    if (!minL || !maxL || count != static_cast<size_t>(kEnvCols)) return;
    uint32_t oldest = m_envWrite;   // the next slot to write = the oldest column
    __dmb();
    for (int i = 0; i < kEnvCols; ++i) {
        uint32_t col = (oldest + static_cast<uint32_t>(i)) % kEnvCols;
        if (mono) {
            minL[i] = m_envMinM[col]; maxL[i] = m_envMaxM[col];
        } else {
            minL[i] = m_envMinL[col]; maxL[i] = m_envMaxL[col];
            if (minR && maxR) { minR[i] = m_envMinR[col]; maxR[i] = m_envMaxR[col]; }
        }
    }
}
