#include "MenuScreen.hpp"
#include "Renderer.hpp"
#include <cstdio>
#include <cstring>

namespace {
// Default GFX font: 6x8 px per character at size 1.
constexpr int16_t kCharW = 6;
constexpr int16_t kCharH = 8;

// Header: where you are (left) and "2/5" (right), above a divider line.
constexpr uint8_t  kHeaderSize  = 2;
constexpr int16_t  kHeaderY     = 4;
constexpr int16_t  kDividerY    = 24;
constexpr uint16_t kHeaderColor = 0x8410; // gray

// The focused item: big and centered, with a hint line under it.
constexpr uint8_t  kLabelSize      = 4;  // 24x32 px characters -> 17 fit across
constexpr uint8_t  kLabelSizeSmall = 3;  // fallback for longer labels
constexpr int16_t  kLabelCenterY   = 62;
constexpr uint8_t  kHintSize       = 2;
constexpr int16_t  kHintY          = 100;
constexpr uint16_t kHintColor      = 0x8410;

// INFO pages
constexpr uint8_t  kInfoTextSize = 2;   // 12x16 px characters -> 35 per line
constexpr int16_t  kInfoTopY     = 30;
constexpr uint16_t kInfoRowH     = 18;

// EDITOR
constexpr uint8_t  kEditorSize  = 5;    // 30x40 px characters
constexpr int16_t  kEditorY     = 44;
constexpr uint16_t kEditorFocusBg = COLOR_YELLOW;

constexpr int16_t textWidth(const char* s, uint8_t size) {
    return static_cast<int16_t>(strlen(s)) * kCharW * size;
}

void drawCentered(Arduino_GFX* gfx, const char* text, uint8_t size, int16_t y, uint16_t color) {
    int16_t x = (SCREEN_WIDTH - textWidth(text, size)) / 2;
    if (x < 0) x = 0;
    gfx->setTextSize(size);
    gfx->setTextColor(color, COLOR_BLACK);
    gfx->setCursor(x, y);
    gfx->print(text);
}
} // namespace

void MenuScreen::init(const MenuItem* rootItems, size_t rootCount) {
    m_stack[0] = { rootItems, rootCount, 0 };
}

void MenuScreen::onEnter() {
    m_stackDepth          = 1;
    m_stack[0].focusedIdx = 0;
    m_inValueChoice       = false;
    m_inInfo              = false;
    m_inEditor            = false;
    m_dirty               = true;
}

void MenuScreen::clampEditorFields() {
    const MenuEditor* ed = focusedItem().editor;
    for (uint8_t i = 0; i < ed->fieldCount; ++i) {
        int16_t lo = 0, hi = 0;
        ed->limits(m_editorValues, i, &lo, &hi);
        if (m_editorValues[i] < lo) m_editorValues[i] = lo;
        if (m_editorValues[i] > hi) m_editorValues[i] = hi;
    }
}

void MenuScreen::moveFocus(int delta) {
    if (m_inEditor) {
        // Up (delta -1) increases the value, wrapping within the field's range.
        const MenuEditor* ed = focusedItem().editor;
        int16_t lo = 0, hi = 0;
        ed->limits(m_editorValues, m_editorField, &lo, &hi);
        int16_t v = m_editorValues[m_editorField] - delta;
        if (v > hi) v = lo;
        if (v < lo) v = hi;
        m_editorValues[m_editorField] = v;
        clampEditorFields(); // e.g. 31 -> 30 when the month changes
        m_editorFieldsDirty = true;
        return;
    }
    if (m_inInfo) {
        // Scroll, no wrap-around -- it's a page of text, not a list to pick from.
        size_t visible   = infoVisibleRows();
        size_t maxScroll = (m_infoCount > visible) ? (m_infoCount - visible) : 0;
        int    next      = static_cast<int>(m_infoScroll) + delta;
        if (next < 0) next = 0;
        if (next > static_cast<int>(maxScroll)) next = static_cast<int>(maxScroll);
        if (static_cast<size_t>(next) != m_infoScroll) {
            m_infoScroll = static_cast<size_t>(next);
            m_dirty = true;
        }
        return;
    }
    if (m_inValueChoice) {
        const MenuItem& item = focusedItem();
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
    if (m_inInfo) return; // read-only page -- nothing to select

    const MenuItem& item = frame.items[frame.focusedIdx];

    if (m_inEditor) {
        if (m_editorField + 1 < item.editor->fieldCount) {
            ++m_editorField;
            m_editorFieldsDirty = true;
        } else {
            item.editor->save(m_editorValues); // last field: Right = save
            m_inEditor = false;
            m_dirty    = true;
        }
        return;
    }

    if (m_inValueChoice) {
        if (item.optionCount == 0) return;
        const MenuValueOption& opt = item.options[m_optionIdx];
        if (opt.onSelect) opt.onSelect();
        m_inValueChoice = false; // back to the list it came from...
        m_dirty         = true;
        // ...unless the item also has an INFO page: then an option that did
        // something (e.g. "Start now") goes straight on to that live page.
        if (opt.onSelect && item.fillInfo) {
            m_inInfo     = true;
            m_infoScroll = 0;
            refillInfo();
        }
        return;
    }

    switch (item.type) {
        case MenuItemType::ACTION:
            if (item.onAction) item.onAction();
            m_dirty = true;
            break;
        case MenuItemType::INFO:
            if (!item.fillInfo) return;
            m_inInfo     = true;
            m_infoScroll = 0;
            refillInfo();
            m_dirty      = true;
            break;
        case MenuItemType::EDITOR:
            if (!item.editor || !item.editor->load(m_editorValues)) return;
            m_inEditor    = true;
            m_editorField = 0;
            clampEditorFields();
            m_dirty       = true;
            break;
        case MenuItemType::VALUE_CHOICE:
            // Open on the currently active option, so the first thing shown
            // is the current setting (a confirmation list with no active
            // option opens on its first entry, e.g. "Cancel").
            m_inValueChoice = true;
            m_optionIdx     = 0;
            for (size_t i = 0; i < item.optionCount; ++i) {
                if (item.options[i].isActive && item.options[i].isActive()) { m_optionIdx = i; break; }
            }
            m_dirty = true;
            break;
        case MenuItemType::SUBMENU:
            if (m_stackDepth >= kMaxDepth) return; // never overrun the fixed stack
            m_stack[m_stackDepth] = { item.submenuItems, item.submenuCount, 0 };
            ++m_stackDepth;
            m_dirty = true;
            break;
    }
}

bool MenuScreen::back() {
    if (m_inEditor) {
        if (m_editorField > 0) {
            --m_editorField;
            m_editorFieldsDirty = true;
        } else {
            m_inEditor = false; // Left on the first field = cancel, nothing saved
            m_dirty    = true;
        }
        return true;
    }
    if (m_inInfo || m_inValueChoice) {
        m_inInfo        = false;
        m_inValueChoice = false;
        m_dirty         = true;
        return true;
    }
    if (m_stackDepth > 1) {
        --m_stackDepth;
        m_dirty = true;
        return true;
    }
    return false; // at the root with nothing open -- caller closes the menu
}

void MenuScreen::render() {
    // INFO pages show live data: re-fill periodically, redraw only on change.
    if (m_inInfo && (millis() - m_lastInfoFillMs >= kInfoRefreshMs)) {
        if (refillInfo()) m_dirty = true;
    }
    if (m_dirty) {
        drawFull();
        m_dirty = false;
        m_editorFieldsDirty = false;
    } else if (m_editorFieldsDirty) {
        drawEditorFields();
        m_editorFieldsDirty = false;
    }
}

// Header: location on the left, "n/total" on the right, divider under it.
void MenuScreen::drawHeader(const char* title, size_t index, size_t count) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    gfx->setTextSize(kHeaderSize);
    gfx->setTextColor(kHeaderColor, COLOR_BLACK);
    gfx->setCursor(6, kHeaderY);
    gfx->print(title);

    if (count > 0) {
        char pos[12];
        snprintf(pos, sizeof(pos), "%u/%u", static_cast<unsigned>(index + 1), static_cast<unsigned>(count));
        int16_t posX = SCREEN_WIDTH - 6 - textWidth(pos, kHeaderSize);
        gfx->setCursor(posX, kHeaderY);
        gfx->print(pos);

        // Up/down arrows next to the position: there's more to step through
        // (the list wraps, so both whenever there's more than one entry).
        if (count > 1) {
            constexpr int16_t w = 10, h = 6;
            int16_t x = posX - w - 8;
            gfx->fillTriangle(x, kHeaderY + h, x + w, kHeaderY + h, x + w / 2, kHeaderY, kHeaderColor);
            gfx->fillTriangle(x, kHeaderY + 9, x + w, kHeaderY + 9, x + w / 2, kHeaderY + 9 + h, kHeaderColor);
        }
    }
    gfx->drawFastHLine(0, kDividerY, SCREEN_WIDTH, COLOR_DIM_GREEN);
}

void MenuScreen::drawFull() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;
    if (m_inInfo)   { drawInfo();       return; }
    if (m_inEditor) { drawEditorFull(); return; }

    gfx->fillScreen(COLOR_BLACK);

    const StackFrame& frame = m_stack[m_stackDepth - 1];
    if (frame.itemCount == 0) {
        drawHeader("MENU", 0, 0);
        drawCentered(gfx, "(empty)", kLabelSize, kLabelCenterY - kCharH * kLabelSize / 2, kHintColor);
        return;
    }
    const MenuItem& item = frame.items[frame.focusedIdx];

    const char* title;
    const char* label;
    const char* hint      = "";
    uint16_t    hintColor = kHintColor;
    size_t      index, count;

    if (m_inValueChoice) {
        const MenuValueOption& opt = item.options[m_optionIdx];
        title = item.label;
        label = opt.label;
        if (opt.isActive && opt.isActive()) { hint = "current setting"; hintColor = COLOR_YELLOW; }
        index = m_optionIdx;
        count = item.optionCount;
    } else {
        title = (m_stackDepth == 1) ? "MENU" : m_stack[m_stackDepth - 2].items[m_stack[m_stackDepth - 2].focusedIdx].label;
        label = item.label;
        index = frame.focusedIdx;
        count = frame.itemCount;
        switch (item.type) {
            case MenuItemType::VALUE_CHOICE:
                // Show the setting's current value right under its name.
                for (size_t i = 0; i < item.optionCount; ++i) {
                    if (item.options[i].isActive && item.options[i].isActive()) {
                        hint = item.options[i].label;
                        hintColor = COLOR_YELLOW;
                        break;
                    }
                }
                if (!hint[0]) hint = "choose >";
                break;
            case MenuItemType::SUBMENU: hint = "open >"; break;
            case MenuItemType::INFO:    hint = "view >"; break;
            case MenuItemType::EDITOR: {
                // The current value, as the editor would show it (e.g. the
                // time, or "AUTO" / "650K"); "edit >" if there's none.
                static char valueHint[32];
                hint = "edit >";
                int16_t vals[kEditorMaxFields] = {};
                const MenuEditor* ed = item.editor;
                if (ed && ed->load(vals)) {
                    size_t len = 0;
                    for (uint8_t i = 0; i < ed->fieldCount && len + 12 < sizeof(valueHint); ++i) {
                        char part[12];
                        if (ed->format) ed->format(vals, i, part, sizeof(part));
                        else            snprintf(part, sizeof(part), "%0*d", ed->digits[i], static_cast<int>(vals[i]));
                        char sep[2] = {i ? ed->separator : '\0', '\0'};
                        len += snprintf(valueHint + len, sizeof(valueHint) - len, "%s%s", sep, part);
                    }
                    // Trim the padding a fixed-width field may carry.
                    char* p = valueHint;
                    while (*p == ' ') ++p;
                    memmove(valueHint, p, strlen(p) + 1);
                    hint      = valueHint;
                    hintColor = COLOR_YELLOW;
                }
                break;
            }
            case MenuItemType::ACTION:  hint = item.onAction ? "run >" : ""; break;
        }
    }

    drawHeader(title, index, count);

    uint8_t size = (textWidth(label, kLabelSize) <= SCREEN_WIDTH - 20) ? kLabelSize : kLabelSizeSmall;
    drawCentered(gfx, label, size, kLabelCenterY - kCharH * size / 2, COLOR_WHITE);
    if (hint[0]) drawCentered(gfx, hint, kHintSize, kHintY, hintColor);

}

// --- INFO pages ---------------------------------------------------------

size_t MenuScreen::infoVisibleRows() const {
    return (SCREEN_HEIGHT - kInfoTopY) / kInfoRowH;
}

bool MenuScreen::refillInfo() {
    m_lastInfoFillMs = millis();
    const MenuItem& item = focusedItem();
    if (!item.fillInfo) return false;

    char   fresh[kInfoMaxLines][kInfoLineLen] = {};
    size_t count = item.fillInfo(fresh, kInfoMaxLines);
    if (count > kInfoMaxLines) count = kInfoMaxLines;

    bool changed = (count != m_infoCount) || memcmp(fresh, m_infoLines, sizeof(fresh)) != 0;
    if (changed) {
        memcpy(m_infoLines, fresh, sizeof(fresh));
        m_infoCount = count;
        size_t visible   = infoVisibleRows();
        size_t maxScroll = (m_infoCount > visible) ? (m_infoCount - visible) : 0;
        if (m_infoScroll > maxScroll) m_infoScroll = maxScroll;
    }
    return changed;
}

void MenuScreen::drawInfo() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    gfx->fillScreen(COLOR_BLACK);
    drawHeader(focusedItem().label, 0, 0);

    size_t visible = infoVisibleRows();
    gfx->setTextSize(kInfoTextSize);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
    for (size_t row = 0; row < visible && (m_infoScroll + row) < m_infoCount; ++row) {
        gfx->setCursor(4, kInfoTopY + row * kInfoRowH);
        gfx->print(m_infoLines[m_infoScroll + row]);
    }

    constexpr int16_t w = 12, h = 8, x = SCREEN_WIDTH - w - 6;
    if (m_infoScroll > 0) {
        gfx->fillTriangle(x, kInfoTopY + h, x + w, kInfoTopY + h, x + w / 2, kInfoTopY, COLOR_YELLOW);
    }
    if (m_infoScroll + visible < m_infoCount) {
        int16_t y = SCREEN_HEIGHT - h - 2;
        gfx->fillTriangle(x, y, x + w, y, x + w / 2, y + h, COLOR_YELLOW);
    }
}

// --- EDITOR ---------------------------------------------------------------

void MenuScreen::drawEditorFull() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    gfx->fillScreen(COLOR_BLACK);
    drawHeader(focusedItem().label, 0, 0);
    drawCentered(gfx, "UP/DOWN change   RIGHT next, save", 1, SCREEN_HEIGHT - 24, kHintColor);
    drawCentered(gfx, "LEFT back, cancel on the first field", 1, SCREEN_HEIGHT - 12, kHintColor);
    drawEditorFields();
}

// Fixed-width fields printed with their background color, so reprinting
// fully overwrites the old digits -- no clear, no flicker while adjusting.
void MenuScreen::drawEditorFields() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    const MenuEditor* ed = focusedItem().editor;

    int16_t chars = 0;
    for (uint8_t i = 0; i < ed->fieldCount; ++i) chars += ed->digits[i];
    chars += ed->fieldCount - 1; // separators
    int16_t x = (SCREEN_WIDTH - chars * kCharW * kEditorSize) / 2;

    gfx->setTextSize(kEditorSize);
    for (uint8_t i = 0; i < ed->fieldCount; ++i) {
        char buf[12];
        if (ed->format) ed->format(m_editorValues, i, buf, sizeof(buf));
        else            snprintf(buf, sizeof(buf), "%0*d", ed->digits[i], static_cast<int>(m_editorValues[i]));
        bool focused = (i == m_editorField);
        gfx->setTextColor(focused ? COLOR_BLACK : COLOR_WHITE, focused ? kEditorFocusBg : COLOR_BLACK);
        gfx->setCursor(x, kEditorY);
        gfx->print(buf);
        x += ed->digits[i] * kCharW * kEditorSize;

        if (i + 1 < ed->fieldCount) {
            char sep[2] = {ed->separator, '\0'};
            gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
            gfx->setCursor(x, kEditorY);
            gfx->print(sep);
            x += kCharW * kEditorSize;
        }
    }
}
