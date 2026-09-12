#pragma once
#include <Arduino.h>
#include <UIEvent.hpp>

// IR remote code -> UIEvent mapping, confirmed against the actual remote.
constexpr uint16_t IR_CODE_MODE_NEXT           = 0x50; // arrow right icon
constexpr uint16_t IR_CODE_MODE_PREV           = 0x51; // arrow left icon
constexpr uint16_t IR_CODE_GAIN_UP             = 0x16; // arrow up icon
constexpr uint16_t IR_CODE_GAIN_DOWN           = 0x1A; // arrow down icon
constexpr uint16_t IR_CODE_MENU_TOGGLE         = 0x11; // home icon
constexpr uint16_t IR_CODE_MUTE_TOGGLE         = 0x41; // mute icon
constexpr uint16_t IR_CODE_CHANNEL_MODE_TOGGLE = 0x4C; // options icon
// 0x44 (green KD icon top right) is free -- previously IR_CODE_COLOR_TOGGLE
// (bin-mapping mode toggle), removed along with that feature.

// Whether a mapped IR code should fire its event once per physical press,
// ignoring repeat frames the remote sends while the button stays held, or
// on every decode including repeats:
//  - ONE_SHOT: for discrete/toggle actions (mode switching, mute toggle,
//    etc). Without this, a press lasting even slightly longer than one
//    repeat interval (~110ms) fires the event twice, which for a two-way
//    toggle looks like it did nothing (or skips past the intended state
//    with more than two options).
//  - CONTINUOUS: for actions that should keep firing while held (volume
//    up/down) -- these rely on repeats to know the remote button is still
//    down.
enum class IrRepeatPolicy : uint8_t { ONE_SHOT, CONTINUOUS };

struct IrMapping {
    uint16_t       code;
    UIEvent        event;
    IrRepeatPolicy policy;
};

// Add a new entry here whenever a new IR_CODE_* is defined above -- this
// is the one place code, event, and repeat behavior all have to agree.
constexpr IrMapping kIrMappings[] = {
    { IR_CODE_MODE_NEXT,           UIEvent::MODE_NEXT,           IrRepeatPolicy::ONE_SHOT   },
    { IR_CODE_MODE_PREV,           UIEvent::MODE_PREV,           IrRepeatPolicy::ONE_SHOT   },
    { IR_CODE_GAIN_UP,             UIEvent::GAIN_UP,             IrRepeatPolicy::CONTINUOUS },
    { IR_CODE_GAIN_DOWN,           UIEvent::GAIN_DOWN,           IrRepeatPolicy::CONTINUOUS },
    { IR_CODE_MENU_TOGGLE,         UIEvent::MENU_TOGGLE,         IrRepeatPolicy::ONE_SHOT   },
    { IR_CODE_MUTE_TOGGLE,         UIEvent::MUTE_TOGGLE,         IrRepeatPolicy::ONE_SHOT   },
    { IR_CODE_CHANNEL_MODE_TOGGLE, UIEvent::CHANNEL_MODE_TOGGLE, IrRepeatPolicy::ONE_SHOT   },
};