#include "Renderer.hpp"
#include "Console.hpp"

bool     NV3007Display::alignPairs = DISPLAY_ALIGN_PIXEL_PAIRS;
uint8_t  NV3007Display::minRows    = DISPLAY_MIN_WINDOW_ROWS;
uint16_t NV3007Display::s_shadow[SCREEN_HEIGHT][SCREEN_WIDTH]; // ~119 KB shadow of the panel
#include "hardware/spi.h"

Renderer& Renderer::instance() {
    static Renderer inst;
    return inst;
}

void Renderer::init() {
    // Instantiate customized Pico SPI bus driver
    m_bus = new Arduino_RPiPicoSPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO, spi0);

    // Instantiate NV3007 display panel driver using Config.hpp parameters
    m_tft = new NV3007Display(
        m_bus, TFT_RST, TFT_ROTATION, false /* IPS */, SCREEN_HEIGHT, SCREEN_WIDTH,
        12, 0, 14, 0, nv3007_279_init_operations, sizeof(nv3007_279_init_operations)
    );

    // SPI clock = clk_peri / (even prescaler >= 2) / postdiv. With the
    // default 48MHz clk_peri that caps out at 24MHz; with CUSTOM_CLOCKS_ENABLED
    // main.cpp binds clk_peri to clk_sys, so SPI_SPEED_HZ (Config.hpp) can go
    // higher. The actual rate achieved is printed below.
    //
    // Signal integrity note: every bar screen draws incrementally (it only
    // paints what changed since last frame), so a single write lost or
    // corrupted on the bus is never repaired -- a missed black erase stays
    // on screen as a stale peak line or bar remnant until something paints
    // over it. If those artifacts appear, try a lower SPI_SPEED_HZ first.
    if (CUSTOM_CLOCKS_ENABLED && SPI_SPEED_HZ>0) {
        m_tft->begin(SPI_SPEED_HZ);
    }
    else {
        m_tft->begin();
    }

    uint32_t actualHz = spi_get_baudrate(spi0);
    Console::log("SPI", "Speed: %lu Hz", static_cast<unsigned long>(actualHz));

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