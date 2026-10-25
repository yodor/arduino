#include "MenuScreen.hpp"
#include "Renderer.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <climits>

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

// INFO pages: large text, 3 rows at a time
constexpr uint8_t  kInfoTextSize = 3;   // 18x24 px characters -> 23 per line (kInfoCols)
constexpr int16_t  kInfoTopY     = 32;
constexpr uint16_t kInfoRowH     = 36;

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

// --- Color palette (COLOR items) ---------------------------------------------

// Hue circle in kMenuPaletteSize-1 steps at full saturation and brightness,
// then white.
uint16_t menuPaletteColor(uint8_t index) {
    if (index >= kMenuPaletteSize - 1) return 0xFFFF;
    float h = index * 360.0f / (kMenuPaletteSize - 1);
    float x = 1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f);
    float r = 0, g = 0, b = 0;
    if      (h < 60)  { r = 1; g = x; }
    else if (h < 120) { r = x; g = 1; }
    else if (h < 180) { g = 1; b = x; }
    else if (h < 240) { g = x; b = 1; }
    else if (h < 300) { r = x; b = 1; }
    else              { r = 1; b = x; }
    return static_cast<uint16_t>((static_cast<uint16_t>(r * 31 + 0.5f) << 11) |
                                 (static_cast<uint16_t>(g * 63 + 0.5f) << 5) |
                                  static_cast<uint16_t>(b * 31 + 0.5f));
}

namespace {
// Palette entry closest to an arbitrary RGB565 color.
int16_t nearestPaletteIndex(uint16_t c) {
    auto r = [](uint16_t v) { return static_cast<int32_t>((v >> 11) & 0x1F) * 255 / 31; };
    auto g = [](uint16_t v) { return static_cast<int32_t>((v >> 5)  & 0x3F) * 255 / 63; };
    auto b = [](uint16_t v) { return static_cast<int32_t>( v        & 0x1F) * 255 / 31; };
    int16_t best = 0;
    int32_t bestD = INT32_MAX;
    for (uint8_t i = 0; i < kMenuPaletteSize; ++i) {
        uint16_t p = menuPaletteColor(i);
        int32_t dr = r(p) - r(c), dg = g(p) - g(c), db = b(p) - b(c);
        int32_t d = dr * dr + dg * dg + db * db;
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

// Chooser geometry: the spectrum bar, the focus marker, the swatch.
constexpr int16_t kBarTop    = 40;
constexpr int16_t kBarH      = 36;
constexpr int16_t kMarkTop   = 34;  // focus box from here...
constexpr int16_t kMarkBot   = 92;  // ...to the triangle's tip below the bar
constexpr int16_t kSwatchY   = 98;
constexpr int16_t kSwatchH   = 28;
int16_t stripeX(int16_t i) { return static_cast<int16_t>(static_cast<int32_t>(i) * SCREEN_WIDTH / kMenuPaletteSize); }
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
    m_inColor             = false;
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
    if (m_inColor) {
        // Up (delta -1) moves right along the bar, Down left; wraps.
        int16_t old = m_colorIdx;
        m_colorIdx = static_cast<int16_t>((m_colorIdx - delta + kMenuPaletteSize) % kMenuPaletteSize);
        if (!m_dirty) drawColorFocus(old);
        return;
    }
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

void MenuScreen::focus(size_t index) {
    StackFrame& frame = m_stack[m_stackDepth - 1];
    if (index < frame.itemCount) frame.focusedIdx = index;
    m_dirty = true;
}

void MenuScreen::openOptions() {
    const MenuItem& item = focusedItem();
    if (!item.options || item.optionCount == 0) return;
    m_inValueChoice = true;
    m_optionIdx     = 0; // the first option -- "Cancel" in the confirmations
    m_dirty         = true;
}

void MenuScreen::enter() {
    StackFrame& frame = m_stack[m_stackDepth - 1];
    if (frame.itemCount == 0) return;
    if (m_inInfo) return; // read-only page -- nothing to select
    if (m_inColor) {
        const MenuItem& it = frame.items[frame.focusedIdx];
        if (it.color && it.color->set) it.color->set(menuPaletteColor(static_cast<uint8_t>(m_colorIdx)));
        m_inColor = false; // Right = set, back to the list
        m_dirty   = true;
        return;
    }

    const MenuItem& item = frame.items[frame.focusedIdx];

    if (m_inEditor) {
        if (m_editorField + 1 < item.editor->fieldCount) {
            ++m_editorField;
            m_editorFieldsDirty = true;
        } else {
            item.editor->save(m_editorValues); // last field: Right = save
            m_inEditor = false;
            m_dirty    = true;
            // A follow-up choice (e.g. Cancel / Start), Cancel-first.
            if (item.options && item.optionCount > 0) {
                m_inValueChoice = true;
                m_optionIdx     = 0;
            }
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
            if (!item.editor || !item.editor->load(m_editorValues)) {
                // Nothing to edit right now (e.g. Rtotal while a calibration
                // runs): show the item's info page instead, if it has one.
                if (item.fillInfo) {
                    m_inInfo     = true;
                    m_infoScroll = 0;
                    refillInfo();
                    m_dirty      = true;
                }
                return;
            }
            m_inEditor    = true;
            m_editorField = 0;
            clampEditorFields();
            m_dirty       = true;
            break;
        case MenuItemType::COLOR:
            if (!item.color) return;
            m_inColor  = true;
            m_colorIdx = nearestPaletteIndex(item.color->get ? item.color->get() : 0xFFFF);
            m_dirty    = true;
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
    if (m_inColor) {
        m_inColor = false; // Left = cancel, nothing set
        m_dirty   = true;
        return true;
    }
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
    if (m_inColor)  { drawColorFull();  return; }

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
                if (ed && ed->describe) {
                    ed->describe(valueHint, sizeof(valueHint));
                    hint      = valueHint;
                    hintColor = COLOR_YELLOW;
                } else if (ed && ed->load(vals)) {
                    size_t len = 0;
                    for (uint8_t i = 0; i < ed->fieldCount && len + 12 < sizeof(valueHint); ++i) {
                        char part[12];
                        if (ed->format) ed->format(vals, i, part, sizeof(part));
                        else            snprintf(part, sizeof(part), "%0*d", ed->digits[i], static_cast<int>(vals[i]));
                        char sep[2] = {(i && ed->separator) ? ed->separator : '\0', '\0'};
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
            case MenuItemType::COLOR:   hint = ""; break; // a swatch is drawn instead, below
        }
    }

    drawHeader(title, index, count);

    uint8_t size = (textWidth(label, kLabelSize) <= SCREEN_WIDTH - 20) ? kLabelSize : kLabelSizeSmall;
    drawCentered(gfx, label, size, kLabelCenterY - kCharH * size / 2, COLOR_WHITE);
    if (hint[0]) drawCentered(gfx, hint, kHintSize, kHintY, hintColor);
    if (!m_inValueChoice && item.type == MenuItemType::COLOR && item.color && item.color->get) {
        constexpr int16_t w = 96, h = 16;
        gfx->fillRect((SCREEN_WIDTH - w) / 2, kHintY, w, h, item.color->get());
    }

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
    gfx->setTextWrap(false); // a line longer than kInfoCols is clipped, never spills into the next row
    for (size_t row = 0; row < visible && (m_infoScroll + row) < m_infoCount; ++row) {
        gfx->setCursor(4, kInfoTopY + row * kInfoRowH);
        gfx->print(m_infoLines[m_infoScroll + row]);
    }
    gfx->setTextWrap(true);

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
    if (ed->separator) chars += ed->fieldCount - 1; // separators ('\0' = none: e.g. one digit per field)
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

        if (i + 1 < ed->fieldCount && ed->separator) {
            char sep[2] = {ed->separator, '\0'};
            gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);
            gfx->setCursor(x, kEditorY);
            gfx->print(sep);
            x += kCharW * kEditorSize;
        }
    }
}

// --- COLOR ----------------------------------------------------------------

void MenuScreen::drawColorFull() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    gfx->fillScreen(COLOR_BLACK);
    drawHeader(focusedItem().label, 0, 0);
    for (int16_t i = 0; i < kMenuPaletteSize; ++i) {
        gfx->fillRect(stripeX(i), kBarTop, stripeX(i + 1) - stripeX(i), kBarH, menuPaletteColor(static_cast<uint8_t>(i)));
    }
    drawCentered(gfx, "UP/DOWN choose   RIGHT set   LEFT cancel", 1, SCREEN_HEIGHT - 10, kHintColor);
    m_colorDrawnIdx = -1;
    drawColorFocus(-1);
}

// Repaints the stripes around the old focus (wiping its marker), draws the
// marker -- a white box around the stripe and a triangle under it -- at the
// new focus, and fills the swatch with the focused color.
void MenuScreen::drawColorFocus(int16_t oldIdx) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (oldIdx >= 0) {
        int16_t lo = oldIdx - 1 < 0 ? 0 : oldIdx - 1;
        int16_t hi = oldIdx + 1 >= kMenuPaletteSize ? kMenuPaletteSize - 1 : oldIdx + 1;
        int16_t x0 = stripeX(lo) - 3, x1 = stripeX(hi + 1) + 3;
        if (x0 < 0) x0 = 0;
        if (x1 > SCREEN_WIDTH) x1 = SCREEN_WIDTH;
        gfx->fillRect(x0, kMarkTop, x1 - x0, kBarTop - kMarkTop, COLOR_BLACK);
        gfx->fillRect(x0, kBarTop + kBarH, x1 - x0, kMarkBot - (kBarTop + kBarH) + 1, COLOR_BLACK);
        for (int16_t i = lo; i <= hi; ++i) {
            gfx->fillRect(stripeX(i), kBarTop, stripeX(i + 1) - stripeX(i), kBarH, menuPaletteColor(static_cast<uint8_t>(i)));
        }
        // The box overhangs into the stripes beyond lo/hi by up to 3 px.
        if (lo > 0)                      gfx->fillRect(stripeX(lo) - 3, kBarTop, 3, kBarH, menuPaletteColor(static_cast<uint8_t>(lo - 1)));
        if (hi < kMenuPaletteSize - 1)   gfx->fillRect(stripeX(hi + 1), kBarTop, 3, kBarH, menuPaletteColor(static_cast<uint8_t>(hi + 1)));
    }
    int16_t x0 = stripeX(m_colorIdx), x1 = stripeX(m_colorIdx + 1);
    gfx->drawRect(x0 - 2, kMarkTop + 2, (x1 - x0) + 4, kBarH + 6, COLOR_WHITE);
    gfx->drawRect(x0 - 3, kMarkTop + 1, (x1 - x0) + 6, kBarH + 8, COLOR_WHITE);
    int16_t cx = (x0 + x1) / 2;
    gfx->fillTriangle(cx - 6, kMarkBot, cx + 6, kMarkBot, cx, kBarTop + kBarH + 6, COLOR_WHITE);

    gfx->fillRect(12, kSwatchY, SCREEN_WIDTH - 24, kSwatchH, menuPaletteColor(static_cast<uint8_t>(m_colorIdx)));
    m_colorDrawnIdx = m_colorIdx;
}
