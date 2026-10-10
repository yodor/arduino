#include "MenuScreen.hpp"
#include "Renderer.hpp"
#include <cstring>

void MenuScreen::init(const MenuItem* rootItems, size_t rootCount) {
    m_stack[0] = { rootItems, rootCount, 0 };
}

void MenuScreen::onEnter() {
    m_stackDepth          = 1;
    m_stack[0].focusedIdx = 0;
    m_inValueChoice       = false;
    m_dirty               = true;
}

void MenuScreen::moveFocus(int delta) {
    if (m_inValueChoice) {
        const StackFrame& frame = m_stack[m_stackDepth - 1];
        if (frame.itemCount == 0) return;
        const MenuItem& item = frame.items[frame.focusedIdx];
        if (item.optionCount == 0) return;

        int idx = static_cast<int>(m_optionIdx) + delta;
        if (idx < 0) idx = static_cast<int>(item.optionCount) - 1;
        if (idx >= static_cast<int>(item.optionCount)) idx = 0;
        m_optionIdx = static_cast<size_t>(idx);
    } else {
        StackFrame& frame = m_stack[m_stackDepth - 1];
        if (frame.itemCount == 0) return;

        int idx = static_cast<int>(frame.focusedIdx) + delta;
        if (idx < 0) idx = static_cast<int>(frame.itemCount) - 1;
        if (idx >= static_cast<int>(frame.itemCount)) idx = 0;
        frame.focusedIdx = static_cast<size_t>(idx);
    }
    m_dirty = true;
}

void MenuScreen::enter() {
    StackFrame& frame = m_stack[m_stackDepth - 1];
    if (frame.itemCount == 0) return;

    if (m_inValueChoice) {
        const MenuItem& item = frame.items[frame.focusedIdx];
        if (item.optionCount == 0) return;
        const MenuValueOption& opt = item.options[m_optionIdx];
        if (opt.onSelect) opt.onSelect();
        m_inValueChoice = false; // return to whichever list we drilled in from -- not necessarily root
        m_dirty         = true;
        return;
    }

    const MenuItem& item = frame.items[frame.focusedIdx];
    if (item.type == MenuItemType::ACTION) {
        if (item.onAction) item.onAction();
        m_dirty = true; // stays at the current list; redraw in case anything visible depends on the action
    } else if (item.type == MenuItemType::VALUE_CHOICE) {
        m_inValueChoice = true;
        m_optionIdx     = 0;
        m_dirty         = true;
    } else { // SUBMENU
        if (m_stackDepth >= kMaxDepth) return; // safety: never overrun the fixed stack
        m_stack[m_stackDepth] = { item.submenuItems, item.submenuCount, 0 };
        ++m_stackDepth;
        m_dirty = true;
    }
}

bool MenuScreen::back() {
    if (m_inValueChoice) {
        m_inValueChoice = false;
        m_dirty         = true;
        return true;
    }
    if (m_stackDepth > 1) {
        --m_stackDepth;
        m_dirty = true;
        return true;
    }
    return false; // already at the root list with nothing open -- caller closes the menu
}

void MenuScreen::render() {
    if (!m_dirty) return;
    drawFull();
    m_dirty = false;
}

void MenuScreen::drawFull() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    const StackFrame& frame = m_stack[m_stackDepth - 1];
    if (!gfx) return;

    gfx->fillScreen(COLOR_BLACK);

    constexpr uint16_t titleY   = MENU_TITLE_Y;
    constexpr uint16_t listTopY = MENU_LIST_TOP_Y;
    constexpr uint16_t rowH     = MENU_ROW_HEIGHT;
    constexpr uint8_t  textSize = MENU_TEXT_SIZE;

    gfx->setTextSize(textSize);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    gfx->setCursor(2, titleY);

    // Title: the focused VALUE_CHOICE's own label while browsing its
    // options; otherwise the label of the item that led into the current
    // list (i.e. the parent frame's focused item) -- or "MENU" at the
    // root, which has no parent to name it after.
    if (m_inValueChoice) {
        gfx->print(frame.items[frame.focusedIdx].label);
    } else if (m_stackDepth == 1) {
        gfx->print("MENU");
    } else {
        const StackFrame& parent = m_stack[m_stackDepth - 2];
        gfx->print(parent.items[parent.focusedIdx].label);
    }

    gfx->drawFastHLine(0, listTopY - 4, SCREEN_WIDTH, COLOR_DIM_GREEN);

    size_t visibleRows = (SCREEN_HEIGHT - listTopY) / rowH;
    if (visibleRows == 0) return;

    size_t itemCount;
    size_t focusedIdx;

    if (m_inValueChoice) {
        itemCount  = frame.items[frame.focusedIdx].optionCount;
        focusedIdx = m_optionIdx;
    } else {
        itemCount  = frame.itemCount;
        focusedIdx = frame.focusedIdx;
    }

    if (itemCount == 0) {
        gfx->setTextColor(COLOR_DIM_GREEN, COLOR_BLACK);
        gfx->setCursor(4, listTopY + 1);
        gfx->print("(empty)");
        return;
    }

    size_t scrollTop = (focusedIdx >= visibleRows) ? (focusedIdx - visibleRows + 1) : 0;

    if (m_inValueChoice) {
        const MenuItem& activeItem = frame.items[frame.focusedIdx];

        for (size_t row = 0; row < visibleRows && (scrollTop + row) < activeItem.optionCount; ++row) {
            size_t idx = scrollTop + row;
            const MenuValueOption& opt = activeItem.options[idx];
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
    } else {
        for (size_t row = 0; row < visibleRows && (scrollTop + row) < frame.itemCount; ++row) {
            size_t idx = scrollTop + row;
            const MenuItem& item = frame.items[idx];
            bool   focused = (idx == frame.focusedIdx);
            uint16_t y = listTopY + row * rowH;

            if (focused) {
                gfx->fillRect(0, y, SCREEN_WIDTH, rowH, COLOR_WHITE);
                gfx->setTextColor(COLOR_BLACK, COLOR_WHITE);
            } else {
                gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
            }
            gfx->setCursor(4, y + 1);
            gfx->print(item.label);
            // Trailing arrow hints "drills in further", distinguishing a
            // SUBMENU from an ACTION that fires immediately -- VALUE_
            // CHOICE isn't marked this way since its own active-value
            // marker (once you're inside it) already signals its nature.
            if (item.type == MenuItemType::SUBMENU) {
                int16_t textWidth = static_cast<int16_t>(strlen(item.label)) * 6 * textSize; // approximate default-font advance
                gfx->setCursor(4 + textWidth + 4, y + 1);
                gfx->print(">");
            }
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