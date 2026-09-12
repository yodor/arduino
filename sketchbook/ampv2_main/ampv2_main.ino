#include <Arduino.h>
#include "DmaAdcSampler.h"
#include "AudioProcessor.h"
#include "DisplayController.h"
#include "InterCoreFifo.h"

#define BTN_UP     0
#define BTN_DOWN   1
#define BTN_LEFT  14
#define BTN_RIGHT 15
#define AUDIO_R   28
#define AUDIO_L   27

constexpr size_t FFT_SIZE = 512;
constexpr uint8_t NUM_FFT_BANDS = 24;
constexpr uint32_t AUDIO_SAMPLE_RATE_HZ = 44100; // Standard audio rate

DmaAdcSampler<FFT_SIZE> adcSampler(AUDIO_R);
AudioProcessor<FFT_SIZE> dspEngine;
DisplayController display;

DisplayMode systemMode = DisplayMode::Waveform;
unsigned long lastButtonTime = 0;

uint8_t sharedWaveY[DisplayController::SCREEN_WIDTH];
uint8_t sharedFftHeights[NUM_FFT_BANDS];

void setup() {
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_LEFT, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);

    adcSampler.begin(AUDIO_SAMPLE_RATE_HZ);
}

void loop() {
    // Mode toggle handling
    if (millis() - lastButtonTime > 250) {
        if (digitalRead(BTN_LEFT) == LOW || digitalRead(BTN_RIGHT) == LOW ||
            digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
            
            systemMode = (systemMode == DisplayMode::Waveform) ? DisplayMode::FFT : DisplayMode::Waveform;
            
            rp2040.fifo.push(0xFF000000 | static_cast<uint32_t>(systemMode));
            lastButtonTime = millis();
        }
    }

    if (adcSampler.isBufferReady()) {
        const uint16_t* samples = adcSampler.getBuffer();

        if (systemMode == DisplayMode::Waveform) {
            for (uint16_t x = 0; x < DisplayController::SCREEN_WIDTH; x++) {
                uint16_t srcIdx = (x * FFT_SIZE) / DisplayController::SCREEN_WIDTH;
                sharedWaveY[x] = static_cast<uint8_t>(map(samples[srcIdx], 0, 4095, DisplayController::SCREEN_HEIGHT - 1, 0));
            }
        } else {
            // Process audio with exact sample rate parameter
            dspEngine.process(samples, sharedFftHeights, NUM_FFT_BANDS, DisplayController::SCREEN_HEIGHT - 2, static_cast<float>(AUDIO_SAMPLE_RATE_HZ));
        }

        adcSampler.restartDma();

        if (rp2040.fifo.available() == 0) {
            rp2040.fifo.push(0x00000001);
        }
    }
}

void setup1() {
    display.begin();
}

void loop1() {
    static DisplayMode activeCore1Mode = DisplayMode::Waveform;

    while (rp2040.fifo.available()) {
        uint32_t cmd = rp2040.fifo.pop();

        if ((cmd & 0xFF000000) == 0xFF000000) {
            activeCore1Mode = static_cast<DisplayMode>(cmd & 0xFF);
            display.clearScreen();
            continue;
        }

        if (cmd == 0x00000001) {
            if (activeCore1Mode == DisplayMode::Waveform) {
                display.drawWaveformFrame(sharedWaveY);
            } else {
                display.drawFftFrame(sharedFftHeights);
            }
        }
    }
}
