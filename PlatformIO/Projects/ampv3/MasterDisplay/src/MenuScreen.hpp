#pragma once
#include "Screen.hpp"
#include "MenuTypes.hpp"
#include "Config.hpp"

// Menu navigation engine. Content-agnostic -- init() is handed the root
// item array (defined elsewhere, in main.cpp, since it needs to reference
// things like available screens that only main.cpp has visibility into).
//
// Supports arbitrary-depth SUBMENU nesting (up to kMaxDepth) via a fixed-
// size navigation stack, with VALUE_CHOICE always treated as a leaf
// overlay on top of whichever stack level is currently focused, rather
// than a stack entry of its own -- a value-choice options list never
// itself nests further, matching how this kind of menu always behaves.
//
// Not part of the normal screen cycle (main.cpp handles switching in/out
// of it separately via UIEvent::MENU_TOGGLE) but still implements Screen
// for the same onEnter()/render() conventions.
//
// Unlike the audio-reactive screens, this doesn't need continuous 60fps
// redraw -- render() only actually draws when something changed (a menu
// is static between user actions), so a full-screen redraw per change is
// simple, correct, and cheap enough with no need for delta-tracking.
class MenuScreen : public Screen {
public:
    void init(const MenuItem* rootItems, size_t rootCount);

    void onEnter() override; // resets to the root list
    void render() override;  // no-op if nothing has changed since the last render
    const char* name() const override { return "MENU"; }

    void moveFocus(int delta); // -1 = up, +1 = down (wraps at the ends)
    void enter();              // RIGHT: performs the focused ACTION, drills into a VALUE_CHOICE's options or a SUBMENU's items, or selects a focused option and returns to its own list

    // LEFT: backs out one level (closing an open VALUE_CHOICE, or popping
    // one SUBMENU level, whichever is currently deepest). Returns true if
    // the menu is still open; returns false if already at the root list
    // with no value-choice open, meaning the caller should close the menu
    // entirely.
    bool back();

private:
    // Root, plus up to 3 nested SUBMENU levels (Root -> Theme -> FFT ->
    // one more if ever needed) -- generous headroom without the
    // complexity of a dynamically-sized stack, matching this project's
    // avoidance of heap allocation.
    static constexpr size_t kMaxDepth = 4;

    struct StackFrame {
        const MenuItem* items      = nullptr;
        size_t          itemCount  = 0;
        size_t          focusedIdx = 0;
    };

    void drawFull();

    StackFrame m_stack[kMaxDepth];
    size_t     m_stackDepth = 1; // always >= 1; index 0 is the root list

    // True when browsing the VALUE_CHOICE options of the item currently
    // focused at the TOP of m_stack -- an overlay on the current stack
    // level, not a level of its own.
    bool   m_inValueChoice = false;
    size_t m_optionIdx     = 0;

    bool m_dirty = true;
};