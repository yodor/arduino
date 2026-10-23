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
    EDITOR        // drilling in shows a row of numeric fields (`editor`) to adjust and save, e.g. a date or a time
};

// INFO pages: up to kInfoMaxLines lines of up to kInfoLineLen-1 characters
// (35 fit across the panel at the info page's text size). fillInfo writes
// the lines and returns how many it used; it's called again every
// kInfoRefreshMs while the page is open, and the page only redraws when the
// text actually changed.
constexpr size_t   kInfoMaxLines  = 16;
constexpr size_t   kInfoLineLen   = 36;
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
    char    separator;
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

    MenuInfoFill    fillInfo     = nullptr; // INFO; or a VALUE_CHOICE's page shown after an option with an action is picked

    const MenuEditor* editor     = nullptr; // EDITOR only
};
