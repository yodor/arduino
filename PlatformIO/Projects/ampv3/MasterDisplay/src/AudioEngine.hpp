#pragma once

#include "Config.hpp"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "SpectrumPipeline.hpp"
#include "WaveformPipeline.hpp"

// Owns the physical ADC/DMA capture (the one piece of hardware every
// pipeline shares) and two independent pipeline objects, each with its
// own window size and refresh cadence. Every physical capture chunk gets
// handed to both; each decides internally what to do with it (see
// SpectrumPipeline/WaveformPipeline). No FFT/aggregation logic lives here
// anymore -- this class is purely capture + dispatch.
class AudioEngine {
public:
    static AudioEngine& instance();

    void runCore1();
    void getSpectrumBars(float* leftBars, float* rightBars) const;
    size_t getSpectrumBandCount() const;
    // trigger: start at a rising zero crossing (oscilloscope trigger) -- see
    // WaveformPipeline::getWaveform(). The meters use the plain full window.
    void getWaveform(int16_t* leftWave, int16_t* rightWave, size_t count, bool trigger = false) const;
    // Triggered mono trace of a fixed span -- see WaveformPipeline::getScopeMono().
    void getScopeMono(int16_t* out, size_t count, size_t spanSamples) const;
    // DAW view: per-column min/max -- see WaveformPipeline::getEnvelope().
    void getEnvelope(int16_t* minL, int16_t* maxL, int16_t* minR, int16_t* maxR, size_t count, bool mono) const {
        m_waveform.getEnvelope(minL, maxL, minR, maxR, count, mono);
    }
    // Mono doubling-band bars -- see SpectrumPipeline::getMiniBars().
    size_t getMiniBars(float* out) const;

    // Menu-facing forwarders -- set from core0, applied on core1 inside
    // SpectrumPipeline::pushChunk() (see that class's comment for why this
    // is safe without locking on this platform).
    void setSpectrumBandLayoutMode(SpectrumPipeline::BandLayoutMode mode);
    void setSpectrumEnergyMode(SpectrumPipeline::EnergyMode mode);
    SpectrumPipeline::BandLayoutMode getSpectrumBandLayoutMode() const;
    SpectrumPipeline::EnergyMode     getSpectrumEnergyMode() const;

    // Same pattern, for WaveformPipeline's Window Mode setting.
    void setWaveformWindowMode(WaveformPipeline::WindowMode mode);
    WaveformPipeline::WindowMode getWaveformWindowMode() const;

private:
    AudioEngine() = default;

    // Aligned to its own size (CAPTURE_CHUNK_SAMPLES * 2 bytes) -- required
    // for the DMA ring-wrap addressing used to capture into this buffer
    // continuously (see runCore1()): the ring hardware wraps by masking
    // low address bits, which only works correctly if the buffer actually
    // starts on that same boundary.
    // One continuous capture ring, CAPTURE_RING_CHUNKS chunks long, which the
    // DMA fills nonstop (see runCore1()). Aligned to its own size -- required
    // by the DMA ring-wrap addressing, which wraps by masking address bits.
    static constexpr size_t kRingSamples = static_cast<size_t>(CAPTURE_CHUNK_SAMPLES) * CAPTURE_RING_CHUNKS;
    alignas(kRingSamples * sizeof(uint16_t)) uint16_t m_ring[kRingSamples] = {};

    SpectrumPipeline m_spectrum;
    WaveformPipeline m_waveform;
    // ADC rate: the divider in use, and the per-channel rate the start-up
    // self-calibration measured (0 until it has) -- see runCore1().
    float m_adcDiv = 544.0f;
    volatile float m_measuredRateHz = 0.0f;
public:
    float measuredSampleRateHz() const { return m_measuredRateHz; }
};
