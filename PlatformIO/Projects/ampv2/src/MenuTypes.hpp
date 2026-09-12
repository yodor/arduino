#pragma once
#include <Arduino.h>

// Two-level menu data model: a root list of items, where each item is
// either an immediate ACTION (fires and returns to the root list) or a
// VALUE_CHOICE (drilling in shows a list of options; selecting one applies
// it and returns to the root list). No deeper nesting -- matches the
// menu's actual current content (Colors, Calibration, Stereo/Mono,
// Starting Screen) with no need for anything more general yet.
//
// Plain C-style function pointers rather than std::function, consistent
// with the rest of this project's avoidance of heap/STL machinery -- menu
// content is defined as static const data, no dynamic allocation.

enum class MenuItemType : uint8_t {
    ACTION,       // onAction() fires immediately, then stays at the root list
    VALUE_CHOICE  // drilling in shows `options`; selecting one applies it and returns to the root list
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

    const MenuValueOption* options    = nullptr; // VALUE_CHOICE only
    size_t                 optionCount = 0;      // VALUE_CHOICE only
};  