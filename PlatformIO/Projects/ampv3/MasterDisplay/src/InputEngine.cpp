#define DECODE_NEC // Limit decoding to NEC to save memory and flash
#include <IRremote.hpp>
#include "InputEngine.hpp"
#include "Console.hpp"
#include <cstdio>
#include <cstring>

InputEngine& InputEngine::instance() {
    static InputEngine inst;
    return inst;
}

void InputEngine::init(uint8_t btnNextPin, uint8_t btnPrevPin, uint8_t irPin, uint8_t btnVolUpPin, uint8_t btnVolDownPin) {
    m_btnNext.begin(btnNextPin, "RIGHT");
    m_btnPrev.begin(btnPrevPin, "LEFT");
    m_btnVolUp.begin(btnVolUpPin, "UP");
    m_btnVolDown.begin(btnVolDownPin, "DOWN");
    m_irPin = irPin;

    if (m_irPin != 255) {
        IrReceiver.begin(m_irPin, DISABLE_LED_FEEDBACK);
    }
}

void InputEngine::printRawStates() const {
    const DebouncedButton* buttons[] = { &m_btnVolUp, &m_btnNext, &m_btnVolDown, &m_btnPrev };
    char text[96];
    size_t n = 0;
    for (const DebouncedButton* b : buttons) {
        n += snprintf(text + n, sizeof(text) - n, " %s=%s", b->name,
                      (b->pin != 255 && digitalRead(b->pin) == LOW) ? "LOW" : "high");
    }
    Console::log("Btn", "raw:%s  (LOW = pressed)", text);
}

UIEvent InputEngine::pop() {
    if (m_queueCount == 0) return UIEvent::NONE;
    UIEvent e = m_queue[0];
    for (uint8_t i = 1; i < m_queueCount; ++i) m_queue[i - 1] = m_queue[i];
    --m_queueCount;
    // Every event leaves through here: note the volume ones for
    // volUpRequested() / volDownRequested().
    if (e == UIEvent::GAIN_UP)   m_lastGainUpMs   = millis();
    if (e == UIEvent::GAIN_DOWN) m_lastGainDownMs = millis();
    return e;
}

UIEvent InputEngine::pollEvents() {
    // Anything queued from the previous poll goes out first, one per call.
    if (m_queueCount > 0) return pop();

    uint32_t now = millis();

    // Update every physical button's debounce state once per poll --
    // isVolUpHeld()/isVolDownHeld() just read the resulting flag rather
    // than each re-debouncing independently on every call.
    m_btnNext.update(now, DEBOUNCE_MS, m_monitor);
    m_btnPrev.update(now, DEBOUNCE_MS, m_monitor);
    m_btnVolUp.update(now, DEBOUNCE_MS, m_monitor);
    m_btnVolDown.update(now, DEBOUNCE_MS, m_monitor);

    // LEFT and RIGHT are both dual-purpose: a short press acts on RELEASE
    // (MODE_PREV / MODE_NEXT) -- deferred so a short press can be told apart
    // from a long one -- and holding for LONG_PRESS_MS does something else
    // and consumes the press (nothing more happens on its release).
    //
    // LEFT held (alone, or together with RIGHT): open/close the menu. When
    // RIGHT is held too, it's consumed as well -- the two-button hold is a
    // menu gesture, never a power toggle.
    if (m_btnPrev.checkLongPress(now, LONG_PRESS_MS)) {
        m_leftConsumedByLongPress = true;
        if (m_btnNext.isHeld()) m_rightConsumedByLongPress = true;
        push(UIEvent::MENU_TOGGLE);
    }
    if (m_btnPrev.justReleased) {
        bool consumed = m_leftConsumedByLongPress;
        m_leftConsumedByLongPress = false;
        if (!consumed) push(UIEvent::MODE_PREV);
    }

    // RIGHT held alone: power on / standby (same as the remote's power key).
    // Not while LEFT is held, and not once the two-button menu hold has
    // consumed this press.
    if (!m_btnPrev.isHeld() && !m_rightConsumedByLongPress &&
        m_btnNext.checkLongPress(now, LONG_PRESS_MS)) {
        m_rightConsumedByLongPress = true;
        push(UIEvent::POWER_TOGGLE);
    }
    if (m_btnNext.justReleased) {
        bool consumed = m_rightConsumedByLongPress;
        m_rightConsumedByLongPress = false;
        if (!consumed) push(UIEvent::MODE_NEXT);
    }

    // DOWN gestures: double-click = mute, single tap = one step down (after
    // the double-click window), hold = repeat (isVolDownHeld()).
    if (m_downGestures) {
        if (m_btnVolDown.justReleased) {
            bool tap = (now - m_btnVolDown.pressedSinceMs) < DOWN_HOLD_MS;
            if (tap && m_downTapPending && now - m_downTapMs <= DOUBLE_CLICK_MS) {
                m_downTapPending = false;
                push(UIEvent::MUTE_TOGGLE);
            } else if (tap) {
                m_downTapPending = true;
                m_downTapMs      = now;
            } else {
                m_downTapPending = false; // that was a hold; it already stepped the volume
            }
        }
        if (m_downTapPending && !m_btnVolDown.isHeld() && now - m_downTapMs > DOUBLE_CLICK_MS) {
            m_downTapPending = false;
            push(UIEvent::GAIN_DOWN);
        }
    }

    // IR Signal Receiver
    if (m_irPin != 255 && IrReceiver.decode()) {
        uint16_t cmd = IrReceiver.decodedIRData.command;
        bool     isRepeat = (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT) != 0;
        IrReceiver.resume();
        m_lastIrWasRepeat = isRepeat;

        // With the button monitor on (USB console BUTTONS): every decoded
        // code, mapped or not -- for reading a new remote's codes. Quiet
        // otherwise.
        if (m_monitor) {
            const char* what = "unmapped";
            for (const auto& mapping : kIrMappings) {
                if (mapping.code == cmd) { what = uiEventName(mapping.event); break; }
            }
            Console::log("IR", " cmd=0x%02X%s -> %s", cmd, isRepeat ? " (repeat)" : "", what);
        }

        for (const auto& mapping : kIrMappings) {
            if (mapping.code != cmd) continue;
            // Repeat frames of one-shot actions are ignored.
            if (!(isRepeat && mapping.policy == IrRepeatPolicy::ONE_SHOT)) push(mapping.event);
            break;
        }
    }

    return pop();
}
