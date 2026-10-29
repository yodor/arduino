#include "BeatDetector.hpp"
#include "FastMath.hpp"
#include "Config.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>

BeatDetector& BeatDetector::instance() {
    static BeatDetector inst;
    return inst;
}

void BeatDetector::pushChunk(const uint16_t* interleaved) {
    // One-pole low-pass at ~200 Hz on the mono signal (44.1 kHz per channel).
    constexpr float kLpA = 0.0211f;   // 1 - exp(-2*pi*150/44100), two poles
    constexpr float kDcA = 0.0005f;   // very slow DC tracker
    constexpr float kEnvA = 0.00214f; // 1 - exp(-2*pi*15/44100)
    for (int i = 0; i < CAPTURE_CHUNK_SAMPLES / 2; ++i) {
        float l = static_cast<float>(interleaved[2 * i] & 0x0FFF);
        float r = static_cast<float>(interleaved[2 * i + 1] & 0x0FFF);
        m_dcL += kDcA * (l - m_dcL);
        m_dcR += kDcA * (r - m_dcR);
        float mono = 0.5f * ((l - m_dcL) + (r - m_dcR));
        m_lp  += kLpA * (mono - m_lp);
        m_lp2 += kLpA * (m_lp - m_lp2);
        // Envelope follower on the bass power (two poles at ~15 Hz): keeps a
        // kick's attack but removes the ripple a sustained bass note's own
        // waveform would otherwise leave -- which looked like a tempo.
        float p = m_lp2 * m_lp2;
        m_env1 += kEnvA * (p - m_env1);
        m_env2 += kEnvA * (m_env1 - m_env2);
    }
    if (++m_chunkCount < kEnvRateDiv) return;

    // One onset value per kEnvRateDiv chunks: the rise in log bass energy.
    float e    = m_env2;
    float logE = logf(e + 1.0f);
    float onset = logE - m_prevLogE;
    m_prevLogE  = logE;
    m_onset[m_writeCount % kHistory] = onset > 0.0f ? onset : 0.0f;
    m_level = 0.98f * m_level + 0.02f * e;
    m_writeCount = m_writeCount + 1;
    m_chunkCount = 0;
}

void BeatDetector::update() {
    uint32_t now = millis();
    if (now - m_lastUpdateMs < 1000) return;
    m_lastUpdateMs = now;

    float est = estimate();
    if (est > 0.0f) {
        m_misses = 0;
        // Median of the last few good estimates.
        m_recent[m_recentPos] = est;
        m_recentPos = (m_recentPos + 1) % kMedianN;
        if (m_recentCount < kMedianN) ++m_recentCount;
        if (m_recentCount < 2) return; // show a tempo once two readings exist
        float sorted[kMedianN];
        memcpy(sorted, m_recent, sizeof(sorted));
        for (int i = 1; i < m_recentCount; ++i) {
            float v = sorted[i];
            int j = i - 1;
            while (j >= 0 && sorted[j] > v) { sorted[j + 1] = sorted[j]; --j; }
            sorted[j + 1] = v;
        }
        m_bpm = sorted[m_recentCount / 2];
    } else if (++m_misses >= kHoldSeconds) {
        // Several seconds without a usable beat: let it go.
        m_bpm = 0.0f;
        m_recentCount = 0;
        m_recentPos   = 0;
    }
    // Otherwise keep showing the last tempo through a brief uncertain spell
    // (a break, a fill, a quiet bar).
}

// One tempo reading from the last ~8 s of onsets, or 0 if there's no usable beat.
float BeatDetector::estimate() {
    uint32_t count = m_writeCount;
    if (count < static_cast<uint32_t>(kHistory)) return 0.0f; // not 8 s of audio yet

    // Quiet input (bass RMS under ~3 ADC counts): no beat to find.
    if (m_level < 9.0f) return 0.0f;

    // Snapshot the history, oldest first, minus its mean. (Core 1 may write
    // one value meanwhile -- harmless here.)
    static float x[kHistory];
    static float tmp[kHistory];
    float mean = 0.0f;
    for (int i = 0; i < kHistory; ++i) {
        x[i]   = m_onset[(count + i) % kHistory];
        tmp[i] = x[i];
        mean  += x[i];
    }
    mean /= kHistory;

    // Spikiness: the top 5% of onset values against the average. A beat
    // makes sharp jumps a few samples long and little in between (bench:
    // 7.6-10.2 on full mixes, compressed masters and bassline-only tracks);
    // drones, pads and tones beating against each other make a smooth
    // ripple (bench: 3.2-4.4). A ratio, so independent of the level.
    if (mean <= 1e-9f) return 0.0f;
    const int top = kHistory / 20;
    std::nth_element(tmp, tmp + (kHistory - top), tmp + kHistory);
    float topSum = 0.0f;
    for (int i = kHistory - top; i < kHistory; ++i) topSum += tmp[i];
    if ((topSum / top) / mean < 6.0f) return 0.0f;

    // A light [1 2 1]/4 blur: a beat period that isn't a whole number of
    // onset samples (160 BPM = 64.6) otherwise splits its correlation peak
    // over two lags, each weaker than the unsplit two-beat period -- and the
    // detector read half the tempo.
    {
        float prev = x[0];
        for (int i = 1; i < kHistory - 1; ++i) {
            float cur = x[i];
            x[i] = 0.25f * prev + 0.5f * cur + 0.25f * x[i + 1];
            prev = cur;
        }
    }
    float r0 = 0.0f;
    for (int i = 0; i < kHistory; ++i) { x[i] -= mean; r0 += x[i] * x[i]; }
    // Practically flat onsets: nothing to correlate. (Bench: real music 0.05-0.09.)
    if (fastSqrt(r0 / kHistory) < 0.02f) return 0.0f;

    // Autocorrelation over the lags for 60..180 BPM -- and their doubles, for
    // the harmonic check below.
    const float fs = AUDIO_SAMPLE_RATE_HZ * 2.0f / CAPTURE_CHUNK_SAMPLES / kEnvRateDiv; // onset rate, ~172 Hz
    const int lagMin = static_cast<int>(fs * 60.0f / 180.0f);
    const int lagMax = static_cast<int>(fs * 60.0f / 60.0f) + 1;
    static float r[2 * 180 + 4];
    if (2 * (lagMax + 1) >= static_cast<int>(sizeof(r) / sizeof(r[0]))) return 0.0f;
    for (int lag = lagMin - 1; lag <= 2 * (lagMax + 1); ++lag) {
        float acc = 0.0f;
        for (int i = lag; i < kHistory; ++i) acc += x[i] * x[i - lag];
        r[lag] = acc / r0;
    }

    // A tempo scores its own correlation plus half that at twice its period
    // (a real beat repeats at both), weighted gently towards ~120 BPM.
    static float score[180 + 2];
    int   best = -1;
    float bestScore = 0.0f;
    for (int lag = lagMin; lag <= lagMax; ++lag) {
        float bpm = 60.0f * fs / lag;
        float oct = log2f(bpm / 120.0f);
        float w   = expf(-0.5f * (oct / 0.9f) * (oct / 0.9f)); // a gentle preference for ~80..160 BPM
        score[lag] = (r[lag] + 0.5f * r[2 * lag]) * w;
        if (score[lag] > bestScore) { bestScore = score[lag]; best = lag; }
    }

    // Stickiness: while a tempo is shown, keep it unless another one scores
    // clearly (15%) better -- a borderline call between a tempo and its half
    // or 2/3 must not make the display flip back and forth.
    if (m_bpm > 0.0f && best >= 0) {
        int cur = static_cast<int>(lroundf(60.0f * fs / m_bpm));
        int curBest = -1;
        for (int lag = cur - 1; lag <= cur + 1; ++lag) {
            if (lag >= lagMin && lag <= lagMax && (curBest < 0 || score[lag] > score[curBest])) curBest = lag;
        }
        if (curBest >= 0 && score[curBest] >= 0.85f * bestScore) best = curBest;
    }
    // Too weak a periodicity to trust (bench: real music 0.47-0.65).
    if (best < 0 || r[best] < 0.25f) return 0.0f;

    // Parabolic interpolation around the peak for sub-lag precision.
    float y0 = r[best - 1], y1 = r[best], y2 = r[best + 1];
    float denom = y0 - 2.0f * y1 + y2;
    float delta = (fabsf(denom) > 1e-9f) ? 0.5f * (y0 - y2) / denom : 0.0f;
    if (delta > 0.5f) delta = 0.5f;
    if (delta < -0.5f) delta = -0.5f;
    return 60.0f * fs / (best + delta);
}
