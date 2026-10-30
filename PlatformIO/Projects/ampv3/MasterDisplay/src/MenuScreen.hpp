#pragma once
#include "Screen.hpp"
#include "MenuTypes.hpp"
#include "Config.hpp"

// Menu navigation engine. Content-agnostic -- init() is handed the root item
// array (MenuContent.cpp).
//
// Look: ONE item at a time, in a big font, readable from across the room --
// a header line (where you are + "2/5" position), the focused item's label
// large in the middle, and a hint line under it (the current value of a
// setting, or what Right will do). Up/Down step through the items (wrapping);
// small arrows at the right edge show there's more.
//
// Supports arbitrary-depth SUBMENU nesting (up to kMaxDepth) via a fixed-size
// navigation stack. VALUE_CHOICE, INFO and EDITOR are leaf overlays on top of
// the current stack level, never levels of their own.
//
// Not part of the normal screen cycle (main.cpp switches in/out of it on
// UIEvent::MENU_TOGGLE) but implements Screen for onEnter()/render(). Only
// redraws when something changed.
class MenuScreen : public Screen {
public:
    void init(const MenuItem* rootItems, size_t rootCount);

    void onEnter() override; // resets to the root list
    void render() override;  // no-op if nothing has changed since the last render
    const char* name() const override { return "MENU"; }

    void moveFocus(int delta); // -1 = up, +1 = down; in an editor: up = increase the focused field

    // For jumping straight to a place in the menu (e.g. from a notice):
    // focus item `index` of the current list, and open the focused item's
    // option list directly (an EDITOR's follow-up choice, without the editor).
    void focus(size_t index);
    void openOptions();
    void enter();              // RIGHT: open / select / next field (save on the last one)

    // LEFT: backs out one level (closing an open option list / page / editor,
    // or popping one SUBMENU level). Returns false if already at the root
    // with nothing open, meaning the caller should close the menu.
    bool back();

    // True while an editor is open -- main.cpp then lets held buttons and IR
    // repeat frames auto-repeat Up/Down, so values can be run up quickly.
    bool acceptsRepeats() const { return m_inEditor || m_inColor || m_inSlider; }

    // The physical Up/Down buttons, read from InputEngine every loop while
    // the menu is open: one focus step per press, and -- where the menu
    // accepts repeats (editors, the colour chooser, sliders) -- one step
    // every MENU_REPEAT_RATE_MS after MENU_REPEAT_DELAY_MS of holding.
    void handleUpDownButtons();

    // Redraw from scratch next frame, keeping the place in the menu (onEnter()
    // starts over at the top) -- for when the menu becomes visible again.
    void invalidate() { m_dirty = true; }

private:
    // Root -> Settings -> Clock -> (overlay) needs 3; one spare level.
    static constexpr size_t kMaxDepth = 4;

    struct StackFrame {
        const MenuItem* items      = nullptr;
        size_t          itemCount  = 0;
        size_t          focusedIdx = 0;
    };

    const MenuItem& focusedItem() const { const StackFrame& f = m_stack[m_stackDepth - 1]; return f.items[f.focusedIdx]; }

    void drawFull();
    void drawHeader(const char* title, size_t index, size_t count);
    void drawInfo();
    bool refillInfo();        // re-runs the open INFO item's fillInfo; true if the text changed
    size_t infoVisibleRows() const;
    void drawEditorFull();
    void drawEditorFields();  // just the value row -- no screen clear, so adjusting doesn't flicker
    void clampEditorFields();
    void drawColorFull();
    void drawSliderFull();
    void drawSliderBody(); // the bar, the knob and the readout
    void drawColorFocus(int16_t oldIdx); // move the focus marker, refresh the swatch

    StackFrame m_stack[kMaxDepth];
    size_t     m_stackDepth = 1; // always >= 1; index 0 is the root list

    // VALUE_CHOICE overlay: browsing the options of the focused item.
    bool   m_inValueChoice = false;
    size_t m_optionIdx     = 0;

    // INFO overlay: a live read-only page; Up/Down scroll it.
    bool     m_inInfo         = false;
    size_t   m_infoScroll     = 0;
    size_t   m_infoCount      = 0;
    uint32_t m_lastInfoFillMs = 0;
    char     m_infoLines[kInfoMaxLines][kInfoLineLen] = {};

    // EDITOR overlay: numeric fields being adjusted.
    bool    m_inEditor    = false;
    uint8_t m_editorField = 0;
    int16_t m_editorValues[kEditorMaxFields] = {};

    // COLOR overlay: the color chooser.
    bool    m_inColor  = false;
    // SLIDER overlay.
    bool    m_inSlider       = false;
    int16_t m_sliderValue    = 0;
    int16_t m_sliderOriginal = 0;
    int16_t m_colorIdx = 0;
    int16_t m_colorDrawnIdx = -1;

    bool m_dirty             = true; // full redraw needed
    bool m_editorFieldsDirty = false;
    uint32_t m_holdStartMs  = 0;   // handleUpDownButtons(): the held-key repeat
    uint32_t m_lastRepeatMs = 0;
    // The list view (proportional fonts) and live labels (MenuItem::describe).
    void drawListHeader(const char* title, size_t index, size_t count);
    void drawListLine(const char* text, bool big, int16_t y, uint16_t color);
    void rememberDescribe(const MenuItem& item);
    bool describeChanged(const MenuItem& item) const;
    uint32_t m_describeHash    = 0;
    uint32_t m_describeCheckMs = 0;
};
