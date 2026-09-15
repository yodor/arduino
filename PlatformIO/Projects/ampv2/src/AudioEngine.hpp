#pragma once

#include "Config.hpp"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "Bar9Pipeline.hpp"
#include "OctavePipeline.hpp"
#include "WaveformPipeline.hpp"

// Owns the physical ADC/DMA capture (the one piece of hardware every
// pipeline shares) and three independent pipeline objects, each with its
// own window size and refresh cadence. Every physical capture chunk gets
// handed to all three; each decides internally what to do with it (see
// Bar9Pipeline/OctavePipeline/WaveformPipeline). No FFT/aggregation logic
// lives here anymore -- this class is purely capture + dispatch.
class AudioEngine {
public:
    static AudioEngine& instance();

    void runCore1();
    void getSpectrumBars(float* leftBars, float* rightBars) const;
    void getOctaveBars(float* leftBars, float* rightBars) const;
    void getWaveform(int16_t* leftWave, int16_t* rightWave, size_t count) const;

private:
    AudioEngine() = default;

    // Aligned to its own size (CAPTURE_CHUNK_SAMPLES * 2 bytes) -- required
    // for the DMA ring-wrap addressing used to capture into this buffer
    // continuously (see runCore1()): the ring hardware wraps by masking
    // low address bits, which only works correctly if the buffer actually
    // starts on that same boundary.
    alignas(CAPTURE_CHUNK_SAMPLES * sizeof(uint16_t)) uint16_t m_audioBuffer[2][CAPTURE_CHUNK_SAMPLES] = {};

    Bar9Pipeline     m_bar9;
    OctavePipeline   m_octave;
    WaveformPipeline m_waveform;
};