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

    // peakDrop is rounded to a whole pixel step (minimum 1), not left as a
    // fractional value. state.peak[] starts as an exact integer
    // (static_cast<float>(h)) every time it's reset by a rising bar, and
    // subtracting an exact-integer peakDrop keeps it an exact integer
    // through every subsequent decay step -- so truncating to uint16_t for
    // the drawn pixel row is always lossless. A fractional peakDrop breaks
    // that invariant: once decaying, the peak accumulates genuine
    // fractional remainders, and truncating a fractional float to a pixel
    // row is exactly the kind of thing that can drift by a pixel over many
    // frames from floating-point representation error -- a real, subtle
    // regression versus the original hardcoded peakDrop=2.0f, which was
    // always an exact integer and never had this problem.
    float rawPeakDrop = kBaselinePeakDrop * frameRatio;
    m_peakDrop = static_cast<uint16_t>(roundf(rawPeakDrop));
    if (m_peakDrop < 1) m_peakDrop = 1;

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

// THE CENTRALIZED ENGINE METHOD: Calculates ballistics updates uniformly
    // Maps a floating-point level input (0.0..1.0) into crisp integer pixel limits
void BarPhysics::updateState(BarPhysicsState& state, float currentVal, uint16_t maxPixelUnits, uint32_t now) const {
    // A. Process attack smoothing and decay tracking curves
    if (currentVal > state.decay) {
        state.decay = state.decay * m_smoothing + currentVal * (1.0f - m_smoothing);
    } else {
        state.decay = max(currentVal, state.decay - m_decayRate);
    }

    // B. Cast to a strict integer coordinate bounds step
    uint16_t integerUnits = static_cast<uint16_t>(state.decay * maxPixelUnits);
    uint16_t integerPeak  = static_cast<uint16_t>(state.peak);

    // C. peakHoldTime is a genuine wall-clock duration, compared directly
    // against millis() -- already frame-rate-independent by construction,
    // like decayRate/peakDrop/smoothing are NOT, so (unlike those three)
    // it needs no TARGET_FRAME_US-based rescale at all. Using the constant
    // directly here also guarantees this can never again silently
    // disagree with the public peakHoldTimeMs() getter, which reports the
    // same constant -- previously this recomputed a DIFFERENT, wrongly
    // hold time locally on every single call (a stray division using an
    // unrelated hardcoded 16666 baseline, inconsistent with this class's
    // own kBaselineFrameUs=16000), silently diverging from what the
    // getter reported and making the peak visibly hold longer than
    // intended at higher frame rates.
    if (integerUnits >= integerPeak) {
        integerPeak = integerUnits;
        state.peakTimer = now;
    } else if (now - state.peakTimer > kPeakHoldTimeMs) {
        if (integerPeak > m_peakDrop) integerPeak -= m_peakDrop;
        else integerPeak = 0;
        if (integerPeak < integerUnits) integerPeak = integerUnits;
    }

    // D. Save integer coordinates losslessly back into the state float store
    state.peak = static_cast<float>(integerPeak);
}