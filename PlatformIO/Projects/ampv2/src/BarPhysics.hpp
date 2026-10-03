#pragma once
#include <Arduino.h>

struct BarPhysicsState {
    float    decay         = 0.0f;
    float    peak          = 0.0f;
    uint32_t peakTimer     = 0;
    uint16_t prevUnits     = 0; // Merges prevBarHeight and prevBarWidth
    uint16_t prevPeakUnits = 0; // Merges prevPeakHeight and prevPeakWidth
};

// Frame-rate-independent audio-reactive bar physics (attack smoothing,
// release/decay speed, peak-hold drop speed), shared by every bar-style
// screen (SpectrumScreen, DigitalVuMeterScreen) instead
// of each hardcoding its own separate copy.
//
// The three baseline constants below were tuned by ear at 60fps
// (TARGET_FRAME_US == 16000). If TARGET_FRAME_US changes (e.g. to run at
// ~90fps), reusing the SAME raw per-frame step sizes would silently change
// how fast bars visually attack/decay/drop in real time -- more frames per
// second means the same per-frame step accumulates faster in wall-clock
// terms. This class rescales the baseline values once at startup so the
// real-time feel stays the same regardless of TARGET_FRAME_US.
//
// peakHoldTime is NOT rescaled: it's already a wall-clock duration
// compared against millis(), not a per-frame step, so it's already
// frame-rate-independent by construction (see AnalogVuScreen's ballistics
// for the same reasoning applied to needle movement).
// Menu-selectable multiplier on peak-hold drop speed (Theme -> Peaks ->
// Decay Speed) -- NORMAL matches the original, by-ear-tuned feel; SLOW/
// FAST are multipliers applied on top of it, not independently-tuned
// values. Purely a core0-side setting (menu and screen render() both run
// on core0), unlike the audio-pipeline settings in SpectrumPipeline/
// WaveformPipeline -- no `volatile`/cross-core handling needed here.
//
// OFF is a genuinely different mode, not just a slower multiplier: once a
// peak is set, it never falls on its own at all -- only a NEW, higher bar
// value raises it further (a true "peak hold forever" meter mode, as
// distinct from "peak hold then decay"). Appended after FAST rather than
// inserted first so SLOW/NORMAL/FAST keep their existing persisted
// ordinal values (0/1/2) -- OFF=3 is purely additive, so this didn't need
// a PersistentData version bump the way adding a new field would have.
enum class PeakDecaySpeed : uint8_t { SLOW, NORMAL, FAST, OFF };

class BarPhysics {
public:
    static BarPhysics& instance();

    float    smoothing() const      { return m_smoothing; }
    float    decayRate() const      { return m_decayRate; }
    uint16_t peakDrop() const       { return m_peakDrop; }
    uint32_t peakHoldTimeMs() const { return kPeakHoldTimeMs; }

    void setPeakDecaySpeed(PeakDecaySpeed speed);
    PeakDecaySpeed getPeakDecaySpeed() const { return m_peakDecaySpeed; }

    // THE CENTRALIZED ENGINE METHOD: Calculates ballistics updates uniformly
    // Maps a floating-point level input (0.0..1.0) into crisp integer pixel limits
    void updateState(BarPhysicsState& state, float currentVal, uint16_t maxPixelUnits, uint32_t now) const;

private:
    BarPhysics();
    void recomputePeakDrop();

    // Baseline values, tuned by ear at 60fps. Retune the underlying feel
    // here -- this is the one place it lives now, instead of three
    // separate copies scattered across screen files.
    static constexpr float    kBaselineSmoothing = 0.35f; // EMA blend factor per frame at 60fps
    static constexpr float    kBaselineDecayRate = 0.08f; // linear per-frame decay (fraction of maxBarHeight) at 60fps
    static constexpr float    kBaselinePeakDrop  = 2.0f;  // linear per-frame peak-fall (pixels) at 60fps
    static constexpr uint32_t kBaselineFrameUs   = 16000; // the frame period the baseline values above were tuned against
    static constexpr uint32_t kPeakHoldTimeMs    = 200;   // wall-clock, not rescaled -- see class comment

    // Defensive floor on decayRate for very high frame rates (well beyond
    // anything tested so far) -- without it, enough TARGET_FRAME_US
    // reduction could shrink the per-frame step below what changes h's
    // truncated integer value most frames, making decay look like it
    // "sticks" for a frame then jumps, rather than moving smoothly. Never
    // engages at any frame rate tested so far.
    static constexpr float    kMinDecayRate      = 0.04f;

    float    m_smoothing;
    float    m_decayRate;
    uint16_t m_peakDrop;
    PeakDecaySpeed m_peakDecaySpeed = PeakDecaySpeed::NORMAL;
};