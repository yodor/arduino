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
    enum class WindowMode : uint8_t { OSCILLOSCOPE, TRACK_PREVIEW };

    // ~11.6ms at 44.1kHz/channel -- about 5 cycles of a 440Hz tone. Shows
    // actual waveform SHAPE: individual cycles, useful for judging the
    // character of the source signal itself. The original, and only,
    // window size before this became a runtime choice.
    static constexpr size_t kOscilloscopeSamples = 512;

    // ~93ms at 44.1kHz/channel. At this span even a 440Hz tone completes
    // far more cycles than the display's fixed point count (WAVEFORM_
    // SAMPLES in Config.hpp) can resolve individually -- what you see
    // stops being cycle shape and becomes amplitude ENVELOPE over a
    // longer moment, closer to a DAW's track-thumbnail view than an
    // oscilloscope. Landed on 4096 rather than the originally-tried 8192
    // after comparing real hardware FPS between the two -- half the
    // per-chunk cost (see pushChunk()'s cost note in the .cpp) for
    // still-clearly-a-preview results.
    static constexpr size_t kTrackPreviewSamples = 4096;

    static constexpr size_t kMaxWindowSamples =
        (kOscilloscopeSamples > kTrackPreviewSamples) ? kOscilloscopeSamples : kTrackPreviewSamples;

    static_assert(kOscilloscopeSamples >= (CAPTURE_CHUNK_SAMPLES / 2),
                  "kOscilloscopeSamples must be at least CAPTURE_CHUNK_SAMPLES/2 (128) samples/channel -- "
                  "below that, pushChunk()'s memmove length underflows as an unsigned subtraction, "
                  "wrapping to a huge value and corrupting memory. Proven hard floor, not a guideline.");
    static_assert(kTrackPreviewSamples >= (CAPTURE_CHUNK_SAMPLES / 2),
                  "kTrackPreviewSamples must be at least CAPTURE_CHUNK_SAMPLES/2 (128) samples/channel -- "
                  "same underflow risk as kOscilloscopeSamples above.");
    static_assert(kMaxWindowSamples <= 16384,
                  "kMaxWindowSamples above 16384 starts eating a meaningful chunk of total SRAM just for "
                  "this pipeline's four buffers (8 bytes/sample-slot -- 128KB at this ceiling, on a "
                  "520KB RP2350). Sanity guard, not a proven hard limit -- raise deliberately, having "
                  "checked the actual free-RAM budget, not by accident.");

    // Feeds one physical capture chunk through the currently-active
    // window; picks up a pending WindowMode change (clearing and
    // resizing the active window) before processing if one occurred
    // since the last call.
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
    void getWaveform(int16_t* leftWave, int16_t* rightWave, size_t count, bool trigger = false) const;

    // A triggered MONO (L+R) trace of `spanSamples` samples (from a rising
    // zero crossing, as getWaveform(trigger=true)), decimated to `count`
    // points -- the same timescale whatever the window mode, for the Info
    // screen's small scope. spanSamples is capped at 3/4 of the window.
    void getScopeMono(int16_t* out, size_t count, size_t spanSamples) const;

    void setWindowMode(WindowMode mode) { m_windowMode = mode; }
    WindowMode getWindowMode() const    { return m_windowMode; }

private:
    void applyWindowModeChange();

    volatile WindowMode m_windowMode         = WindowMode::OSCILLOSCOPE;
    WindowMode          m_appliedWindowMode  = WindowMode::OSCILLOSCOPE;

    // How many of the kMaxWindowSamples slots below are actually part of
    // the active window right now -- kOscilloscopeSamples or
    // kTrackPreviewSamples depending on the applied mode. Not itself
    // `volatile`: it's read from core0 (getWaveform()) and written from
    // core1 (applyWindowModeChange()), but same reasoning as
    // SpectrumPipeline's m_activeBandCount -- a torn read here means at
    // worst a transiently-stale sample count for a frame or two, not a
    // memory-safety issue, since the backing arrays are always sized for
    // the larger of the two modes regardless of which is active.
    size_t m_activeWindowSamples = kOscilloscopeSamples;

    uint16_t m_slideL[kMaxWindowSamples] = {};
    uint16_t m_slideR[kMaxWindowSamples] = {};

    // Raw, DC-removed samples of the current window, for the screens --
    // DOUBLE-BUFFERED across the two cores. core1 rewrites the whole window
    // on every capture chunk (~2.9ms); with a single buffer, core0 copying
    // it at the same moment got half the old window and half the new one --
    // 128 samples apart -- which showed as a sudden step or jump somewhere
    // along the trace. Now core1 fills the back buffer and then publishes
    // it (m_readyBuf); core0 only ever reads the published one, which core1
    // won't touch again until the chunk after next (copying takes ~10us).
    int16_t m_waveL[2][kMaxWindowSamples] = {};
    int16_t m_waveR[2][kMaxWindowSamples] = {};
    size_t  m_waveSamples[2] = {kOscilloscopeSamples, kOscilloscopeSamples}; // window size each buffer was filled for
    volatile uint8_t m_readyBuf = 0; // last complete buffer (read by core0)
    uint8_t          m_writeBuf = 1; // buffer core1 fills next
};
