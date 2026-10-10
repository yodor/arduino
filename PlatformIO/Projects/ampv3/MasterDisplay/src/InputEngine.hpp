#pragma once
#include "Config.hpp"
#include "UIEvent.hpp"

// Debounce state for one physical button. Supports three ways to read it:
//  - justPressed: fires once, the poll where the stable state transitions
//    to pressed. For plain single-action buttons.
//  - isHeld(): continuous level, for buttons that should drive something
//    for as long as they're down (e.g. the volume buttons).
//  - checkLongPress(now, thresholdMs): fires once per press, the first
//    poll where the button has been continuously held for at least
//    thresholdMs. Independent of justPressed/isHeld so a button can serve
//    a short-press role and a long-press role at once (see InputEngine::
//    pollEvents()'s handling of the LEFT button for how the two combine
//    without double-firing).
// update() must be called once per poll for the timing to behave
// correctly; the read methods only report whatever the last update()
// computed, they never re-read the pin themselves.
struct DebouncedButton {
    uint8_t  pin          = 255;
    bool     lastRawState = HIGH;
    bool     stableState  = HIGH;
    uint32_t lastChangeMs = 0;
    bool     justPressed  = false; // true for exactly the one update() call where the stable state transitions to pressed
    bool     justReleased = false; // true for exactly the one update() call where the stable state transitions to released

    uint32_t pressedSinceMs = 0; // timestamp of the most recent justPressed; only meaningful while isHeld()
    bool     longPressFired = false; // true once checkLongPress() has already fired for the CURRENT press, so it only fires once

    void begin(uint8_t p) {
        pin = p;
        if (pin != 255) pinMode(pin, INPUT_PULLUP);
    }

    void update(uint32_t now, uint32_t debounceMs) {
        justPressed  = false;
        justReleased = false;
        if (pin == 255) return;

        bool reading = digitalRead(pin);
        if (reading != lastRawState) {
            // Raw signal changed (a real press/release or just contact
            // bounce) -- restart the debounce timer either way.
            lastChangeMs = now;
            lastRawState = reading;
        }

        if ((now - lastChangeMs) > debounceMs && reading != stableState) {
            // Signal has held steady long enough to trust it: this is the
            // real, settled state change.
            stableState = reading;
            if (stableState == LOW) {
                justPressed    = true;
                pressedSinceMs = now;
                longPressFired = false;
            } else {
                justReleased = true;
            }
        }
    }

    bool isHeld() const { return stableState == LOW; }

    // Checked explicitly by the caller with whatever threshold that
    // caller wants (kept generic here rather than hardcoding a specific
    // feature's duration into the button primitive itself).
    bool checkLongPress(uint32_t now, uint32_t thresholdMs) {
        if (!isHeld() || longPressFired) return false;
        if (now - pressedSinceMs >= thresholdMs) {
            longPressFired = true;
            return true;
        }
        return false;
    }
};

class InputEngine {
public:
    static InputEngine& instance();

    void init(uint8_t btnNextPin, uint8_t btnPrevPin, uint8_t irPin, uint8_t btnVolUpPin, uint8_t btnVolDownPin);
    UIEvent pollEvents();

    // Reads the flag pollEvents() already computed this poll -- doesn't
    // re-debounce or touch the pin itself. Call pollEvents() once per loop
    // iteration as usual before relying on these.
    bool isVolUpHeld() const   { return m_btnVolUp.isHeld(); }
    bool isVolDownHeld() const { return m_btnVolDown.isHeld(); }

    // Edge-triggered variants of the same two buttons, for contexts that
    // want one discrete step per press rather than continuous drive (e.g.
    // menu navigation) -- as opposed to isVolUpHeld()/isVolDownHeld()
    // above, which normal volume control uses for continuous driving.
    bool volUpJustPressed() const   { return m_btnVolUp.justPressed; }
    bool volDownJustPressed() const { return m_btnVolDown.justPressed; }

    // True if the most recently decoded IR frame (the one that produced
    // whatever pollEvents() just returned, if it returned an IR-sourced
    // event) was a repeat frame rather than a fresh press. GAIN_UP/DOWN
    // are CONTINUOUS by policy (see IRCodes.hpp) so they fire on repeats
    // too, by design, for driving real volume -- but a context that wants
    // single-step-per-press behavior from IR as well (e.g. menu
    // navigation) can check this to ignore the repeat frames specifically.
    bool lastIrWasRepeat() const { return m_lastIrWasRepeat; }

private:
    InputEngine() = default;

    static constexpr uint32_t DEBOUNCE_MS         = 50;
    static constexpr uint32_t MENU_LONG_PRESS_MS  = 1000; // hold LEFT this long (alone, or with RIGHT also held) to open the menu

    uint8_t m_irPin = 255;

    DebouncedButton m_btnNext;
    DebouncedButton m_btnPrev;
    DebouncedButton m_btnVolUp;
    DebouncedButton m_btnVolDown;

    // True once a LEFT long-press has fired MENU_TOGGLE for the CURRENT
    // press, so the eventual release doesn't ALSO fire MODE_PREV for the
    // same physical press.
    bool m_leftConsumedByLongPress = false;

    bool m_lastIrWasRepeat = false;
};