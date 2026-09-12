#pragma once

// Logical UI actions, decoupled from whatever physical source triggered
// them (a button press, an IR remote code, or in principle any future
// input source). Lives in its own header since both IRCodes.hpp (the
// IR-code-to-event mapping table) and InputEngine.hpp (the class that
// produces these events) need it, and neither should have to own the
// other just to get this type.
enum class UIEvent {
    NONE,
    MODE_NEXT,
    MODE_PREV,
    GAIN_UP,
    GAIN_DOWN,
    MUTE_TOGGLE,
    CHANNEL_MODE_TOGGLE,
    MENU_TOGGLE
};