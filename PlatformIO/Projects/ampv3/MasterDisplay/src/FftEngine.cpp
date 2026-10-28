#include "FftEngine.hpp"
#include "Config.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {
// The CMSIS-DSP real-FFT setup for FFT_SIZE -- chosen at compile time (a
// template, so the other sizes' calls are never instantiated), so only that
// size's twiddle and bit-reversal tables end up in flash.
template <size_t N>
arm_status initRfft(arm_rfft_fast_instance_f32* s) {
    if constexpr (N == 4096)      return arm_rfft_fast_init_4096_f32(s);
    else if constexpr (N == 2048) return arm_rfft_fast_init_2048_f32(s);
    else if constexpr (N == 1024) return arm_rfft_fast_init_1024_f32(s);
    else if constexpr (N == 512)  return arm_rfft_fast_init_512_f32(s);
    else {
        static_assert(N == 256, "FFT_SIZE must be 256, 512, 1024, 2048 or 4096");
        return arm_rfft_fast_init_256_f32(s);
    }
}
} // namespace

FftEngine::FftEngine(size_t fftSize)
    : m_fftSize(fftSize)
    , m_halfWindowThreshold(fftSize / 2)
{
    m_fftInL   = static_cast<float*>(malloc(sizeof(float) * m_fftSize));
    m_fftInR   = static_cast<float*>(malloc(sizeof(float) * m_fftSize));
    m_fftOut   = static_cast<float*>(malloc(sizeof(float) * m_fftSize));
    m_fftBinsL = static_cast<float*>(malloc(sizeof(float) * (m_fftSize / 2)));
    m_fftBinsR = static_cast<float*>(malloc(sizeof(float) * (m_fftSize / 2)));

    m_hanningTable = static_cast<float*>(malloc(sizeof(float) * m_fftSize));
    m_slideL       = static_cast<uint16_t*>(calloc(m_fftSize, sizeof(uint16_t)));
    m_slideR       = static_cast<uint16_t*>(calloc(m_fftSize, sizeof(uint16_t)));

    m_stagingL = static_cast<uint16_t*>(malloc(sizeof(uint16_t) * m_halfWindowThreshold));
    m_stagingR = static_cast<uint16_t*>(malloc(sizeof(uint16_t) * m_halfWindowThreshold));

    initHanningWindow();

    // Only the configured size's init is called, so only its tables are
    // linked (CMSIS-DSP has one init per size; the generic one would pull in
    // every size's tables).
    if (m_fftSize == FFT_SIZE) {
        m_rfftOk = (initRfft<FFT_SIZE>(&m_rfft) == ARM_MATH_SUCCESS);
    }
    if (!m_rfftOk) {
        Serial.printf("[FFT] CMSIS-DSP real FFT setup failed for %u points\n", static_cast<unsigned>(m_fftSize));
        return;
    }

    // Copy the FFT's constant tables (twiddles and bit reversal, ~20 KB at
    // 2048 points) into RAM and point the instance at the copies. In flash
    // they'd be read through the 16 KB XIP cache that core 1's code also
    // runs through -- streaming more than the cache holds on every FFT costs
    // misses. (If RAM ran short they'd simply stay in flash.)
    size_t cfftLen = m_rfft.Sint.fftLen;
    m_ramTwRfft = static_cast<float*>(malloc(sizeof(float) * m_fftSize));
    m_ramTwCfft = static_cast<float*>(malloc(sizeof(float) * 2 * cfftLen));
    m_ramBitRev = static_cast<uint16_t*>(malloc(sizeof(uint16_t) * m_rfft.Sint.bitRevLength));
    if (m_ramTwRfft && m_ramTwCfft && m_ramBitRev) {
        memcpy(m_ramTwRfft, m_rfft.pTwiddleRFFT,      sizeof(float) * m_fftSize);
        memcpy(m_ramTwCfft, m_rfft.Sint.pTwiddle,     sizeof(float) * 2 * cfftLen);
        memcpy(m_ramBitRev, m_rfft.Sint.pBitRevTable, sizeof(uint16_t) * m_rfft.Sint.bitRevLength);
        m_rfft.pTwiddleRFFT      = m_ramTwRfft;
        m_rfft.Sint.pTwiddle     = m_ramTwCfft;
        m_rfft.Sint.pBitRevTable = m_ramBitRev;
    }
}

FftEngine::~FftEngine() {
    if (m_fftInL)    free(m_fftInL);
    if (m_fftInR)    free(m_fftInR);
    if (m_fftOut)    free(m_fftOut);
    if (m_fftBinsL)  free(m_fftBinsL);
    if (m_fftBinsR)  free(m_fftBinsR);
    if (m_ramTwRfft) free(m_ramTwRfft);
    if (m_ramTwCfft) free(m_ramTwCfft);
    if (m_ramBitRev) free(m_ramBitRev);

    if (m_hanningTable) free(m_hanningTable);
    if (m_slideL) free(m_slideL);
    if (m_slideR) free(m_slideR);
    if (m_stagingL) free(m_stagingL);
    if (m_stagingR) free(m_stagingR);
}

void FftEngine::initHanningWindow() {
    for (size_t i = 0; i < m_fftSize; ++i) {
        m_hanningTable[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (m_fftSize - 1)));
    }
}

bool FftEngine::pushChunk(const uint16_t* interleavedChunk) {
    constexpr size_t kChunkPerChannel = CAPTURE_CHUNK_SAMPLES / 2;

    // Stage this chunk's samples; the (much larger, much more expensive)
    // main window is left untouched until a full threshold's worth has
    // accumulated -- see the staging buffers' comment in FftEngine.hpp.
    for (size_t i = 0; i < kChunkPerChannel; ++i) {
        m_stagingL[m_stagedSamples + i] = interleavedChunk[i * 2]     & 0x0FFF;
        m_stagingR[m_stagedSamples + i] = interleavedChunk[i * 2 + 1] & 0x0FFF;
    }
    m_stagedSamples += kChunkPerChannel;

    if (m_stagedSamples < m_halfWindowThreshold) {
        return false;
    }

    // A full threshold's worth has staged up -- fold it into the main
    // window in ONE combined shift+append, then run the FFT that
    // actually needs it.
    memmove(m_slideL, m_slideL + m_halfWindowThreshold, (m_fftSize - m_halfWindowThreshold) * sizeof(uint16_t));
    memmove(m_slideR, m_slideR + m_halfWindowThreshold, (m_fftSize - m_halfWindowThreshold) * sizeof(uint16_t));
    memcpy(m_slideL + (m_fftSize - m_halfWindowThreshold), m_stagingL, m_halfWindowThreshold * sizeof(uint16_t));
    memcpy(m_slideR + (m_fftSize - m_halfWindowThreshold), m_stagingR, m_halfWindowThreshold * sizeof(uint16_t));
    m_stagedSamples = 0;

    runFft();
    return true;
}

void FftEngine::runFft() {
    float meanL = 0.0f, meanR = 0.0f;
    for (size_t i = 0; i < m_fftSize; ++i) {
        meanL += static_cast<float>(m_slideL[i]);
        meanR += static_cast<float>(m_slideR[i]);
    }
    meanL /= static_cast<float>(m_fftSize);
    meanR /= static_cast<float>(m_fftSize);

    for (size_t i = 0; i < m_fftSize; ++i) {
        float sampleL = static_cast<float>(m_slideL[i]) - meanL;
        float sampleR = static_cast<float>(m_slideR[i]) - meanR;
        m_fftInL[i] = (sampleL / 2048.0f) * m_hanningTable[i];
        m_fftInR[i] = (sampleR / 2048.0f) * m_hanningTable[i];
    }

    // lastFftDurationUs() times the two transforms only (as before the
    // switch from kissfft, so the numbers compare); each channel's
    // magnitudes are taken from the shared output buffer before the next
    // transform overwrites it.
    //
    // Output format of arm_rfft_fast_f32: out[0] = bin 0 (DC, real),
    // out[1] = the Nyquist bin (real), then re/im pairs for bins 1..N/2-1 --
    // the same unnormalised transform as before, so the scaling is unchanged.
    constexpr float kHannCoherentGain = 0.5f;
    const float scale = 2.0f / (static_cast<float>(m_fftSize) * kHannCoherentGain);
    auto magnitudes = [&](float* bins) {
        bins[0] = fabsf(m_fftOut[0]) * scale;
        for (size_t k = 1; k < m_fftSize / 2; ++k) {
            float re = m_fftOut[2 * k], im = m_fftOut[2 * k + 1];
            bins[k] = sqrtf(re * re + im * im) * scale;
        }
    };

    if (m_rfftOk) {
        uint32_t t0 = micros();
        arm_rfft_fast_f32(&m_rfft, m_fftInL, m_fftOut, 0);
        uint32_t fftUs = micros() - t0;
        magnitudes(m_fftBinsL);
        t0 = micros();
        arm_rfft_fast_f32(&m_rfft, m_fftInR, m_fftOut, 0);
        fftUs += micros() - t0;
        magnitudes(m_fftBinsR);
        m_lastFftUs = fftUs;
    } else {
        memset(m_fftBinsL, 0, sizeof(float) * (m_fftSize / 2));
        memset(m_fftBinsR, 0, sizeof(float) * (m_fftSize / 2));
    }

    uint32_t nowUs = micros();
    if (m_lastRunUs != 0) m_refreshHz = 1000000.0f / static_cast<float>(nowUs - m_lastRunUs);
    m_lastRunUs = nowUs;
}
