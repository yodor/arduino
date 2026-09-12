#define DECODE_NEC // Limit decoding to NEC to save memory and flash
#include <IRremote.hpp>
#include "InputEngine.hpp"

InputEngine& InputEngine::instance() {
    static InputEngine inst;
    return inst;
}

void InputEngine::init(uint8_t btnNextPin, uint8_t btnPrevPin, uint8_t irPin, uint8_t btnVolUpPin, uint8_t btnVolDownPin) {
    m_btnNext.begin(btnNextPin);
    m_btnPrev.begin(btnPrevPin);
    m_btnVolUp.begin(btnVolUpPin);
    m_btnVolDown.begin(btnVolDownPin);
    m_irPin = irPin;

    if (m_irPin != 255) {
        IrReceiver.begin(m_irPin, DISABLE_LED_FEEDBACK);
    }
}

UIEvent InputEngine::pollEvents() {
    uint32_t now = millis();

    // Update every physical button's debounce state once per poll --
    // isVolUpHeld()/isVolDownHeld() just read the resulting flag rather
    // than each re-debouncing independently on every call.
    m_btnNext.update(now, DEBOUNCE_MS);
    m_btnPrev.update(now, DEBOUNCE_MS);
    m_btnVolUp.update(now, DEBOUNCE_MS);
    m_btnVolDown.update(now, DEBOUNCE_MS);

    // RIGHT: simple edge-triggered MODE_NEXT, unchanged. Adding another
    // plain edge-triggered button later is just one entry here plus its
    // DebouncedButton member and update() call above.
    struct ButtonMapping { DebouncedButton* button; UIEvent event; };
    const ButtonMapping edgeButtons[] = {
        { &m_btnNext, UIEvent::MODE_NEXT },
    };
    for (const auto& mapping : edgeButtons) {
        if (mapping.button->justPressed) return mapping.event;
    }

    // LEFT: dual-purpose. A long hold -- of LEFT alone, or LEFT with RIGHT
    // also held at the same time, since either way LEFT itself has been
    // continuously held for the threshold -- opens the menu and consumes
    // this press. Otherwise a normal short press+release fires MODE_PREV,
    // deferred to release (rather than press) so we can tell short from
    // long presses apart before committing to either action.
    if (m_btnPrev.checkLongPress(now, MENU_LONG_PRESS_MS)) {
        m_leftConsumedByLongPress = true;
        return UIEvent::MENU_TOGGLE;
    }
    if (m_btnPrev.justReleased) {
        bool consumed = m_leftConsumedByLongPress;
        m_leftConsumedByLongPress = false;
        if (!consumed) {
            return UIEvent::MODE_PREV;
        }
    }

    // IR Signal Receiver
    if (m_irPin != 255 && IrReceiver.decode()) {
        uint16_t cmd = IrReceiver.decodedIRData.command;
        bool     isRepeat = (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT) != 0;
        IrReceiver.resume();
        m_lastIrWasRepeat = isRepeat;

        // Prints every decoded code regardless of whether it matches a
        // known mapping below -- useful for reading raw codes off a new
        // remote to map later, without needing to guess hex values blind.
        Serial.printf("[IR] cmd=0x%02X%s\n", cmd, isRepeat ? " (repeat)" : "");

        for (const auto& mapping : kIrMappings) {
            if (mapping.code != cmd) continue;
            if (isRepeat && mapping.policy == IrRepeatPolicy::ONE_SHOT) {
                return UIEvent::NONE; // ignore repeat frames for one-shot actions
            }
            return mapping.event;
        }
    }

    return UIEvent::NONE;
}