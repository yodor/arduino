#ifndef DISPLAY_CONTROLLER_H
#define DISPLAY_CONTROLLER_H

#include <Arduino_GFX_Library.h>

#define TFT_DC     4
#define TFT_CS     5
#define TFT_SCK    2
#define TFT_MOSI   3
#define TFT_MISO  -1
#define TFT_RST    6
#define TFT_BL     7

class DisplayController {
public:
    static constexpr uint16_t SCREEN_WIDTH  = 428;
    static constexpr uint16_t SCREEN_HEIGHT = 142;
    static constexpr uint8_t  NUM_BARS      = 24;

    bool filledWaveMode   = false;
    uint8_t waveThickness = 2;

    DisplayController() = default;

    void begin() {
        m_bus = new Arduino_RPiPicoSPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO, spi0);
        m_tft = new Arduino_NV3007(
            m_bus, TFT_RST, 1, false, SCREEN_HEIGHT, SCREEN_WIDTH, 12, 0, 14, 0,
            nv3007_279_init_operations, sizeof(nv3007_279_init_operations)
        );

        pinMode(TFT_BL, OUTPUT);   digitalWrite(TFT_BL, HIGH);
        pinMode(TFT_RST, OUTPUT);  digitalWrite(TFT_RST, HIGH); delay(10);
        digitalWrite(TFT_RST, LOW); delay(150);
        digitalWrite(TFT_RST, HIGH); delay(300);

        m_tft->begin(100000000);
        m_tft->invertDisplay(false);
        clearScreen();
    }

    void clearScreen() {
        if (!m_tft) return;
        m_tft->fillScreen(0x0000);
        for (uint16_t i = 0; i < SCREEN_WIDTH; i++) {
            m_oldWaveYTop[i] = SCREEN_HEIGHT / 2;
            m_oldWaveYBot[i] = SCREEN_HEIGHT / 2;
        }
        for (uint8_t b = 0; b < NUM_BARS; b++) {
            m_oldFFTHeight[b] = 0;
        }
    }

    void drawWaveformFrame(const uint8_t* waveY) {
        if (!m_tft) return;
        m_tft->startWrite();

        uint16_t centerY = SCREEN_HEIGHT / 2;

        for (uint16_t x = 0; x < SCREEN_WIDTH; x++) {
            uint8_t newYTop, newYBot;

            if (filledWaveMode) {
                newYTop = min(waveY[x], static_cast<uint8_t>(centerY));
                newYBot = max(waveY[x], static_cast<uint8_t>(centerY));
            } else {
                uint8_t halfThick = waveThickness / 2;
                newYTop = (waveY[x] > halfThick) ? (waveY[x] - halfThick) : 0;
                newYBot = min(static_cast<uint16_t>(waveY[x] + halfThick), static_cast<uint16_t>(SCREEN_HEIGHT - 1));
            }

            uint8_t oldTop = m_oldWaveYTop[x];
            uint8_t oldBot = m_oldWaveYBot[x];

            if (oldTop < newYTop) {
                m_tft->drawFastVLine(x, oldTop, newYTop - oldTop, 0x0000);
            }
            if (oldBot > newYBot) {
                m_tft->drawFastVLine(x, newYBot + 1, oldBot - newYBot, 0x0000);
            }

            uint8_t drawHeight = (newYBot >= newYTop) ? (newYBot - newYTop + 1) : 1;
            m_tft->drawFastVLine(x, newYTop, drawHeight, 0xFFFF);

            m_oldWaveYTop[x] = newYTop;
            m_oldWaveYBot[x] = newYBot;
        }

        m_tft->endWrite();
    }

    void drawFftFrame(const uint8_t* barHeights) {
        if (!m_tft) return;
        constexpr uint8_t BAR_WIDTH = 14;
        constexpr uint8_t BAR_GAP   = 3;

        m_tft->startWrite();
        for (uint8_t b = 0; b < NUM_BARS; b++) {
            uint16_t targetX = b * (BAR_WIDTH + BAR_GAP);
            uint8_t newHeight = barHeights[b];
            uint8_t oldHeight = m_oldFFTHeight[b];

            if (newHeight != oldHeight) {
                if (newHeight < oldHeight) {
                    m_tft->fillRect(
                        targetX, 
                        (SCREEN_HEIGHT - 1) - oldHeight, 
                        BAR_WIDTH, 
                        oldHeight - newHeight, 
                        0x0000
                    );
                } else {
                    uint16_t barColor = (newHeight > (SCREEN_HEIGHT * 0.75f)) ? 0xF800 : 0x07E0;
                    m_tft->fillRect(
                        targetX, 
                        (SCREEN_HEIGHT - 1) - newHeight, 
                        BAR_WIDTH, 
                        newHeight - oldHeight, 
                        barColor
                    );
                }
                m_oldFFTHeight[b] = newHeight;
            }
        }
        m_tft->endWrite();
    }

private:
    Arduino_DataBus *m_bus = nullptr;
    Arduino_GFX *m_tft = nullptr;
    uint8_t m_oldWaveYTop[SCREEN_WIDTH];
    uint8_t m_oldWaveYBot[SCREEN_WIDTH];
    uint8_t m_oldFFTHeight[NUM_BARS];
};

#endif
