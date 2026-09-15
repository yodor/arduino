#pragma once
#include <Arduino.h>
#include "../lib/kissfft/kiss_fftr.h"

// Reusable real-FFT engine: sliding-window capture + DC removal + Hann
// windowing + FFT + magnitude computation, parameterized by window size
// at CONSTRUCTION time (a runtime parameter, not a C++ template) --
// Bar9Pipeline and OctavePipeline each own an INSTANCE of this SAME
// class, differing only in the size passed to their constructors (1024
// vs 8192), not in any code. A template would compile two separate
// copies of identical logic; this is genuinely one shared implementation,
// matching this project's established pattern of one runtime-
// parameterized engine reused across multiple call sites (see BarPhysics
// for the same idea applied to bar-drawing ballistics).
//
// Deliberately does NOT know about bar/band aggregation, noise gates, or
// bin-range tables -- those differ meaningfully between the two FFT-based
// screens and stay in Bar9Pipeline/OctavePipeline, which each own an
// instance of this engine and read its magnitude output.
//
// Does NOT own or know about DMA/ADC capture -- AudioEngine owns the
// physical capture and feeds every pipeline (including this engine, via
// its owning Pipeline) the same interleaved chunks as they arrive.
class FftEngine {
public:
    explicit FftEngine(size_t fftSize);
    ~FftEngine();

    // Not copyable -- owns raw malloc'd buffers and a kissfft config with
    // no defined copy semantics.
    FftEngine(const FftEngine&) = delete;
    FftEngine& operator=(const FftEngine&) = delete;

    // Feeds one physical capture chunk (CAPTURE_CHUNK_SAMPLES interleaved
    // samples, masked 12-bit ADC codes) into the sliding window. Returns
    // true if this call's accumulation crossed the window's 50% overlap
    // threshold and a fresh FFT + magnitude computation just ran --
    // meaning magnitudeBinsL/R() now reflect new data the caller should
    // re-aggregate from. Returns false most calls (the window is still
    // accumulating), in which case the caller has nothing new to do.
    bool pushChunk(const uint16_t* interleavedChunk);

    const float* magnitudeBinsL() const { return m_fftBinsL; }
    const float* magnitudeBinsR() const { return m_fftBinsR; }
    size_t       binCount()       const { return m_fftSize / 2; }

    uint32_t lastFftDurationUs() const { return m_lastFftUs; }
    float    observedRefreshHz() const { return m_refreshHz; }

private:
    void initHanningWindow();
    void runFft();

    size_t m_fftSize;
    size_t m_halfWindowThreshold; // m_fftSize/2 -- samples/channel needed to trigger a refresh (50% overlap)

    kiss_fftr_cfg    m_fftCfgL = nullptr;
    kiss_fft_scalar* m_fftInL  = nullptr;
    kiss_fft_cpx*    m_fftOutL = nullptr;
    kiss_fftr_cfg    m_fftCfgR = nullptr;
    kiss_fft_scalar* m_fftInR  = nullptr;
    kiss_fft_cpx*    m_fftOutR = nullptr;

    float* m_fftBinsL     = nullptr;
    float* m_fftBinsR     = nullptr;
    float* m_hanningTable = nullptr;

    uint16_t* m_slideL = nullptr;
    uint16_t* m_slideR = nullptr;

    // Incoming chunks accumulate here rather than being folded into the
    // main sliding window immediately -- pushChunk() only does the (much
    // more expensive) shift+append into m_slideL/R once a FULL
    // m_halfWindowThreshold's worth has staged up, in ONE combined move,
    // right before the FFT that will actually consume it. Shifting the
    // main window on every small chunk (as an earlier version of this
    // did) meant most of that data got shifted again on the NEXT chunk
    // before any FFT ever read it -- for OCTAVE_FFT_SIZE specifically,
    // that was ~32 shifts of ~8064 elements per refresh cycle where one
    // shift of 4096 does the same job.
    uint16_t* m_stagingL = nullptr;
    uint16_t* m_stagingR = nullptr;
    size_t    m_stagedSamples = 0;

    uint32_t m_lastFftUs = 0;
    float    m_refreshHz = 0.0f;
    // Microsecond-resolution, not millis() -- at millis() resolution, an
    // ~11-12ms true interval rounds to either 11 or 12, and 1000/11 vs
    // 1000/12 report as 90.9Hz vs 83.3Hz for what's actually the same
    // real rate -- jitter from timer resolution, not a genuine rate
    // change.
    uint32_t m_lastRunUs = 0;
};