#pragma once
#include <Arduino.h>

// Tempo (BPM) estimate from the audio input, shown on the Info screen.
//
//   core 1 (pushChunk, every capture chunk): the bass of the mono signal
//     (two one-pole low-passes at ~150 Hz) is squared and smoothed by an
//     envelope follower (~15 Hz); every two chunks (~172 Hz) its rise in log
//     terms is one "onset" value. Cheap: a few operations per sample.
//   core 0 (update, about once a second): autocorrelation of the last ~8 s
//     of onsets over lags for 60..180 BPM, each candidate also credited with
//     half the correlation at twice its period (a real beat repeats at both),
//     weighted gently towards 120 BPM; parabolic interpolation of the peak; a
//     median of the last few readings steadies the number, and a shown tempo
//     is kept unless another scores clearly (15%) better -- no flipping
//     between a tempo and its half.
//
// A reading counts only if the onsets are SPIKY (a beat: sharp jumps, little
// in between -- drones, pads and beating tones make a smooth ripple instead),
// not practically flat, and the periodicity is clear enough. A shown tempo is
// held through up to kHoldSeconds of uncertain readings (a break, a fill).
//
// bpm() is 0 when there's no usable beat. Bench (synthetic mixes with a
// continuous bassline, kick, snare, hats and a pad; a limited "loud master";
// bassline only; a sparse ballad): the right tempo every second; drones, pads
// with noise and silence: 0.
class BeatDetector {
public:
    static BeatDetector& instance();

    void pushChunk(const uint16_t* interleaved); // core 1: CAPTURE_CHUNK_SAMPLES interleaved L/R ADC samples
    void update();                               // core 0: call every loop; recomputes about once a second

    float bpm() const { return m_bpm; }          // 0 = unknown

private:
    BeatDetector() = default;

    static constexpr int   kEnvRateDiv  = 2;    // chunks per onset value (~172 Hz)
    static constexpr int   kHistory     = 1376; // ~8 s of onsets
    static constexpr int   kMedianN     = 5;
    static constexpr int   kHoldSeconds = 6;    // a shown tempo survives this many uncertain readings

    // core 1 state
    float    m_dcL = 2048.0f, m_dcR = 2048.0f;  // slow DC tracking of the ADC offset
    float    m_lp  = 0.0f, m_lp2 = 0.0f;        // the mono sample through two one-pole low-passes (~150 Hz)
    float    m_env1 = 0.0f, m_env2 = 0.0f;      // the bass power's envelope (two poles at ~15 Hz)
    int      m_chunkCount = 0;
    float    m_prevLogE   = 0.0f;
    float    m_onset[kHistory] = {};
    volatile uint32_t m_writeCount = 0;         // onsets written so far (index = count % kHistory)
    volatile float    m_level = 0.0f;           // recent bass energy (to detect silence)

    // core 0 state
    float    estimate();                        // one reading, 0 = no usable beat
    uint32_t m_lastUpdateMs = 0;
    int      m_misses = 0;
    float    m_recent[kMedianN] = {};
    int      m_recentCount = 0, m_recentPos = 0;
    float    m_bpm = 0.0f;
};
