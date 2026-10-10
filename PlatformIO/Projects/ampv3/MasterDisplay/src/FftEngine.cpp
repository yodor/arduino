#include "FftEngine.hpp"
#include "Config.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>

FftEngine::FftEngine(size_t fftSize)
    : m_fftSize(fftSize)
    , m_halfWindowThreshold(fftSize / 2)
{
    m_fftCfgL  = kiss_fftr_alloc(static_cast<int>(m_fftSize), 0, nullptr, nullptr);
    m_fftInL   = static_cast<kiss_fft_scalar*>(malloc(sizeof(kiss_fft_scalar) * m_fftSize));
    m_fftOutL  = static_cast<kiss_fft_cpx*>(malloc(sizeof(kiss_fft_cpx) * (m_fftSize / 2 + 1)));
    m_fftBinsL = static_cast<float*>(malloc(sizeof(float) * (m_fftSize / 2)));

    m_fftCfgR  = kiss_fftr_alloc(static_cast<int>(m_fftSize), 0, nullptr, nullptr);
    m_fftInR   = static_cast<kiss_fft_scalar*>(malloc(sizeof(kiss_fft_scalar) * m_fftSize));
    m_fftOutR  = static_cast<kiss_fft_cpx*>(malloc(sizeof(kiss_fft_cpx) * (m_fftSize / 2 + 1)));
    m_fftBinsR = static_cast<float*>(malloc(sizeof(float) * (m_fftSize / 2)));

    m_hanningTable = static_cast<float*>(malloc(sizeof(float) * m_fftSize));
    m_slideL       = static_cast<uint16_t*>(calloc(m_fftSize, sizeof(uint16_t)));
    m_slideR       = static_cast<uint16_t*>(calloc(m_fftSize, sizeof(uint16_t)));

    m_stagingL = static_cast<uint16_t*>(malloc(sizeof(uint16_t) * m_halfWindowThreshold));
    m_stagingR = static_cast<uint16_t*>(malloc(sizeof(uint16_t) * m_halfWindowThreshold));

    initHanningWindow();
}

FftEngine::~FftEngine() {
    if (m_fftCfgL)  kiss_fftr_free(m_fftCfgL);
    if (m_fftInL)   free(m_fftInL);
    if (m_fftOutL)  free(m_fftOutL);
    if (m_fftBinsL) free(m_fftBinsL);

    if (m_fftCfgR)  kiss_fftr_free(m_fftCfgR);
    if (m_fftInR)   free(m_fftInR);
    if (m_fftOutR)  free(m_fftOutR);
    if (m_fftBinsR) free(m_fftBinsR);

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

    uint32_t fftStartUs = micros();
    kiss_fftr(m_fftCfgL, m_fftInL, m_fftOutL);
    kiss_fftr(m_fftCfgR, m_fftInR, m_fftOutR);
    m_lastFftUs = micros() - fftStartUs;

    uint32_t nowUs = micros();
    if (m_lastRunUs != 0) m_refreshHz = 1000000.0f / static_cast<float>(nowUs - m_lastRunUs);
    m_lastRunUs = nowUs;

    // 2/N standard single-sided amplitude scale, corrected for the Hann
    // window's coherent gain (0.5) -- without this correction a genuine
    // full-scale input reads back ~6dB low and never quite reaches 0dBFS.
    constexpr float kHannCoherentGain = 0.5f;
    const float scale = 2.0f / (static_cast<float>(m_fftSize) * kHannCoherentGain);
    for (size_t i = 0; i < m_fftSize / 2; ++i) {
        float realL = m_fftOutL[i].r;
        float imagL = m_fftOutL[i].i;
        m_fftBinsL[i] = sqrtf(realL * realL + imagL * imagL) * scale;

        float realR = m_fftOutR[i].r;
        float imagR = m_fftOutR[i].i;
        m_fftBinsR[i] = sqrtf(realR * realR + imagR * imagR) * scale;
    }
}