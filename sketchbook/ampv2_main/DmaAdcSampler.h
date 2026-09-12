#ifndef DMA_ADC_SAMPLER_H
#define DMA_ADC_SAMPLER_H

#include <Arduino.h>
#include "hardware/adc.h"
#include "hardware/dma.h"

template <size_t BufferSize>
class DmaAdcSampler {
public:
    static inline DmaAdcSampler* instance = nullptr;

    explicit DmaAdcSampler(uint8_t gpioPin) : m_gpioPin(gpioPin), m_adcChannel(gpioPin - 26) {
        instance = this;
    }

    void begin(uint32_t sampleRateHz) {
        adc_gpio_init(m_gpioPin);
        adc_init();
        adc_select_input(m_adcChannel);
        adc_fifo_setup(true, true, 1, false, false);

        float clkDiv = (48000000.0f / static_cast<float>(sampleRateHz)) - 1.0f;
        adc_set_clkdiv(clkDiv);

        m_dmaChannel = dma_claim_unused_channel(true);
        dma_channel_config cfg = dma_channel_get_default_config(m_dmaChannel);
        channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
        channel_config_set_read_increment(&cfg, false);
        channel_config_set_write_increment(&cfg, true);
        channel_config_set_dreq(&cfg, DREQ_ADC);

        dma_channel_configure(
            m_dmaChannel,
            &cfg,
            m_sampleBuffer,
            &adc_hw->fifo,
            BufferSize,
            false
        );

        dma_channel_set_irq0_enabled(m_dmaChannel, true);
        irq_set_exclusive_handler(DMA_IRQ_0, dmaIrqHandler);
        irq_set_enabled(DMA_IRQ_0, true);

        restartDma();
    }

    bool isBufferReady() const { return m_bufferReady; }
    void clearBufferReadyFlag() { m_bufferReady = false; }
    const uint16_t* getBuffer() const { return m_sampleBuffer; }

    void restartDma() {
        m_bufferReady = false;
        adc_run(false);
        adc_fifo_drain();
        dma_channel_abort(m_dmaChannel);
        dma_channel_set_write_addr(m_dmaChannel, m_sampleBuffer, true);
        adc_run(true);
    }

private:
    static void dmaIrqHandler() {
        if (instance != nullptr) {
            dma_hw->ints0 = (1u << instance->m_dmaChannel);
            instance->m_bufferReady = true;
        }
    }

    uint8_t m_gpioPin;
    uint8_t m_adcChannel;
    int m_dmaChannel = -1;
    volatile bool m_bufferReady = false;
    uint16_t m_sampleBuffer[BufferSize] __attribute__((aligned(4)));
};

#endif
