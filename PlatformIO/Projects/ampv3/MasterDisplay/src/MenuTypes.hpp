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
    SUBMENU       // drilling in shows `submenuItems`, a whole separate MenuItem list one level deeper
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
};