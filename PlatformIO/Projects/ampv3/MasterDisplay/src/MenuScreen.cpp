#include "MenuScreen.hpp"
#include "InputEngine.hpp"
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
constexpr uint8_t  kHeaderSize  = 2;    // the header of INFO pages, editors, color, slider
constexpr int16_t  kHeaderY     = 4;
constexpr int16_t  kDividerY    = 24;
// The list view follows the Info screen's three bands: the header (as above),
// the item's name at the size of its big dB number, and the line under it at
// the size and grey of its unit label -- two rows when it needs them.
constexpr uint8_t  kItemSize    = 5;    // 30x40 px characters: 14 fit across
constexpr int16_t  kItemY       = 32;
constexpr int16_t  kItemMargin  = 4;
constexpr uint8_t  kSubSize     = 3;    // 18x24 px characters: 23 per row
constexpr int16_t  kSubCols     = 23;
constexpr int16_t  kSubY        = 95;   // one row
constexpr int16_t  kSubY1       = 81;   // two rows (24 px glyphs, 4 px apart)
constexpr int16_t  kSubY2       = 109;
// A name too long for the width scrolls: a pause, a slide to its end, a
// pause, back to the start.
constexpr uint32_t kMarqueePauseMs = 1500;
constexpr uint32_t kMarqueeStepMs  = 40;  // 2 px per step: 50 px/s
constexpr int16_t  kMarqueeStepPx  = 2;
constexpr uint16_t kHeaderColor = 0x8410; // gray

// The focused item: big and centered, with a hint line under it.
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
    m_inSlider            = false;
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
    if (m_inSlider) {
        // Up (delta -1) moves right, Down left; stops at the ends; live.
        const MenuSlider* sl = focusedItem().slider;
        int16_t v = static_cast<int16_t>(m_sliderValue - delta);
        if (v < sl->min) v = sl->min;
        if (v > sl->max) v = sl->max;
        if (v != m_sliderValue) {
            m_sliderValue = v;
            if (sl->set) sl->set(v);
            if (!m_dirty) drawSliderBody();
        }
        return;
    }
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
    if (m_inSlider) {
        m_inSlider = false; // Right = keep the value (already applied)
        m_dirty    = true;
        return;
    }
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
            // A follow-up choice (e.g. Cancel / Start), Cancel-first -- or, with
            // none, straight on to the item's page (e.g. the run it started).
            if (item.options && item.optionCount > 0) {
                m_inValueChoice = true;
                m_optionIdx     = 0;
            } else if (item.fillInfo) {
                m_inInfo     = true;
                m_infoScroll = 0;
                refillInfo();
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
            if (item.fillInfo) {          // e.g. Verify / Full: on to the run's progress
                m_inInfo     = true;
                m_infoScroll = 0;
                refillInfo();
            }
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
        case MenuItemType::SLIDER:
            if (!item.slider) return;
            m_inSlider       = true;
            m_sliderValue    = item.slider->get ? item.slider->get() : 0;
            m_sliderOriginal = m_sliderValue;
            m_dirty          = true;
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
    if (m_inSlider) {
        const MenuSlider* sl = focusedItem().slider;
        if (sl->set && m_sliderValue != m_sliderOriginal) sl->set(m_sliderOriginal); // Left = cancel
        m_inSlider = false;
        m_dirty    = true;
        return true;
    }
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
    // A long item name scrolls (list view only).
    if (!m_dirty && !m_inInfo && !m_inEditor && !m_inColor && !m_inSlider) marqueeStep();
    // A focused item with a live label: re-ask twice a second, redraw on change.
    if (!m_dirty && !m_inInfo && !m_inEditor && !m_inColor && !m_inSlider && !m_inValueChoice &&
        millis() - m_describeCheckMs >= 500) {
        m_describeCheckMs = millis();
        const StackFrame& frame = m_stack[m_stackDepth - 1];
        if (frame.itemCount && frame.items[frame.focusedIdx].describe && describeChanged(frame.items[frame.focusedIdx])) m_dirty = true;
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
// The item's name, centred at the big size -- or, if it's wider than the
// screen, left-aligned and scrolled by marqueeStep().
void MenuScreen::drawItemName(const char* text) {
    snprintf(m_marqueeText, sizeof(m_marqueeText), "%s", text);
    int16_t w = textWidth(m_marqueeText, kItemSize);
    m_marqueeActive = (w > SCREEN_WIDTH - 2 * kItemMargin);
    m_marqueeOffset = 0;
    m_marqueeAtEnd  = false;
    m_marqueeMs     = millis();
    if (!m_marqueeActive) {
        drawCentered(Renderer::instance().gfx(), m_marqueeText, kItemSize, kItemY, COLOR_WHITE);
    } else {
        drawMarquee();
    }
}

void MenuScreen::drawMarquee() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    gfx->setTextWrap(false);
    gfx->setTextSize(kItemSize);
    gfx->setTextColor(COLOR_WHITE, COLOR_BLACK);   // each cell paints its own background: no flicker
    gfx->setCursor(kItemMargin - m_marqueeOffset, kItemY);
    gfx->print(m_marqueeText);
    gfx->setTextWrap(true);
    constexpr int16_t h = kCharH * kItemSize;
    gfx->fillRect(0, kItemY, kItemMargin, h, COLOR_BLACK);                           // keep the margins clear
    gfx->fillRect(SCREEN_WIDTH - kItemMargin, kItemY, kItemMargin, h, COLOR_BLACK);
}

// Called every frame from render(): moves a long name along.
void MenuScreen::marqueeStep() {
    if (!m_marqueeActive) return;
    uint32_t now = millis();
    int16_t maxOffset = textWidth(m_marqueeText, kItemSize) - (SCREEN_WIDTH - 2 * kItemMargin);
    if (m_marqueeOffset == 0 && !m_marqueeAtEnd) {
        if (now - m_marqueeMs < kMarqueePauseMs) return;             // pause at the start
    } else if (m_marqueeAtEnd) {
        if (now - m_marqueeMs < kMarqueePauseMs) return;             // pause at the end, then back
        m_marqueeOffset = 0;
        m_marqueeAtEnd  = false;
        m_marqueeMs     = now;
        drawMarquee();
        return;
    }
    if (now - m_marqueeMs < kMarqueeStepMs) return;
    m_marqueeMs     = now;
    m_marqueeOffset = static_cast<int16_t>(m_marqueeOffset + kMarqueeStepPx);
    if (m_marqueeOffset >= maxOffset) { m_marqueeOffset = maxOffset; m_marqueeAtEnd = true; }
    drawMarquee();
}

// The line under the name: one row, or two split at a space; longer is cut.
void MenuScreen::drawSubText(const char* text, uint16_t color) {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    size_t len = strlen(text);
    if (len <= static_cast<size_t>(kSubCols)) { drawCentered(gfx, text, kSubSize, kSubY, color); return; }
    size_t cut = kSubCols;
    while (cut > 0 && text[cut] != ' ') --cut;
    if (cut == 0) cut = kSubCols;
    char row1[kSubCols + 1], row2[kSubCols + 1];
    snprintf(row1, sizeof(row1), "%.*s", static_cast<int>(cut), text);
    const char* rest = text + cut;
    while (*rest == ' ') ++rest;
    snprintf(row2, sizeof(row2), "%s", rest);
    drawCentered(gfx, row1, kSubSize, kSubY1, color);
    drawCentered(gfx, row2, kSubSize, kSubY2, color);
}

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
}

void MenuScreen::drawFull() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    if (!gfx) return;
    m_marqueeActive = false;   // set again below by the list view, if needed
    if (m_inInfo)   { drawInfo();       return; }
    if (m_inEditor) { drawEditorFull(); return; }
    if (m_inColor)  { drawColorFull();  return; }
    if (m_inSlider) { drawSliderFull(); return; }

    gfx->fillScreen(COLOR_BLACK);

    const StackFrame& frame = m_stack[m_stackDepth - 1];
    if (frame.itemCount == 0) {
        m_marqueeActive = false;
        drawHeader("MENU", 0, 0);
        drawCentered(gfx, "(empty)", kItemSize, kItemY, kHintColor);
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
            case MenuItemType::SLIDER: {
                static char sliderHint[32];
                hint = "";
                if (item.slider && item.slider->format && item.slider->get) {
                    item.slider->format(item.slider->get(), sliderHint, sizeof(sliderHint));
                    hint      = sliderHint;
                    hintColor = COLOR_YELLOW;
                }
                break;
            }
        }
    }

    // A live label / line from the item itself (e.g. the calibration state).
    static char descLabel[40], descHint[48];
    if (!m_inValueChoice && item.describe) {
        descLabel[0] = descHint[0] = '\0';
        uint16_t c = kHintColor;
        item.describe(descLabel, sizeof(descLabel), descHint, sizeof(descHint), &c);
        if (descLabel[0]) label = descLabel;
        if (descHint[0])  { hint = descHint; hintColor = c; }
    }
    rememberDescribe(item);

    drawHeader(title, index, count);
    drawItemName(label);
    if (hint[0]) drawSubText(hint, hintColor);
    if (!m_inValueChoice && item.type == MenuItemType::COLOR && item.color && item.color->get) {
        constexpr int16_t w = 96, h = 24;
        gfx->fillRect((SCREEN_WIDTH - w) / 2, kSubY, w, h, item.color->get());
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

// --- SLIDER -----------------------------------------------------------------

namespace {
constexpr int16_t kSlTrackX0 = 44, kSlTrackX1 = SCREEN_WIDTH - 44; // the track's ends
constexpr int16_t kSlTrackY  = 58, kSlTrackH = 8;
constexpr int16_t kSlKnobW   = 10, kSlKnobH  = 28;
constexpr int16_t kSlTop     = kSlTrackY + kSlTrackH / 2 - kSlKnobH / 2 - 2; // the redrawn band
constexpr int16_t kSlValueY  = 92;
constexpr uint16_t kSlTrackColor = 0x4208;
constexpr uint16_t kSlFillColor  = 0x07FF; // cyan: from the centre to the knob
} // namespace

void MenuScreen::drawSliderFull() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    gfx->fillScreen(COLOR_BLACK);
    const MenuSlider* sl = focusedItem().slider;
    drawHeader(focusedItem().label, 0, 0);
    gfx->setTextSize(2);
    gfx->setTextColor(kHintColor, COLOR_BLACK);
    gfx->setCursor(14, kSlTrackY - 4);
    gfx->print(sl->leftLabel ? sl->leftLabel : "");
    gfx->setCursor(SCREEN_WIDTH - 14 - 12 * static_cast<int16_t>(strlen(sl->rightLabel ? sl->rightLabel : "")), kSlTrackY - 4);
    gfx->print(sl->rightLabel ? sl->rightLabel : "");
    drawCentered(gfx, "UP/DOWN adjust   RIGHT keep   LEFT cancel", 1, SCREEN_HEIGHT - 10, kHintColor);
    drawSliderBody();
}

// The track with a centre mark, the part between the centre and the knob
// filled, the knob, and the readout below.
void MenuScreen::drawSliderBody() {
    Arduino_GFX* gfx = Renderer::instance().gfx();
    const MenuSlider* sl = focusedItem().slider;
    int16_t range = sl->max - sl->min;
    if (range <= 0) range = 1;
    auto xOf = [&](int16_t v) {
        return static_cast<int16_t>(kSlTrackX0 + static_cast<int32_t>(v - sl->min) * (kSlTrackX1 - kSlTrackX0) / range);
    };
    int16_t centreV = (sl->min <= 0 && sl->max >= 0) ? 0 : sl->min;
    int16_t xc = xOf(centreV), xk = xOf(m_sliderValue);

    gfx->fillRect(kSlTrackX0 - kSlKnobW, kSlTop, (kSlTrackX1 - kSlTrackX0) + 2 * kSlKnobW, kSlKnobH + 4, COLOR_BLACK);
    gfx->fillRect(kSlTrackX0, kSlTrackY, kSlTrackX1 - kSlTrackX0, kSlTrackH, kSlTrackColor);
    if (xk != xc) {
        int16_t a = xk < xc ? xk : xc, b = xk < xc ? xc : xk;
        gfx->fillRect(a, kSlTrackY, b - a, kSlTrackH, sl->fillColor ? sl->fillColor() : kSlFillColor);
    }
    gfx->fillRect(xc - 1, kSlTop, 2, kSlKnobH + 4, COLOR_WHITE);           // centre mark
    gfx->fillRect(xk - kSlKnobW / 2, kSlTrackY + kSlTrackH / 2 - kSlKnobH / 2, kSlKnobW, kSlKnobH, COLOR_WHITE); // knob

    char text[32] = "";
    if (sl->format) sl->format(m_sliderValue, text, sizeof(text));
    gfx->fillRect(0, kSlValueY, SCREEN_WIDTH, 24, COLOR_BLACK);
    drawCentered(gfx, text, 3, kSlValueY, COLOR_WHITE);
}

// --- The physical Up/Down buttons ----------------------------------------------

void MenuScreen::handleUpDownButtons() {
    InputEngine& in = InputEngine::instance();
    bool upPressed   = in.volUpJustPressed();
    bool downPressed = in.volDownJustPressed();
    if (upPressed)   moveFocus(-1);
    if (downPressed) moveFocus(1);

    int heldDir = in.isVolUpHeld() ? -1 : in.isVolDownHeld() ? 1 : 0;
    uint32_t now = millis();
    if (heldDir == 0 || upPressed || downPressed) {
        m_holdStartMs = m_lastRepeatMs = now;
    } else if (acceptsRepeats() &&
               now - m_holdStartMs >= MENU_REPEAT_DELAY_MS &&
               now - m_lastRepeatMs >= MENU_REPEAT_RATE_MS) {
        moveFocus(heldDir);
        m_lastRepeatMs = now;
    }
}

// --- Live labels ------------------------------------------------------------------

namespace {
uint32_t describeHash(const MenuItem& item) {
    char l[40] = "", h[48] = "";
    uint16_t c = 0;
    item.describe(l, sizeof(l), h, sizeof(h), &c);
    uint32_t x = 2166136261u ^ c;                      // FNV-1a over both strings
    for (const char* p = l; *p; ++p) x = (x ^ static_cast<uint8_t>(*p)) * 16777619u;
    x = (x ^ 0xFF) * 16777619u;
    for (const char* p = h; *p; ++p) x = (x ^ static_cast<uint8_t>(*p)) * 16777619u;
    return x;
}
} // namespace

void MenuScreen::rememberDescribe(const MenuItem& item) {
    m_describeHash = item.describe ? describeHash(item) : 0;
}

bool MenuScreen::describeChanged(const MenuItem& item) const {
    return describeHash(item) != m_describeHash;
}
