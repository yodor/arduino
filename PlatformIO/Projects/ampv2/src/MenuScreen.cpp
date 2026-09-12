#include "MenuScreen.hpp"
#include "Renderer.hpp"

void MenuScreen::init(const MenuItem* rootItems, size_t rootCount) {
    m_rootItems = rootItems;
    m_rootCount = rootCount;
}

void MenuScreen::onEnter() {
    m_level   = Level::ROOT;
    m_rootIdx = 0;
    m_dirty   = true;
}

void MenuScreen::moveFocus(int delta) {
    if (m_level == Level::ROOT) {
        if (m_rootCount == 0) return;
        int idx = static_cast<int>(m_rootIdx) + delta;
        if (idx < 0) idx = static_cast<int>(m_rootCount) - 1;
        if (idx >= static_cast<int>(m_rootCount)) idx = 0;
        m_rootIdx = static_cast<size_t>(idx);
    } else {
        const MenuItem& item = m_rootItems[m_activeItemIdx];
        if (item.optionCount == 0) return;
        int idx = static_cast<int>(m_optionIdx) + delta;
        if (idx < 0) idx = static_cast<int>(item.optionCount) - 1;
        if (idx >= static_cast<int>(item.optionCount)) idx = 0;
        m_optionIdx = static_cast<size_t>(idx);
    }
    m_dirty = true;
}

void MenuScreen::enter() {
    if (m_rootCount == 0) return;

    if (m_level == Level::ROOT) {
        const MenuItem& item = m_rootItems[m_rootIdx];
        if (item.type == MenuItemType::ACTION) {
            if (item.onAction) item.onAction();
            m_dirty = true; // stays at root; redraw in case anything visible depends on the action
        } else { // VALUE_CHOICE
            m_activeItemIdx = m_rootIdx;
            m_optionIdx     = 0;
            m_level         = Level::ITEM_OPTIONS;
            m_dirty         = true;
        }
    } else { // ITEM_OPTIONS
        const MenuItem& item = m_rootItems[m_activeItemIdx];
        if (item.optionCount == 0) return;
        const MenuValueOption& opt = item.options[m_optionIdx];
        if (opt.onSelect) opt.onSelect();
        m_level = Level::ROOT;
        m_dirty = true;
    }
}

bool MenuScreen::back() {
    if (m_level == Level::ITEM_OPTIONS) {
        m_level = Level::ROOT;
        m_dirty = true;
        return true;
    }
    return false; // already at root -- caller closes the menu
}

void MenuScreen::render() {
    if (!m_dirty) return;
    drawFull();
    m_dirty = false;
}

void MenuScreen::drawFull() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx || m_rootCount == 0) return;

    gfx->fillScreen(COLOR_BLACK);

    constexpr uint16_t titleY   = MENU_TITLE_Y;
    constexpr uint16_t listTopY = MENU_LIST_TOP_Y;
    constexpr uint16_t rowH     = MENU_ROW_HEIGHT;
    constexpr uint8_t  textSize = MENU_TEXT_SIZE;

    gfx->setTextSize(textSize);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setCursor(2, titleY);
    gfx->print(m_level == Level::ROOT ? "MENU" : m_rootItems[m_activeItemIdx].label);

    gfx->drawFastHLine(0, listTopY - 4, SCREEN_WIDTH, COLOR_DIM_GREEN);

    size_t visibleRows = (SCREEN_HEIGHT - listTopY) / rowH;
    if (visibleRows == 0) return;

    size_t itemCount;
    size_t focusedIdx;

    if (m_level == Level::ROOT) {
        itemCount  = m_rootCount;
        focusedIdx = m_rootIdx;
    } else {
        itemCount  = m_rootItems[m_activeItemIdx].optionCount;
        focusedIdx = m_optionIdx;
    }

    size_t scrollTop = (focusedIdx >= visibleRows) ? (focusedIdx - visibleRows + 1) : 0;

    if (m_level == Level::ROOT) {
        for (size_t row = 0; row < visibleRows && (scrollTop + row) < m_rootCount; ++row) {
            size_t idx = scrollTop + row;
            bool   focused = (idx == m_rootIdx);
            uint16_t y = listTopY + row * rowH;

            if (focused) {
                gfx->fillRect(0, y, SCREEN_WIDTH, rowH, COLOR_WHITE);
                gfx->setTextColor(COLOR_BLACK, COLOR_WHITE);
            } else {
                gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
            }
            gfx->setCursor(4, y + 1);
            gfx->print(m_rootItems[idx].label);
        }
    } else { // ITEM_OPTIONS
        const MenuItem& item = m_rootItems[m_activeItemIdx];

        for (size_t row = 0; row < visibleRows && (scrollTop + row) < item.optionCount; ++row) {
            size_t idx = scrollTop + row;
            const MenuValueOption& opt = item.options[idx];
            bool focused = (idx == m_optionIdx);
            bool active  = opt.isActive && opt.isActive();
            uint16_t y = listTopY + row * rowH;

            if (focused) {
                gfx->fillRect(0, y, SCREEN_WIDTH, rowH, COLOR_WHITE);
                gfx->setTextColor(COLOR_BLACK, COLOR_WHITE);
            } else {
                gfx->setTextColor(active ? COLOR_YELLOW : COLOR_WHITE, COLOR_BLACK);
            }
            gfx->setCursor(4, y + 1);
            gfx->print(active ? "> " : "  ");
            gfx->print(opt.label);
        }
    }

    // Scroll indicators: drawn last so they're never covered by a focused
    // row's highlight fill. "More above" sits in the title bar's unused
    // right side; "more below" uses whatever margin is left under the
    // last row (currently ~8px with 4 visible rows -- if you add enough
    // items that this ever feels cramped, MENU_LIST_TOP_Y/MENU_ROW_HEIGHT
    // in Config.hpp are the knobs to revisit).
    bool moreAbove = scrollTop > 0;
    bool moreBelow = (scrollTop + visibleRows) < itemCount;

    constexpr int16_t triW = 10;
    constexpr int16_t triH = 6;
    constexpr int16_t triX = SCREEN_WIDTH - triW - 4;

    if (moreAbove) {
        int16_t y = titleY + 2;
        gfx->fillTriangle(triX, y + triH, triX + triW, y + triH, triX + triW / 2, y, COLOR_YELLOW);
    }
    if (moreBelow) {
        int16_t y = SCREEN_HEIGHT - triH - 2;
        gfx->fillTriangle(triX, y, triX + triW, y, triX + triW / 2, y + triH, COLOR_YELLOW);
    }
}