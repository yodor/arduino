#include "PowerOverlay.hpp"
#include "PowerControl.hpp"
#include "Renderer.hpp"
#include "ColorTheme.hpp"
#include <cstring>

namespace {
constexpr int16_t  kCharW     = 6;
constexpr int16_t  kCharH     = 8;
constexpr int16_t  kMargin    = 12;
constexpr int16_t  kTitleY    = 8;
constexpr int16_t  kBigY      = 40;
constexpr uint8_t  kBigSize   = 5;
constexpr int16_t  kSubY      = 92;
constexpr int16_t  kBarY      = 118;
constexpr int16_t  kBarH      = 12;
constexpr uint16_t kGray      = 0x8410;
constexpr uint16_t kDarkGray  = 0x2104;

void centered(Arduino_GFX* gfx, const char* text, uint8_t size, int16_t y, uint16_t color) {
    int16_t w = static_cast<int16_t>(strlen(text)) * kCharW * size;
    gfx->setTextSize(size);
    gfx->setTextColor(color, COLOR_BLACK);
    gfx->setCursor((SCREEN_WIDTH - w) / 2, y);
    gfx->print(text);
}
} // namespace

void PowerOverlay::showHint() {
    m_mode       = Mode::HINT;
    m_active     = true;
    m_shownUntil = millis() + POWER_POPUP_MS;
    m_needFull   = true;
}

void PowerOverlay::update() {
    const PowerControl& pc = PowerControl::instance();
    bool transition = pc.inTransition();

    if (transition) {
        if (m_mode != Mode::SEQUENCE) m_needFull = true;
        m_mode   = Mode::SEQUENCE;
        m_active = true;
    } else if (m_wasTransition) {
        // A sequence just finished: keep its final state up a moment.
        m_shownUntil = millis() + POWER_POPUP_MS;
    }
    m_wasTransition = transition;

    if (m_active && !transition && static_cast<int32_t>(millis() - m_shownUntil) >= 0) {
        m_active = false;
        m_mode   = Mode::NONE;
    }
}

void PowerOverlay::onEnter() {
    m_needFull = true;
}

void PowerOverlay::drawFrame() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    gfx->fillScreen(COLOR_BLACK);
    gfx->setTextSize(2);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setCursor(kMargin, kTitleY);
    gfx->print("POWER");
    m_drawnState = -1;
    m_drawnFill  = -1;
}

void PowerOverlay::render() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;
    const PowerControl& pc = PowerControl::instance();

    if (m_needFull) {
        drawFrame();
        m_needFull = false;
    }

    if (m_mode == Mode::HINT) {
        if (m_drawnState != 100) {
            centered(gfx, "STANDBY", kBigSize, kBigY, COLOR_YELLOW);
            centered(gfx, "press POWER to switch on", 2, kSubY + 8, kGray);
            m_drawnState = 100;
        }
        return;
    }

    // SEQUENCE (also its final state, held for POWER_POPUP_MS)
    int8_t st = static_cast<int8_t>(pc.state());
    if (st != m_drawnState) {
        gfx->fillRect(0, kBigY, SCREEN_WIDTH, kBarY - kBigY - 2, COLOR_BLACK);
        bool goingOn = pc.switchingOn() || pc.isOn();
        centered(gfx, goingOn ? "POWER ON" : "STANDBY", kBigSize, kBigY, goingOn ? COLOR_WHITE : COLOR_YELLOW);
        char sub[40];
        snprintf(sub, sizeof(sub), "%s", pc.stepText());
        centered(gfx, sub, 2, kSubY, kGray);
        m_drawnState = st;
        m_drawnFill  = -1;
    }

    // Progress bar (only redrawn where it grew).
    int16_t barW = SCREEN_WIDTH - 2 * kMargin;
    int16_t fill = static_cast<int16_t>(static_cast<int32_t>(barW) * pc.percent() / 100);
    if (m_drawnFill < 0) {
        gfx->fillRect(kMargin, kBarY, barW, kBarH, kDarkGray);
        m_drawnFill = 0;
    }
    if (fill > m_drawnFill) {
        gfx->fillRect(kMargin + m_drawnFill, kBarY, fill - m_drawnFill, kBarH, ColorTheme::instance().lowColor());
        m_drawnFill = fill;
    }
}
