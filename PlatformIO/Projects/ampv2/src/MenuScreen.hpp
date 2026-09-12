#pragma once
#include "Screen.hpp"
#include "MenuTypes.hpp"
#include "Config.hpp"

// Menu navigation engine. Content-agnostic -- init() is handed the root
// item array (defined elsewhere, in main.cpp, since it needs to reference
// things like available screens that only main.cpp has visibility into).
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
    void enter();              // RIGHT: performs the focused ACTION, or drills into a VALUE_CHOICE's options, or selects a focused option and returns to root

    // LEFT: backs out one level. Returns true if the menu is still open
    // (was in the options list, now back at root); returns false if
    // already at the root list, meaning the caller should close the menu
    // entirely.
    bool back();

private:
    enum class Level : uint8_t { ROOT, ITEM_OPTIONS };

    void drawFull();

    const MenuItem* m_rootItems = nullptr;
    size_t           m_rootCount = 0;

    Level  m_level         = Level::ROOT;
    size_t m_rootIdx        = 0; // focused item within the root list
    size_t m_activeItemIdx  = 0; // which root item we've drilled into (valid when m_level == ITEM_OPTIONS)
    size_t m_optionIdx      = 0; // focused option within the active item's list

    bool m_dirty = true;
};