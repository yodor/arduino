#pragma once
#include <Arduino.h>

// Menu data model: a root list of items, where each item is an immediate
// ACTION (fires and returns to its own list), a VALUE_CHOICE (drilling in
// shows a flat list of options; selecting one applies it and returns to
// its own list), or a SUBMENU (drilling in shows another list of
// MenuItems, at arbitrary nesting depth up to MenuScreen::kMaxDepth). A
// VALUE_CHOICE is always a leaf -- its options list never itself contains
// a further SUBMENU or VALUE_CHOICE, matching how this kind of menu
// always behaves in practice.
//
// Plain C-style function pointers rather than std::function, consistent
// with the rest of this project's avoidance of heap/STL machinery -- menu
// content is defined as static const data, no dynamic allocation.

enum class MenuItemType : uint8_t {
    ACTION,       // onAction() fires immediately, then stays at the current list
    VALUE_CHOICE, // drilling in shows `options`; selecting one applies it and returns to the current list
    SUBMENU,      // drilling in shows `submenuItems`, a whole separate MenuItem list one level deeper
    INFO,         // drilling in shows a read-only text page filled by `fillInfo`, refreshed live while open
    EDITOR,       // drilling in shows a row of numeric fields (`editor`) to adjust and save, e.g. a date or a time;
                  // if the item also has `options`, they're offered right after the save (e.g. Cancel / Start),
                  // and an option with an action then opens `fillInfo`'s page, as for VALUE_CHOICE
    COLOR,        // drilling in shows the color chooser (`color`): a spectrum bar to pick from, Right sets it
    SLIDER        // drilling in shows a horizontal slider (`slider`): Up/Down move it (applied live), Right keeps, Left restores
};

// SLIDER items: an integer range around a centre, applied live while it moves.
struct MenuSlider {
    int16_t     min, max;
    int16_t   (*get)();
    void      (*set)(int16_t);                       // called on every step (live) and on cancel (the start value)
    void      (*format)(int16_t, char*, size_t);     // the readout, e.g. "Left -1.50 dB" / "Center"
    const char* leftLabel;                           // at the bar's ends, e.g. "L" / "R"
    const char* rightLabel;
};

// COLOR items: the chooser offers kMenuPaletteSize colors -- the hue circle at
// full brightness, then white -- across the panel; it opens on the palette
// color nearest the current one.
constexpr uint8_t kMenuPaletteSize = 61;
uint16_t menuPaletteColor(uint8_t index);           // RGB565 of a palette entry (MenuScreen.cpp)
struct MenuColor {
    uint16_t (*get)();          // the current color (RGB565)
    void     (*set)(uint16_t);  // apply the chosen one
};

// INFO pages: up to kInfoMaxLines lines of up to kInfoLineLen-1 characters.
// They're shown large (3 rows at a time), where only kInfoCols characters fit
// across the panel -- longer lines are clipped, so keep them to that. fillInfo writes
// the lines and returns how many it used; it's called again every
// kInfoRefreshMs while the page is open, and the page only redraws when the
// text actually changed.
constexpr size_t   kInfoMaxLines  = 24;
constexpr size_t   kInfoLineLen   = 36;
constexpr size_t   kInfoCols      = 23;
constexpr uint32_t kInfoRefreshMs = 500;
using MenuInfoFill = size_t (*)(char (*lines)[kInfoLineLen], size_t maxLines);

// EDITOR items: up to kEditorMaxFields numeric fields shown in one row,
// zero-padded to `digits[i]` and joined by `separator` (e.g. 21:05:00 or
// 2026-10-20). Up/Down change the focused field (wrapping within its
// limits), Right moves to the next field and saves on the last one, Left
// moves back and cancels on the first one.
constexpr uint8_t kEditorMaxFields = 6;
struct MenuEditor {
    uint8_t fieldCount;
    uint8_t digits[kEditorMaxFields];
    char    separator;  // drawn between fields; '\0' = none (e.g. a number edited one digit per field)
    // Fills the starting values; false = nothing to edit (the page won't open).
    bool (*load)(int16_t* values);
    // Valid range of one field -- may depend on the others (day vs. month).
    void (*limits)(const int16_t* values, uint8_t field, int16_t* minOut, int16_t* maxOut);
    // Applies the result; false = it failed (logged by the callback).
    bool (*save)(const int16_t* values);
    // Optional: shows a field as text instead of a zero-padded number (e.g.
    // "AUTO" / "650K"). Must always produce exactly digits[field] characters
    // so the row keeps its layout; nullptr = plain number.
    void (*format)(const int16_t* values, uint8_t field, char* out, size_t outSize) = nullptr;
    // Optional: the text under the item in the list (default: the current
    // value as the editor would show it).
    void (*describe)(char* out, size_t outSize) = nullptr;
};

struct MenuValueOption {
    const char* label;
    void (*onSelect)();   // called when this option is chosen
    bool (*isActive)();   // returns true if this option is the CURRENTLY active value -- drawn with a marker so the user can see the current setting without changing it
};

struct MenuItem {
    const char* label;
    MenuItemType type;

    void (*onAction)() = nullptr; // ACTION only

    const MenuValueOption* options     = nullptr; // VALUE_CHOICE only
    size_t                 optionCount = 0;       // VALUE_CHOICE only

    const MenuItem* submenuItems = nullptr; // SUBMENU only
    size_t          submenuCount = 0;       // SUBMENU only

    MenuInfoFill    fillInfo     = nullptr; // INFO; or the page shown after a VALUE_CHOICE/EDITOR option with an action
                                            // is picked -- and, for an EDITOR, instead of the editor when it can't open

    const MenuEditor* editor     = nullptr; // EDITOR only

    const MenuColor*  color      = nullptr; // COLOR only

    const MenuSlider* slider     = nullptr; // SLIDER only
};
