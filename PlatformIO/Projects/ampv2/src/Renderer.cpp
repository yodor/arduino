#include "Renderer.hpp"
#include "hardware/spi.h"

Renderer& Renderer::instance() {
    static Renderer inst;
    return inst;
}

void Renderer::init() {
    // Instantiate customized Pico SPI bus driver
    m_bus = new Arduino_RPiPicoSPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO, spi0);

    // Instantiate NV3007 display panel driver using Config.hpp parameters
    m_tft = new Arduino_NV3007(
        m_bus, TFT_RST, 1 /* Rotation */, false /* IPS */, SCREEN_HEIGHT, SCREEN_WIDTH,
        12, 0, 14, 0, nv3007_279_init_operations, sizeof(nv3007_279_init_operations)
    );

    // 24MHz is the actual hardware ceiling here: RP2040's SPI clock is
    // clk_peri / (even prescaler >= 2) / (postdiv >= 1), and clk_peri is
    // fixed at 48MHz by default on Arduino-Pico (independent of CPU clock,
    // so UART/peripheral timing stays stable regardless of CPU overclock).
    // That makes 48MHz/2 = 24MHz the fastest SPI rate achievable without
    // reconfiguring clk_peri itself. Requesting anything higher (we tried
    // 41.67MHz) just gets silently clamped back down to this same ceiling.
    m_tft->begin(SPI_SPEED_HZ);

    uint32_t actualHz = spi_get_baudrate(spi0);
    Serial.print("[SPI] Requested ");
    Serial.print(SPI_SPEED_HZ);
    Serial.print(" Hz, actual achieved: ");
    Serial.print(actualHz);
    Serial.println(" Hz");

    // Enable display backlight if configured
    if (TFT_BL != static_cast<uint8_t>(-1)) {
        pinMode(TFT_BL, OUTPUT);
        digitalWrite(TFT_BL, HIGH);
    }

    m_tft->fillScreen(COLOR_BLACK);
}

void Renderer::clear() {
    if (!m_tft) return;
    m_tft->fillScreen(COLOR_BLACK);
}