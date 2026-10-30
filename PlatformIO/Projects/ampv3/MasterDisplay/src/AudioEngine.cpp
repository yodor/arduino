#include "AudioEngine.hpp"
#include "Console.hpp"
#include "DebugFlags.hpp"
#include "Profiler.hpp"
#include <cmath>
#include "hardware/clocks.h"
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
    // Divider from the ACTUAL ADC clock (normally 48 MHz from the USB PLL),
    // so the rate is AUDIO_SAMPLE_RATE_HZ per channel whatever that clock
    // is: one conversion every (1 + div) ADC clock cycles, two channels.
    {
        uint32_t adcHz = clock_get_hz(clk_adc);
        float    div   = static_cast<float>(adcHz) / (2.0f * AUDIO_SAMPLE_RATE_HZ) - 1.0f;
        if (div < 95.0f) div = 95.0f; // a conversion takes 96 cycles: the fastest the ADC can go
        adc_set_clkdiv(div);
        m_adcDiv = div;
        Console::log("Audio", "ADC clock %lu Hz, divider %s -> %s Hz per channel", static_cast<unsigned long>(adcHz),
                     Console::num(div, 1).s, Console::num(adcHz / (2.0f * (div + 1.0f)), 0).s);
    }

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

    // Diagnostics (DebugFlags::audio): per-chunk cost, deepest backlog,
    // overruns -- reported once a second.
    uint32_t chunkCount = 0;
    PerfStat waveformStat, spectrumStat, beatStat;   // per chunk
    // FFT chunks only: the whole chunk and its stages.
    PerfStat fftChunkStat, stShift, stWindow, stFft, stMags, stBands;
    uint32_t maxBacklogChunks = 0, overruns = 0, totalOverruns = 0;
    uint32_t lastReportMs = 0;
    uint32_t maxGapUs = 0, gapSkips = 0, lostChunks = 0;
    // 3/4 of the ring's duration (16 chunks = ~46 ms at 44.1 kHz).
    constexpr uint32_t kLapGuardUs = static_cast<uint32_t>(
        0.75f * (kRingSamples / 2) / AUDIO_SAMPLE_RATE_HZ * 1000000.0f);

    // ADC rate self-calibration: the samples the DMA actually PRODUCED
    // (its write position's progress -- independent of how many chunks this
    // core processed) are counted over a 2 s window; if the per-channel rate
    // is more than 1% off AUDIO_SAMPLE_RATE_HZ, the ADC divider is scaled by
    // exactly that ratio (rate = clk_adc / (1 + div)), then checked once
    // more. Fixes the rate whatever clk_adc really is.
    uint32_t producedSamples = 0;                 // per diagnostics second
    uint32_t calSamples = 0, calStartMs = 0, lastWp = writePos();
    uint8_t  calStage = 0;                        // 0 settle, 1 measure, 2 verify, 3 done
    uint32_t calStageMs = millis();

    uint32_t lastLoopUs = micros();
    while (true) {
        // Samples produced since the last iteration (the ring is far longer
        // than one iteration, so a lap can't be missed).
        {
            uint32_t wp = writePos();
            uint32_t produced = (wp + kRingSamples - lastWp) % kRingSamples;
            lastWp = wp;
            producedSamples += produced;
            calSamples      += produced;
            uint32_t ms = millis();
            if (calStage == 0 && ms - calStageMs >= 1000) {          // let start-up settle
                calStage = 1; calSamples = 0; calStartMs = ms;
            } else if ((calStage == 1 || calStage == 2) && ms - calStartMs >= 2000) {
                float rate = calSamples / 2.0f / ((ms - calStartMs) / 1000.0f); // per channel
                float div  = m_adcDiv;
                if (calStage == 1 && fabsf(rate / AUDIO_SAMPLE_RATE_HZ - 1.0f) > 0.01f) {
                    float newDiv = (div + 1.0f) * rate / AUDIO_SAMPLE_RATE_HZ - 1.0f;
                    if (newDiv < 95.0f) newDiv = 95.0f;
                    adc_set_clkdiv(newDiv);
                    m_adcDiv = newDiv;
                    Console::log("Audio", "measured %s Hz/ch (target %s): ADC divider %s -> %s",
                                 Console::num(rate, 0).s, Console::num(AUDIO_SAMPLE_RATE_HZ, 0).s,
                                 Console::num(div, 1).s, Console::num(newDiv, 1).s);
                    calStage = 2; calSamples = 0; calStartMs = ms;
                } else {
                    Console::log("Audio", "%s %s Hz/ch (divider %s)", calStage == 2 ? "verified" : "measured",
                                 Console::num(rate, 0).s, Console::num(div, 1).s);
                    m_measuredRateHz = rate;
                    calStage = 3;
                }
            }
        }

        // A pause of this core (a flash write: LittleFS idles it via
        // rp2040.idleOtherCore(); a moment's wait on the shared USB serial)
        // is simply caught up afterwards -- processing a chunk takes about
        // half its real time. Only a pause long enough that the DMA could
        // have lapped the ring (the wrap would hide it) jumps to fresh audio,
        // and that's counted. (An earlier 10 ms threshold here silently threw
        // away 10-20% of the chunks.)
        uint32_t nowUs = micros();
        uint32_t gapUs = nowUs - lastLoopUs;
        if (gapUs > maxGapUs) maxGapUs = gapUs;
        if (gapUs > kLapGuardUs) {
            uint32_t lost = ((writePos() + kRingSamples - readPos) % kRingSamples) / kChunk;
            readPos = ((writePos() / kChunk) * kChunk + kRingSamples - kChunk) % kRingSamples;
            ++gapSkips;
            lostChunks += lost;
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
        uint32_t t2 = micros();
        BeatDetector::instance().pushChunk(chunk); // a few operations per sample (tempo for the Info screen)
        uint32_t t3 = micros();
        waveformStat.add(t1 - t0);
        spectrumStat.add(t2 - t1);
        beatStat.add(t3 - t2);
        if (m_spectrum.ranFft()) {
            // Where an FFT chunk's time goes, stage by stage.
            const FftEngine::Stages& st = m_spectrum.fftStages();
            fftChunkStat.add(t2 - t1);
            stShift.add(st.shift);
            stWindow.add(st.window);
            stFft.add(st.fft);
            stMags.add(st.mags);
            stBands.add(m_spectrum.bandsUs());
        }
        ++chunkCount;

        readPos = (readPos + kChunk) % kRingSamples;

        uint32_t nowMs = millis();
        if (nowMs - lastReportMs >= 1000) {
            lastReportMs = nowMs;
            if (DebugFlags::audio && chunkCount > 0) {
                // ~344 chunks/s means every chunk was processed.
                uint16_t minL = 4095, maxL = 0, minR = 4095, maxR = 0;
                for (uint32_t i = 0; i < kChunk / 2; ++i) {
                    uint16_t vL = chunk[i * 2] & 0x0FFF, vR = chunk[i * 2 + 1] & 0x0FFF;
                    if (vL < minL) minL = vL;
                    if (vL > maxL) maxL = vL;
                    if (vR < minR) minR = vR;
                    if (vR > maxR) maxR = vR;
                }
                // One line, built here on core 1 and sent by core 0 (Console's queue):
                // this core never waits on USB.
                Console::log("Audio", "%lu chunks/s | ADC %lu Hz/ch | per chunk: waveform %sms (max %s), spectrum %sms (max %s) | fft %sms @ %sHz"
                             " | max backlog %lu/%u chunks | overruns %lu (total %lu) | max gap %sms, skips %lu (lost %lu)"
                             " | raw pp L=%u R=%u",
                             static_cast<unsigned long>(chunkCount), static_cast<unsigned long>(producedSamples / 2),
                             Console::num(waveformStat.avgMs(), 3).s, Console::num(waveformStat.maxMs(), 2).s,
                             Console::num(spectrumStat.avgMs(), 3).s, Console::num(spectrumStat.maxMs(), 2).s,
                             Console::num(m_spectrum.lastFftDurationUs() / 1000.0f, 2).s,
                             Console::num(m_spectrum.observedRefreshHz(), 1).s,
                             static_cast<unsigned long>(maxBacklogChunks), static_cast<unsigned>(CAPTURE_RING_CHUNKS),
                             static_cast<unsigned long>(overruns), static_cast<unsigned long>(totalOverruns),
                             Console::num(maxGapUs / 1000.0f, 1).s,
                             static_cast<unsigned long>(gapSkips), static_cast<unsigned long>(lostChunks),
                             static_cast<unsigned>(maxL - minL), static_cast<unsigned>(maxR - minR));
                auto ms2 = [](const PerfStat& p) { return Console::num(p.avgMs(), 2); };
                auto mx2 = [](const PerfStat& p) { return Console::num(p.maxMs(), 2); };
                Console::log("Audio", "FFT chunk %sms (max %s) = shift %s (%s) + window %s (%s) + fft %s (%s)"
                             " + mags %s (%s) + bands %s (%s) | beat/chunk %s (%s) [ms avg (max)]",
                             ms2(fftChunkStat).s, mx2(fftChunkStat).s, ms2(stShift).s, mx2(stShift).s,
                             ms2(stWindow).s, mx2(stWindow).s, ms2(stFft).s, mx2(stFft).s,
                             ms2(stMags).s, mx2(stMags).s, ms2(stBands).s, mx2(stBands).s,
                             Console::num(beatStat.avgMs(), 3).s, mx2(beatStat).s);
            } else if (overruns > 0) {
                Console::log("Audio", "Capture overrun: chunks skipped in the last second: %lu", static_cast<unsigned long>(overruns));
            }
            chunkCount = 0;
            waveformStat.reset();
            spectrumStat.reset();
            beatStat.reset();
            fftChunkStat.reset(); stShift.reset(); stWindow.reset(); stFft.reset(); stMags.reset(); stBands.reset();
            maxBacklogChunks = overruns = 0;
            producedSamples = 0;
            maxGapUs = gapSkips = lostChunks = 0;
        }
    }
}

size_t AudioEngine::getSpectrumBars(float* leftBars, float* rightBars) const {
    return m_spectrum.getBars(leftBars, rightBars);
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
