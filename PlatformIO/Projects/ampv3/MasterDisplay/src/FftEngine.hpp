#pragma once
#include <Arduino.h>
#include "dsp/transform_functions.h"     // CMSIS-DSP (lib/CMSISDSP): arm_rfft_fast_f32
#include "dsp/complex_math_functions.h"  // arm_cmplx_mag_f32
#include "dsp/basic_math_functions.h"    // arm_scale_f32

// Reusable real-FFT engine: sliding-window capture + DC removal + Hann
// windowing + FFT + magnitude computation, parameterized by window size
// at CONSTRUCTION time (a runtime parameter, not a C++ template).
// SpectrumPipeline owns one instance of this class (sized via FFT_SIZE),
// matching this project's established pattern of one runtime-
// parameterized engine reused across call sites (see BarPhysics for the
// same idea applied to bar-drawing ballistics).
//
// Deliberately does NOT know about band aggregation, noise gates, or
// bin-range tables -- those live in SpectrumPipeline, which owns an
// instance of this engine and reads its magnitude output.
//
// Does NOT own or know about DMA/ADC capture -- AudioEngine owns the
// physical capture and feeds every pipeline (including this engine, via
// its owning Pipeline) the same interleaved chunks as they arrive.
class FftEngine {
public:
    explicit FftEngine(size_t fftSize);
    ~FftEngine();

    // Where the last FFT run's time went (diagnostics): the window shift,
    // DC removal + Hann windowing, the two transforms, the magnitudes. us.
    struct Stages { uint32_t shift = 0, window = 0, fft = 0, mags = 0; };
    const Stages& lastStages() const { return m_stages; }

    // Not copyable -- owns raw malloc'd buffers.
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

    // The FFT: ARM's CMSIS-DSP real FFT (arm_rfft_fast_f32), hand-optimised
    // for Cortex-M with an FPU. One instance serves both channels (it only
    // holds pointers to constant tables), and the two transforms share one
    // output buffer: each channel's magnitudes are taken before the next runs.
    arm_rfft_fast_instance_f32 m_rfft{};
    bool    m_rfftOk = false;
    float*  m_fftInL = nullptr;   // windowed input; the FFT also uses it as scratch
    float*  m_fftInR = nullptr;
    float*  m_fftOut = nullptr;   // packed real-FFT output, m_fftSize floats
    // RAM copies of the FFT's constant tables (see the constructor).
    float*    m_ramTwRfft = nullptr;
    float*    m_ramTwCfft = nullptr;
    uint16_t* m_ramBitRev = nullptr;

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
    // before any FFT ever read it -- the larger fftSize is relative to
    // CAPTURE_CHUNK_SAMPLES, the more redundant repeated-shifting this
    // avoids.
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
    Stages m_stages;
};
