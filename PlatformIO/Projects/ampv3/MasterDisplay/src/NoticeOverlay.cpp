#include "NoticeOverlay.hpp"
#include "Renderer.hpp"
#include <cstring>
#include <cstdio>

namespace {
void centered(Arduino_GFX* gfx, const char* text, uint8_t size, int16_t y, uint16_t color) {
    int16_t w = static_cast<int16_t>(strlen(text)) * 6 * size;
    int16_t x = (SCREEN_WIDTH - w) / 2;
    gfx->setTextSize(size);
    gfx->setTextColor(color, COLOR_BLACK);
    gfx->setCursor(x < 0 ? 0 : x, y);
    gfx->print(text);
}
} // namespace

void NoticeOverlay::show(const char* title, const char* line1, const char* line2, uint16_t titleColor, bool resync) {
    m_resync = resync;
    snprintf(m_title, sizeof(m_title), "%s", title);
    snprintf(m_line1, sizeof(m_line1), "%s", line1 ? line1 : "");
    snprintf(m_line2, sizeof(m_line2), "%s", line2 ? line2 : "");
    m_titleColor = titleColor;
    m_active     = true;
    m_untilMs    = millis() + NOTICE_POPUP_MS;
    m_needFull   = true;
}

void NoticeOverlay::update() {
    if (m_active && static_cast<int32_t>(millis() - m_untilMs) >= 0) m_active = false;
}

void NoticeOverlay::render() {
    if (!m_needFull) return; // static content: drawn once
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;
    gfx->fillScreen(COLOR_BLACK);
    centered(gfx, m_title, 3, 30, m_titleColor);  // 23 characters fit at size 3
    centered(gfx, m_line1, 2, 74, COLOR_WHITE);   // 35 at size 2
    centered(gfx, m_line2, 2, 102, 0x8410);
    m_needFull = false;
}
