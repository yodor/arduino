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
    if (CUSTOM_CLOCKS_ENABLED && SPI_SPEED_HZ>0) {
        m_tft->begin(SPI_SPEED_HZ);
    }
    else {
        m_tft->begin();
    }

    uint32_t actualHz = spi_get_baudrate(spi0);
    Serial.print("[SPI] Speed: ");
    Serial.print(actualHz);
    Serial.println(" Hz");

    // Menu wiring done (root-level "Brightness" item, see MenuContent.cpp);
    // IR control for brightness is still a possible future addition.
    setBacklightPercent(100); // sensible default matching the old always-on
                               // behavior; PersistentSettings::loadAndApply()
                               // (called later, after Renderer::init()) will
                               // override this if a saved brightness exists

    m_tft->fillScreen(COLOR_BLACK);
}

void Renderer::clear() {
    if (!m_tft) return;
    m_tft->fillScreen(COLOR_BLACK);
}

void Renderer::setBacklightPercent(uint8_t percent) {
    if (TFT_BL == static_cast<uint8_t>(-1)) return;
    if (percent > 100) percent = 100;

    // analogWrite() handles switching the pin's function to PWM (and
    // configuring the underlying slice's counter/clkdiv) the first time
    // it's called on a given pin -- no separate pinMode(OUTPUT) needed,
    // and this only ever touches TFT_BL's own GPIO, never the other pins
    // that happen to share its PWM slice (TFT_RST, REGULATOR_MODE_PIN --
    // see Pins.hpp).
    uint8_t duty = static_cast<uint8_t>((static_cast<uint16_t>(percent) * 255) / 100);
    analogWrite(TFT_BL, duty);
}