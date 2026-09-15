#include "AudioEngine.hpp"
#include "hardware/clocks.h"

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

    // --- Two-channel chained ping-pong ring capture ---
    // Each channel writes into its OWN CAPTURE_CHUNK_SAMPLES-sized buffer,
    // ring-wrapping WITHIN that buffer only, and chains to the OTHER
    // channel on completion -- so finishing chunk A automatically starts
    // filling chunk B, and vice versa, forever, with no CPU involvement
    // between chunks. This is the single physical capture stream all
    // three independent pipelines below build their own differently-sized
    // sliding windows from.
    int chanA = dma_claim_unused_channel(true);
    int chanB = dma_claim_unused_channel(true);

    constexpr uint32_t kRingBytes    = static_cast<uint32_t>(CAPTURE_CHUNK_SAMPLES) * sizeof(uint16_t);
    constexpr uint32_t kRingSizeBits = log2_constexpr(kRingBytes);

    dma_channel_config confA = dma_channel_get_default_config(chanA);
    channel_config_set_transfer_data_size(&confA, DMA_SIZE_16);
    channel_config_set_read_increment(&confA, false);
    channel_config_set_write_increment(&confA, true);
    channel_config_set_dreq(&confA, DREQ_ADC);
    channel_config_set_ring(&confA, true, kRingSizeBits);
    channel_config_set_chain_to(&confA, chanB);

    dma_channel_config confB = dma_channel_get_default_config(chanB);
    channel_config_set_transfer_data_size(&confB, DMA_SIZE_16);
    channel_config_set_read_increment(&confB, false);
    channel_config_set_write_increment(&confB, true);
    channel_config_set_dreq(&confB, DREQ_ADC);
    channel_config_set_ring(&confB, true, kRingSizeBits);
    channel_config_set_chain_to(&confB, chanA);

    dma_channel_configure(chanA, &confA, m_audioBuffer[0], &adc_hw->fifo, CAPTURE_CHUNK_SAMPLES, false);
    dma_channel_configure(chanB, &confB, m_audioBuffer[1], &adc_hw->fifo, CAPTURE_CHUNK_SAMPLES, false);

    dma_channel_start(chanA);
    adc_run(true); // started ONCE here -- never stopped again during normal operation

    bool waitingForA = true;
    constexpr size_t kChunkPerChannel = CAPTURE_CHUNK_SAMPLES / 2;

    // --- Diagnostic accumulators (see the periodic report near the end
    // of the loop for what each one means) ---
    uint32_t chunkAccumUs       = 0;
    uint32_t chunkCountSinceRpt = 0;
    uint32_t lastRawDebugMs     = 0;
    uint32_t lastTimingDebugMs  = 0;

    // Per-pipeline dispatch cost, isolated -- the chunk-average timing
    // didn't drop as expected after batching the octave/bar9 window
    // shifts, meaning something else is the real dominant cost. Rather
    // than guess again, measure each pushChunk() call directly so we can
    // see exactly which pipeline (or the DMA wait itself, by elimination)
    // actually accounts for the time.
    uint32_t waveformAccumUs = 0;
    uint32_t bar9AccumUs     = 0;
    uint32_t octaveAccumUs   = 0;

    // Dispatch sum alone didn't add up to chunk avg -- isolating the wait
    // and the RawADC scan directly (the two remaining unmeasured pieces)
    // to find exactly where the rest of the time goes, rather than assume.
    uint32_t waitAccumUs   = 0;
    uint32_t rawAdcAccumUs = 0;

    // clk_adc measured and confirmed correct (48MHz, exactly as assumed),
    // ruling that out -- this probes the DMA hardware DIRECTLY instead of
    // inferring from aggregate timing: reads the just-started channel's
    // live remaining-transfer-count register right as dispatch begins and
    // again right as it ends, to see definitively whether that channel is
    // actually progressing (i.e. genuinely continuing to capture) DURING
    // our dispatch work, or sitting idle until dispatch finishes.
    uint32_t transferProgressAccum = 0;

    while (true) {
        uint32_t chunkStartUs = micros();

        uint16_t* captureBuf;
        int justStartedChannel;
        uint32_t waitStartUs = micros();
        if (waitingForA) {
            // chanA is currently running; chanB going busy is the signal
            // that chanA just completed (its chain_to triggered chanB).
            while (!dma_channel_is_busy(chanB)) tight_loop_contents();
            captureBuf = m_audioBuffer[0];
            justStartedChannel = chanB;
        } else {
            while (!dma_channel_is_busy(chanA)) tight_loop_contents();
            captureBuf = m_audioBuffer[1];
            justStartedChannel = chanA;
        }
        waitingForA = !waitingForA;
        waitAccumUs += micros() - waitStartUs;

        uint32_t transferCountAtDispatchStart = dma_channel_hw_addr(justStartedChannel)->transfer_count;

        // --- Diagnostic: raw ADC peak-to-peak, straight off THIS chunk,
        // before any accumulation into any pipeline's window -- the
        // cleanest possible checkpoint to tell whether cross-channel
        // bleed exists at the hardware/ADC level or gets introduced
        // downstream in our own code.
        uint32_t rawAdcStartUs = micros();
        uint16_t rawMinL = 4095, rawMaxL = 0;
        uint16_t rawMinR = 4095, rawMaxR = 0;
        for (size_t i = 0; i < kChunkPerChannel; ++i) {
            uint16_t vL = captureBuf[i * 2]     & 0x0FFF;
            uint16_t vR = captureBuf[i * 2 + 1] & 0x0FFF;
            if (vL < rawMinL) rawMinL = vL;
            if (vL > rawMaxL) rawMaxL = vL;
            if (vR < rawMinR) rawMinR = vR;
            if (vR > rawMaxR) rawMaxR = vR;
        }
        rawAdcAccumUs += micros() - rawAdcStartUs;

        uint32_t rawDebugNowMs = millis();
        if (rawDebugNowMs - lastRawDebugMs >= 1000) {
            lastRawDebugMs = rawDebugNowMs;
            Serial.print("[RawADC] L pp=");
            Serial.print(rawMaxL - rawMinL);
            Serial.print(" (min=");
            Serial.print(rawMinL);
            Serial.print(" max=");
            Serial.print(rawMaxL);
            Serial.print(") | R pp=");
            Serial.print(rawMaxR - rawMinR);
            Serial.print(" (min=");
            Serial.print(rawMinR);
            Serial.print(" max=");
            Serial.print(rawMaxR);
            Serial.println(")");
        }

        // Dispatch this chunk to all three independent pipelines. Each
        // decides internally whether/how to act on it -- WaveformPipeline
        // always updates immediately; Bar9Pipeline/OctavePipeline only
        // actually run their FFT once enough samples have accumulated
        // (see FftEngine::pushChunk()).
        uint32_t waveformStartUs = micros();
        m_waveform.pushChunk(captureBuf);
        waveformAccumUs += micros() - waveformStartUs;

        uint32_t bar9StartUs = micros();
        m_bar9.pushChunk(captureBuf);
        bar9AccumUs += micros() - bar9StartUs;

        uint32_t octaveStartUs = micros();
        m_octave.pushChunk(captureBuf);
        octaveAccumUs += micros() - octaveStartUs;

        uint32_t transferCountAtDispatchEnd = dma_channel_hw_addr(justStartedChannel)->transfer_count;
        // transfer_count COUNTS DOWN as the channel progresses, so a
        // healthy amount of REAL, concurrent capture during dispatch
        // shows up as a large drop here. If this is consistently near
        // zero, the channel is NOT actually capturing while we're busy --
        // meaning the two channels aren't overlapping the way the design
        // intends, regardless of what the aggregate timing implied.
        if (transferCountAtDispatchStart >= transferCountAtDispatchEnd) {
            transferProgressAccum += transferCountAtDispatchStart - transferCountAtDispatchEnd;
        }

        // --- Diagnostic: three independent refresh cadences, once/sec ---
        // chunk avg reflects the waveform pipeline's refresh rate (it
        // updates every chunk, so this IS its effective refresh period).
        // bar9/octave each report their OWN measured FFT duration and
        // observed refresh rate, since they run on independent,
        // intermittent schedules now -- neither is tied to the chunk
        // rate or to each other.
        chunkAccumUs += (micros() - chunkStartUs);
        chunkCountSinceRpt++;

        uint32_t timingNowMs = millis();
        if (timingNowMs - lastTimingDebugMs >= 1000) {
            lastTimingDebugMs = timingNowMs;

            // One-time diagnostic, fired here (guaranteed at least 1s into
            // runtime, well after core0's Serial.begin()) rather than
            // immediately after adc_init() -- that earlier placement was
            // racing against Serial initialization from core0's setup()
            // and apparently losing, since the line never appeared at all.
            // Measures the ADC's ACTUAL live clock rather than assuming
            // the RP2040-derived 48MHz reference still holds on RP2350;
            // the gap between assumed capture time (~2.9ms, from
            // 48MHz/545) and measured chunk timing (~4.05-4.33ms) implies
            // an actual combined sample rate closer to ~59-63kHz.
            static bool printedClockInfo = false;
            if (!printedClockInfo) {
                printedClockInfo = true;
                uint32_t adcClockHz = clock_get_hz(clk_adc);
                Serial.print("[Clock] clk_adc = ");
                Serial.print(adcClockHz);
                Serial.print(" Hz (assumed 48000000) | with clkdiv=544, implied combined sample rate = ");
                Serial.print(static_cast<float>(adcClockHz) / 545.0f, 1);
                Serial.println(" Hz");
            }
            float avgChunkUs = (chunkCountSinceRpt > 0)
                                    ? static_cast<float>(chunkAccumUs) / static_cast<float>(chunkCountSinceRpt)
                                    : 0.0f;
            float avgWaveformUs = (chunkCountSinceRpt > 0)
                                       ? static_cast<float>(waveformAccumUs) / static_cast<float>(chunkCountSinceRpt)
                                       : 0.0f;
            float avgBar9Us = (chunkCountSinceRpt > 0)
                                   ? static_cast<float>(bar9AccumUs) / static_cast<float>(chunkCountSinceRpt)
                                   : 0.0f;
            float avgOctaveUs = (chunkCountSinceRpt > 0)
                                     ? static_cast<float>(octaveAccumUs) / static_cast<float>(chunkCountSinceRpt)
                                     : 0.0f;
            float avgWaitUs = (chunkCountSinceRpt > 0)
                                   ? static_cast<float>(waitAccumUs) / static_cast<float>(chunkCountSinceRpt)
                                   : 0.0f;
            float avgRawAdcUs = (chunkCountSinceRpt > 0)
                                     ? static_cast<float>(rawAdcAccumUs) / static_cast<float>(chunkCountSinceRpt)
                                     : 0.0f;

            Serial.print("[Timing] waveform chunk avg=");
            Serial.print(avgChunkUs / 1000.0f, 3);
            Serial.print("ms (~");
            Serial.print(avgChunkUs > 0.0f ? 1000000.0f / avgChunkUs : 0.0f, 1);
            Serial.print("Hz) | bar9 fft=");
            Serial.print(m_bar9.lastFftDurationUs() / 1000.0f, 2);
            Serial.print("ms @ ");
            Serial.print(m_bar9.observedRefreshHz(), 1);
            Serial.print("Hz | octave fft=");
            Serial.print(m_octave.lastFftDurationUs() / 1000.0f, 2);
            Serial.print("ms @ ");
            Serial.print(m_octave.observedRefreshHz(), 1);
            Serial.println("Hz");

            Serial.print("[Timing] per-chunk dispatch avg -- waveform=");
            Serial.print(avgWaveformUs / 1000.0f, 3);
            Serial.print("ms | bar9=");
            Serial.print(avgBar9Us / 1000.0f, 3);
            Serial.print("ms | octave=");
            Serial.print(avgOctaveUs / 1000.0f, 3);
            Serial.print("ms | wait=");
            Serial.print(avgWaitUs / 1000.0f, 3);
            Serial.print("ms | rawAdcScan=");
            Serial.print(avgRawAdcUs / 1000.0f, 3);
            Serial.print("ms | sum=");
            Serial.print((avgWaveformUs + avgBar9Us + avgOctaveUs + avgWaitUs + avgRawAdcUs) / 1000.0f, 3);
            Serial.println("ms");

            // Direct DMA hardware probe: how many of the just-started
            // channel's CAPTURE_CHUNK_SAMPLES actually got captured WHILE
            // we were busy dispatching, on average -- vs. what real
            // concurrent capture (at the confirmed 88073.4Hz combined
            // rate) over that same dispatch duration SHOULD have produced.
            // A big gap between "actual" and "expected" here means the two
            // DMA channels aren't genuinely overlapping with our
            // processing, regardless of what the aggregate timing implied.
            float avgTransferProgress = (chunkCountSinceRpt > 0)
                                             ? static_cast<float>(transferProgressAccum) / static_cast<float>(chunkCountSinceRpt)
                                             : 0.0f;
            float avgDispatchUs = avgWaveformUs + avgBar9Us + avgOctaveUs + avgRawAdcUs;
            float expectedProgress = avgDispatchUs * (88073.4f / 1000000.0f);

            Serial.print("[Timing] DMA overlap check -- actual capture progress during dispatch=");
            Serial.print(avgTransferProgress, 1);
            Serial.print(" samples | expected if fully overlapped=");
            Serial.print(expectedProgress, 1);
            Serial.print(" samples (of ");
            Serial.print(CAPTURE_CHUNK_SAMPLES);
            Serial.println(" per chunk)");

            chunkAccumUs           = 0;
            chunkCountSinceRpt     = 0;
            waveformAccumUs        = 0;
            bar9AccumUs            = 0;
            octaveAccumUs          = 0;
            waitAccumUs            = 0;
            rawAdcAccumUs          = 0;
            transferProgressAccum  = 0;
        }
    }
}

void AudioEngine::getSpectrumBars(float* leftBars, float* rightBars) const {
    m_bar9.getBars(leftBars, rightBars);
}

void AudioEngine::getOctaveBars(float* leftBars, float* rightBars) const {
    m_octave.getBars(leftBars, rightBars);
}

void AudioEngine::getWaveform(int16_t* leftWave, int16_t* rightWave, size_t count) const {
    m_waveform.getWaveform(leftWave, rightWave, count);
}