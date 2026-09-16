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
    void getWaveform(int16_t* leftWave, int16_t* rightWave, size_t count) const;

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
    alignas(CAPTURE_CHUNK_SAMPLES * sizeof(uint16_t)) uint16_t m_audioBuffer[2][CAPTURE_CHUNK_SAMPLES] = {};

    SpectrumPipeline m_spectrum;
    WaveformPipeline m_waveform;
};