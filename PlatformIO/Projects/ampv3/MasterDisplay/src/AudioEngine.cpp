#include "AudioEngine.hpp"
#include "BeatDetector.hpp"

namespace {
// Computes log2(n) for a power-of-two n at compile time -- used to derive
// the DMA ring-wrap size_bits parameter (channel_config_set_ring()) from
// CAPTURE_CHUNK_SAMPLES's byte size, which is always a power of two since
// it's required to be one.
constexpr uint32_t log2_constexpr(uint32_t n) {
    uint32_t bits = 0;
    while (n > 1) {
        n >>= 1;
        ++bits;
    }
    return bits;
}
} // namespace

AudioEngine& AudioEngine::instance() {
    static AudioEngine inst;
    return inst;
}

void AudioEngine::runCore1() {
    adc_init();
    adc_gpio_init(AUDIO_PIN_L); // ADC2 - GPIO28
    adc_gpio_init(AUDIO_PIN_R); // ADC1 - GPIO27

    // --- One-time ADC setup ("Hardware Phase Lock Fix") ---
    // Runs ONCE, not every cycle -- the ADC session never stops once
    // started below, so there's no "next cycle" needing a fresh phase
    // lock.
    adc_run(false);
    adc_fifo_drain();
    adc_select_input(2);        // Force starting input explicitly to ADC2 (Left)
    adc_set_round_robin(0x06);  // Alternates between ADC2 (L) and ADC1 (R)
    adc_fifo_setup(true, true, 1, false, false);
    adc_set_clkdiv(544.0f);     // ~44.1 kHz sampling per channel

    // --- Continuous ring capture ---
    // Two DMA channels, BOTH writing into the same ring buffer (wrapping
    // within it), chained to each other. Each runs for a whole number of
    // ring laps (kLapsPerChannel), so it ends exactly back at the ring start,
    // where the other one -- also pointed at the ring start -- carries on
    // without missing a sample. (Two channels only because a single channel's
    // transfer count eventually runs out; at 88k samples/s each one lasts
    // over three minutes.) The CPU never has to re-arm anything.
    int chanA = dma_claim_unused_channel(true);
    int chanB = dma_claim_unused_channel(true);

    constexpr uint32_t kRingBytes      = static_cast<uint32_t>(kRingSamples) * sizeof(uint16_t);
    constexpr uint32_t kRingSizeBits   = log2_constexpr(kRingBytes);
    constexpr uint32_t kLapsPerChannel = 4096;
    constexpr uint32_t kTransfers      = static_cast<uint32_t>(kRingSamples) * kLapsPerChannel;
    static_assert((1u << kRingSizeBits) == kRingBytes, "ring size must be a power of two");
    static_assert(kTransfers < (1u << 28), "transfer count must fit the DMA counter");

    auto configure = [&](int chan, int chainTo) {
        dma_channel_config c = dma_channel_get_default_config(chan);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
        channel_config_set_read_increment(&c, false);
        channel_config_set_write_increment(&c, true);
        channel_config_set_dreq(&c, DREQ_ADC);
        channel_config_set_ring(&c, true, kRingSizeBits);
        channel_config_set_chain_to(&c, chainTo);
        dma_channel_configure(chan, &c, m_ring, &adc_hw->fifo, kTransfers, false);
    };
    configure(chanA, chanB);
    configure(chanB, chanA);

    dma_channel_start(chanA);
    adc_run(true); // started ONCE here -- never stopped again during normal operation

    const uintptr_t ringBase = reinterpret_cast<uintptr_t>(m_ring);
    // Where the DMA will write next, in samples from the ring start. Read
    // from whichever channel is running; at a handoff both point at the ring
    // start, so either reading is right.
    auto writePos = [&]() -> uint32_t {
        int active = dma_channel_is_busy(chanA) ? chanA : chanB;
        uintptr_t addr = dma_channel_hw_addr(active)->write_addr;
        return static_cast<uint32_t>(((addr - ringBase) / sizeof(uint16_t)) % kRingSamples);
    };

    constexpr uint32_t kChunk = CAPTURE_CHUNK_SAMPLES;
    uint32_t readPos = 0; // next chunk to process (samples, always a multiple of kChunk)

    // Diagnostics (DEBUG_AUDIO_DIAGNOSTICS): per-chunk cost, deepest backlog,
    // overruns -- reported once a second.
    uint32_t chunkCount = 0, waveformAccumUs = 0, spectrumAccumUs = 0;
    uint32_t maxBacklogChunks = 0, overruns = 0, totalOverruns = 0;
    uint32_t lastReportMs = 0;

    uint32_t lastLoopUs = micros();
    while (true) {
        // A settings save pauses this core during flash writes (LittleFS
        // does it via rp2040.idleOtherCore()). After a long gap the DMA may
        // have lapped us, so skip to fresh audio rather than read
        // overwritten chunks.
        uint32_t nowUs = micros();
        if (nowUs - lastLoopUs > 10000) {
            readPos = ((writePos() / kChunk) * kChunk + kRingSamples - kChunk) % kRingSamples;
        }
        lastLoopUs = nowUs;

        uint32_t avail = (writePos() + kRingSamples - readPos) % kRingSamples;
        if (avail < kChunk) {
            tight_loop_contents(); // next chunk not complete yet
            continue;
        }

        // Fallen nearly a whole lap behind: the oldest chunks are about to be
        // overwritten under us. Jump to the newest complete chunk instead.
        if (avail > kRingSamples - 2 * kChunk) {
            readPos = ((writePos() / kChunk) * kChunk + kRingSamples - kChunk) % kRingSamples;
            ++overruns;
            ++totalOverruns;
            avail = kChunk;
        }
        uint32_t backlog = avail / kChunk;
        if (backlog > maxBacklogChunks) maxBacklogChunks = backlog;

        const uint16_t* chunk = &m_ring[readPos];

        uint32_t t0 = micros();
        m_waveform.pushChunk(chunk);
        uint32_t t1 = micros();
        m_spectrum.pushChunk(chunk);
        BeatDetector::instance().pushChunk(chunk); // a few operations per sample (tempo for the Info screen)
        uint32_t t2 = micros();
        waveformAccumUs += t1 - t0;
        spectrumAccumUs += t2 - t1;
        ++chunkCount;

        readPos = (readPos + kChunk) % kRingSamples;

        uint32_t nowMs = millis();
        if (nowMs - lastReportMs >= 1000) {
            lastReportMs = nowMs;
            if (DEBUG_AUDIO_DIAGNOSTICS && chunkCount > 0) {
                // ~344 chunks/s means every chunk was processed.
                uint16_t minL = 4095, maxL = 0, minR = 4095, maxR = 0;
                for (uint32_t i = 0; i < kChunk / 2; ++i) {
                    uint16_t vL = chunk[i * 2] & 0x0FFF, vR = chunk[i * 2 + 1] & 0x0FFF;
                    if (vL < minL) minL = vL;
                    if (vL > maxL) maxL = vL;
                    if (vR < minR) minR = vR;
                    if (vR > maxR) maxR = vR;
                }
                Serial.print("[Audio] ");
                Serial.print(chunkCount);
                Serial.print(" chunks/s | per chunk: waveform ");
                Serial.print(waveformAccumUs / 1000.0f / chunkCount, 3);
                Serial.print("ms, spectrum ");
                Serial.print(spectrumAccumUs / 1000.0f / chunkCount, 3);
                Serial.print("ms | fft ");
                Serial.print(m_spectrum.lastFftDurationUs() / 1000.0f, 2);
                Serial.print("ms @ ");
                Serial.print(m_spectrum.observedRefreshHz(), 1);
                Serial.print("Hz | max backlog ");
                Serial.print(maxBacklogChunks);
                Serial.print("/");
                Serial.print(static_cast<unsigned>(CAPTURE_RING_CHUNKS));
                Serial.print(" chunks | overruns ");
                Serial.print(overruns);
                Serial.print(" (total ");
                Serial.print(totalOverruns);
                Serial.print(") | raw pp L=");
                Serial.print(static_cast<unsigned>(maxL - minL));
                Serial.print(" R=");
                Serial.println(static_cast<unsigned>(maxR - minR));
            } else if (overruns > 0) {
                Serial.print("[Audio] Capture overrun: chunks skipped in the last second: ");
                Serial.println(overruns);
            }
            chunkCount = waveformAccumUs = spectrumAccumUs = 0;
            maxBacklogChunks = overruns = 0;
        }
    }
}

void AudioEngine::getSpectrumBars(float* leftBars, float* rightBars) const {
    m_spectrum.getBars(leftBars, rightBars);
}

size_t AudioEngine::getSpectrumBandCount() const {
    return m_spectrum.bandCount();
}

void AudioEngine::setSpectrumBandLayoutMode(SpectrumPipeline::BandLayoutMode mode) {
    m_spectrum.setBandLayoutMode(mode);
}

void AudioEngine::setSpectrumEnergyMode(SpectrumPipeline::EnergyMode mode) {
    m_spectrum.setEnergyMode(mode);
}

SpectrumPipeline::BandLayoutMode AudioEngine::getSpectrumBandLayoutMode() const {
    return m_spectrum.getBandLayoutMode();
}

SpectrumPipeline::EnergyMode AudioEngine::getSpectrumEnergyMode() const {
    return m_spectrum.getEnergyMode();
}

size_t AudioEngine::getMiniBars(float* out) const {
    return m_spectrum.getMiniBars(out);
}

void AudioEngine::getScopeMono(int16_t* out, size_t count, size_t spanSamples) const {
    m_waveform.getScopeMono(out, count, spanSamples);
}

void AudioEngine::getWaveform(int16_t* leftWave, int16_t* rightWave, size_t count, bool trigger) const {
    m_waveform.getWaveform(leftWave, rightWave, count, trigger);
}

void AudioEngine::setWaveformWindowMode(WaveformPipeline::WindowMode mode) {
    m_waveform.setWindowMode(mode);
}

WaveformPipeline::WindowMode AudioEngine::getWaveformWindowMode() const {
    return m_waveform.getWindowMode();
}
