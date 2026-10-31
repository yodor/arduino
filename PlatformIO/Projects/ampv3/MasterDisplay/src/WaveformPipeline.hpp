#pragma once
#include <Arduino.h>
#include "Config.hpp"

// Raw waveform sliding window -- deliberately NOT built on FftEngine,
// since there's no spectral processing here at all. Refreshed on every
// single physical capture chunk for maximum responsiveness, genuinely
// independent of either FFT pipeline's size or cadence.
//
// Supports two window sizes, selectable at runtime via the menu (Theme ->
// Wave -> Window Mode): Oscilloscope and Track Preview -- see each
// constant's own comment below for what each actually looks like and
// why. Mode changes are set from core0 (menu) but applied on core1
// (inside pushChunk()) -- the same `volatile`-field pattern as
// SpectrumPipeline's Band Layout/Energy Mode, for the same reason:
// single-word reads/writes are naturally atomic on this platform, so no
// locking is needed, but a mode change can take up to one pushChunk()
// call to actually apply, and getWaveform() may very briefly (a frame or
// two, right at the moment of switching) read a window that isn't fully
// repopulated for its new size yet -- harmless given the array is always
// sized for the larger of the two modes, and mode switches are rare,
// deliberate, user-initiated events.
class WaveformPipeline {
public:
    // OSCILLOSCOPE: the newest samples at the chosen scale, triggered.
    // DAW: a scrolling ~3 s min/max timeline (getEnvelope()), kept in every
    // mode so it's full the moment the mode is chosen.
    enum class WindowMode : uint8_t { OSCILLOSCOPE, DAW };

    // The oscilloscope's scale (Theme > Wave > Scale): how many of the most
    // recent samples one screen shows -- 256 (~5.8 ms) doubling per step up
    // to kMaxWindowSamples (8192, ~186 ms). The longest scales show the
    // envelope rather than single cycles (what "Track Preview" used to be).
    static constexpr size_t  kScopeFirstSamples = 256;  // the shortest scale (~5.8 ms)
    static constexpr uint8_t kScopeScaleSteps  = 6;     // 256, 512, 1024, 2048, 4096, 8192
    static constexpr uint8_t kScopeScaleDefault = 3;    // 2048 (~46 ms)

    // The DAW timeline's scale (the same Theme > Wave > Scale item while DAW
    // is the mode): capture chunks per timeline column, 2 doubling to 32 --
    // ~1.2 s to ~20 s across the screen at 44.1 kHz.
    static constexpr uint8_t kDawScaleSteps   = 5;      // 2, 4, 8, 16, 32 chunks per column
    static constexpr uint8_t kDawScaleDefault = 1;      // 4 chunks per column (~2.5 s)
    static_assert(kDawScaleSteps == 5, "kZoomSteps counts the DAW's steps");
    static constexpr uint32_t dawChunksForScale(uint8_t step) { return 2u << (step < kDawScaleSteps ? step : kDawScaleSteps - 1); }
    static constexpr size_t  scopeSamplesForScale(uint8_t step) { return kScopeFirstSamples << (step < kScopeScaleSteps ? step : kScopeScaleSteps - 1); }

    // The VU meters' RMS window (and the Info screen's mini scope) -- fixed,
    // whatever the scale: the captured window is never shorter than this.
    static constexpr size_t kLevelSamples = 512;

    static constexpr size_t kMaxWindowSamples = kScopeFirstSamples << (kScopeScaleSteps - 1);   // 8192

    // Theme > Wave > Scale is one zoom across both views: steps
    // 0..kScopeScaleSteps-1 are the oscilloscope (5.8 ms .. 186 ms), the rest
    // the DAW timeline (1.2 s .. 20 s across).
    static constexpr uint8_t kZoomSteps   = kScopeScaleSteps + 5;   // + the DAW's 5 steps
    static constexpr uint8_t kZoomDefault = 3;                      // 2048 samples, ~46 ms

    // The capture ring the readers take their samples from: core 1 only
    // appends each chunk (the same small cost at any scale), core 0 reads
    // the newest samples it needs when it draws -- never older than the last
    // chunk. Twice the longest window, so while core 0 reads one window core
    // 1 can append ~185 ms more before reaching it. Power of two (masking).
    static constexpr size_t kRingSamples = 2 * kMaxWindowSamples;          // 16384 per channel, 64 KB
    static_assert((kRingSamples & (kRingSamples - 1)) == 0, "kRingSamples must be a power of two");

    void pushChunk(const uint16_t* interleavedChunk);

    // Decimates the latest window to `count` points. With trigger=true the
    // output starts at the first rising zero crossing (of L+R, with some
    // hysteresis against noise) found in the window's first quarter, and
    // spans the remaining three quarters -- like an oscilloscope's trigger,
    // successive frames then start at the same point of the waveform, so a
    // steady tone stands still instead of rolling, and the moment where the
    // panel shows half the new frame and half the old one no longer shows a
    // seam. Without a crossing (silence, very low notes) it free-runs from
    // the window start, with the same span so the scale doesn't jump.
    // `count` points from the newest `spanSamples` of the window (0: the
    // kLevelSamples the VU meters use), resampled -- interpolated when the
    // span has fewer samples than points, so a short scale still fills the
    // screen. trigger: start at a rising zero crossing (oscilloscope).
    void getWaveform(int16_t* leftWave, int16_t* rightWave, size_t count, bool trigger = false,
                     size_t spanSamples = 0) const;

    // A triggered MONO (L+R) trace of `spanSamples` samples (from a rising
    // zero crossing, as getWaveform(trigger=true)), decimated to `count`
    // points -- the same timescale whatever the window mode, for the Info
    // screen's small scope. spanSamples is capped at 3/4 of the window.
    void getScopeMono(int16_t* out, size_t count, size_t spanSamples) const;

    // The DAW timeline: kEnvCols columns, oldest first, newest last, each
    // the true minimum and maximum of every sample in its stretch of time
    // (dawChunksForScale() capture chunks, ~12 ms at the default) -- every peak
    // and transient counts (no decimation, no aliasing), and each column
    // spans several cycles, so music reads as a solid envelope. mono = the
    // L+R mix, into minL/maxL (minR/maxR may be null). count must be kEnvCols.
    static constexpr int kEnvCols = WAVEFORM_SAMPLES;
    void getEnvelope(int16_t* minL, int16_t* maxL, int16_t* minR, int16_t* maxR,
                     size_t count, bool mono) const;

    void setWindowMode(WindowMode mode) { m_windowMode = mode; }
    WindowMode getWindowMode() const    { return m_windowMode; }
    void    setScopeScale(uint8_t step) { m_scopeScale = step < kScopeScaleSteps ? step : kScopeScaleSteps - 1; }
    uint8_t getScopeScale() const       { return m_scopeScale; }
    // A new DAW scale also clears the timeline (applied on core 1), so old
    // columns at the previous speed don't sit next to new ones.
    void    setDawScale(uint8_t step)   { m_dawScale = step < kDawScaleSteps ? step : kDawScaleSteps - 1; }
    uint8_t getDawScale() const         { return m_dawScale; }

    // The one zoom (Theme > Wave > Scale): sets the view and its scale.
    void setZoom(uint8_t step) {
        if (step >= kZoomSteps) step = kZoomSteps - 1;
        if (step < kScopeScaleSteps) { setScopeScale(step); setWindowMode(WindowMode::OSCILLOSCOPE); }
        else                         { setDawScale(step - kScopeScaleSteps); setWindowMode(WindowMode::DAW); }
    }
    uint8_t getZoom() const {
        return m_windowMode == WindowMode::DAW ? static_cast<uint8_t>(kScopeScaleSteps + m_dawScale) : m_scopeScale;
    }

private:
    volatile WindowMode m_windowMode         = WindowMode::OSCILLOSCOPE;
    volatile uint8_t    m_scopeScale         = kScopeScaleDefault;

    // The capture ring (raw 12-bit samples) and how many samples have been
    // written in all (the newest is m_written - 1). Core 1 writes, core 0 reads.
    uint16_t          m_ringL[kRingSamples] = {};
    uint16_t          m_ringR[kRingSamples] = {};
    volatile uint32_t m_written = 0;
    // The DC level over the newest kLevelSamples, kept running on core 1 for
    // the DAW timeline (sums of the raw samples).
    uint32_t          m_dcSumL = 0, m_dcSumR = 0;

    // A reader's view of the ring: the newest `window` samples, DC removed
    // with that window's own mean (computed in begin()).
    struct View {
        const WaveformPipeline* p;
        uint32_t base;            // ring index of the window's first sample (unmasked)
        size_t   window;
        float    meanL, meanR;
        int16_t  l(size_t i) const { return static_cast<int16_t>(static_cast<float>(p->m_ringL[(base + i) & (kRingSamples - 1)]) - meanL); }
        int16_t  r(size_t i) const { return static_cast<int16_t>(static_cast<float>(p->m_ringR[(base + i) & (kRingSamples - 1)]) - meanR); }
    };
    bool view(size_t window, View& v) const;

    int16_t m_envMinL[kEnvCols] = {}, m_envMaxL[kEnvCols] = {};
    int16_t m_envMinR[kEnvCols] = {}, m_envMaxR[kEnvCols] = {};
    int16_t m_envMinM[kEnvCols] = {}, m_envMaxM[kEnvCols] = {};
    volatile uint32_t m_envWrite = 0;              // the next column to write (= the oldest); wraps at kEnvCols
    int16_t m_accMinL = 0, m_accMaxL = 0, m_accMinR = 0, m_accMaxR = 0, m_accMinM = 0, m_accMaxM = 0;
    int     m_accChunks = 0;
    volatile uint8_t m_dawScale        = kDawScaleDefault;
    uint8_t          m_appliedDawScale = kDawScaleDefault;
    void accumulateEnvelope(const uint16_t* interleavedChunk, float meanL, float meanR);

};
