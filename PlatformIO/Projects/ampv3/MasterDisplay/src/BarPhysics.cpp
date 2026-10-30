#include "BarPhysics.hpp"
#include "Config.hpp"
#include <cmath>



BarPhysics& BarPhysics::instance() {
    static BarPhysics inst;
    return inst;
}

BarPhysics::BarPhysics() {
    float frameRatio = static_cast<float>(TARGET_FRAME_US) / static_cast<float>(kBaselineFrameUs);

    // Linear per-frame steps: each represents a fall SPEED (fraction of
    // maxBarHeight per second, or pixels per second), so scaling the
    // per-frame step directly by the frame-period ratio preserves that
    // same real-time speed regardless of how many frames/sec actually
    // occur -- half the frame period means half the per-frame step, taken
    // twice as often, netting the same speed.
    m_decayRate = kBaselineDecayRate * frameRatio;
    if (m_decayRate < kMinDecayRate) m_decayRate = kMinDecayRate;


    // smoothing is a per-frame EXPONENTIAL blend factor, not a linear
    // step -- a straight ratio rescale (like decayRate above) would be
    // wrong here, since the relationship between a per-step blend factor
    // and real-time convergence speed is exponential, not linear.
    // Back-solve the underlying continuous-time constant (tau) implied by
    // the baseline value at the baseline frame rate, then recompute the
    // per-frame factor that reproduces that SAME tau at the actual frame
    // rate. This formula is naturally self-bounded (asymptotically
    // approaches but never reaches 1.0 as the frame period shrinks, never
    // goes negative), so unlike decayRate it needs no artificial floor.
    float tau = -static_cast<float>(kBaselineFrameUs) / logf(kBaselineSmoothing);
    m_smoothing = expf(-static_cast<float>(TARGET_FRAME_US) / tau);
}

// The peak speeds: hold, then fall the whole bar height in fallS seconds,
// accelerating from rest (gravity g = 2 / fallS^2 of full height per s^2).
// Timed in seconds, so the same at any frame rate and any bar height.
namespace {
struct PeakSpeed { uint32_t holdMs; float fallS; };
constexpr PeakSpeed kPeakSpeeds[] = {
    { 1500, 6.0f },   // TURTLE
    { 1000, 3.5f },   // SLOW
    {  600, 2.0f },   // NORMAL
    {  300, 1.1f },   // FAST
    {  120, 0.5f },   // CHEETAH
};
const PeakSpeed& peakSpeed(PeakDecaySpeed s) {
    size_t i = static_cast<size_t>(s);
    return kPeakSpeeds[i < sizeof(kPeakSpeeds) / sizeof(kPeakSpeeds[0]) ? i : 2];
}
} // namespace

void BarPhysics::setPeakDecaySpeed(PeakDecaySpeed speed) { m_peakDecaySpeed = speed; }
uint32_t BarPhysics::peakHoldTimeMs() const { return peakSpeed(m_peakDecaySpeed).holdMs; }

// THE CENTRALIZED ENGINE METHOD: Calculates ballistics updates uniformly
    // Maps a floating-point level input (0.0..1.0) into crisp integer pixel limits
void BarPhysics::updateState(BarPhysicsState& state, float currentVal, uint16_t maxPixelUnits, uint32_t now) const {
    // A. Process attack smoothing and decay tracking curves
    if (currentVal > state.decay) {
        state.decay = state.decay * m_smoothing + currentVal * (1.0f - m_smoothing);
    } else {
        state.decay = max(currentVal, state.decay - m_decayRate);
    }

    // B. The bar in whole pixel units.
    uint16_t integerUnits = static_cast<uint16_t>(state.decay * maxPixelUnits);

    // C. The peak: jumps up with the bar; after its hold (wall-clock) it falls
    // with constant acceleration, never below the bar. Kept fractional, so
    // even the slowest speed moves smoothly at 100 fps.
    const PeakSpeed& sp = peakSpeed(m_peakDecaySpeed);
    if (integerUnits >= state.peak) {
        state.peak      = integerUnits;
        state.peakVel   = 0.0f;
        state.peakTimer = now;
    } else if (now - state.peakTimer > sp.holdMs) {
        constexpr float dt = TARGET_FRAME_US / 1e6f;                  // one frame
        float g = 2.0f * maxPixelUnits / (sp.fallS * sp.fallS);       // units per s^2
        state.peakVel += g * dt;
        state.peak    -= state.peakVel * dt;
        if (state.peak < integerUnits) { state.peak = integerUnits; state.peakVel = 0.0f; }
    }
}