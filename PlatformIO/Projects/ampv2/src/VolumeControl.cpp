#include "VolumeControl.hpp"
#include "VolumeMotor.hpp"
#include "DaughterBoardLink.hpp"

VolumeControl& VolumeControl::instance() {
    static VolumeControl inst;
    return inst;
}

void VolumeControl::init() {
    // DaughterBoardLink must be ready before applyMuteState() below can
    // safely send anything over it -- initializing mute state first would
    // try to transmit on a UART that hasn't been begin()'d yet.
    DaughterBoardLink::instance().init(DAUGHTER_UART_TX_PIN, DAUGHTER_UART_RX_PIN, DAUGHTER_UART_BAUD);

    VolumeMotor::instance().init(DRV8833_IN1_PIN, DRV8833_IN2_PIN);

    pinMode(MUTE_PIN, OUTPUT);
    m_bootMs = millis();
    m_bootHoldActive = true;
    applyMuteState(true); // force muted; muteOn()/muteOff() refuse to change this until the hold elapses, regardless of caller
}

void VolumeControl::applyMuteState(bool muted) {
    digitalWrite(MUTE_PIN, (muted == MUTE_ACTIVE_HIGH) ? HIGH : LOW);
    if (muted) {
        DaughterBoardLink::instance().sendMuteOn();
    } else {
        DaughterBoardLink::instance().sendMuteOff();
    }
    m_muted = muted;
}

void VolumeControl::muteOn() {
    if (m_bootHoldActive) return; // hard safety floor -- refused outright, not deferred
    applyMuteState(true);
}

void VolumeControl::muteOff() {
    if (m_bootHoldActive) return; // hard safety floor -- refused outright, not deferred
    applyMuteState(false);
}

void VolumeControl::toggleMute() {
    if (m_muted) {
        muteOff();
    } else {
        muteOn();
    }
}

void VolumeControl::ampMuteOn() {
    DaughterBoardLink::instance().sendAmpMuteOn(); // no-op if nothing's listening; see getLastStatus() for the ERR case (e.g. daughter's own boot hold)
    m_ampMuted = true;
}

void VolumeControl::ampMuteOff() {
    DaughterBoardLink::instance().sendAmpMuteOff();
    m_ampMuted = false;
}

void VolumeControl::calibrate() {
    DaughterBoardLink::instance().sendCalibrate(); // no-op if nothing's listening
    // No defined calibration procedure exists yet for the motor/mute path.
}

void VolumeControl::update(bool wantVolUpHeld, bool wantVolDownHeld) {
    // Mandatory boot mute hold: elapses purely by time, never by any
    // explicit call (muteOn/muteOff/toggleMute all refuse to do anything
    // while this is active -- see MUTE_BOOT_HOLD_MS in Config.hpp).
    if (m_bootHoldActive && (millis() - m_bootMs >= MUTE_BOOT_HOLD_MS)) {
        m_bootHoldActive = false; // clear first, so muteOff() below is no longer refused
        muteOff();
    }

    // Motor: continuous drive while held -- no-op if DRV8833_IN1/IN2
    // aren't physically connected to anything.
    VolumeMotor::instance().update(wantVolUpHeld, wantVolDownHeld);

    // Daughter board: always serviced (processes replies/timeouts), and
    // sends throttled discrete VOL UP/DOWN commands while held -- no-op if
    // nothing's connected (the command just goes nowhere, no reply ever
    // arrives, no harm done).
    DaughterBoardLink& link = DaughterBoardLink::instance();
    link.update();

    // Logs calibration's real outcome exactly once, right when
    // isCalibrating() transitions back to false -- protocol v2 made this a
    // genuine async DONE/FAIL (or a safety-timeout fallback), worth
    // surfacing now that there's an actual result to report instead of a
    // blind fixed-wait guess.
    bool isCalibratingNow = link.isCalibrating();
    if (m_wasCalibrating && !isCalibratingNow) {
        switch (link.getLastStatus()) {
            case DaughterCmdStatus::OK:
                Serial.println("[Calibration] Completed successfully.");
                break;
            case DaughterCmdStatus::ERROR:
                Serial.print("[Calibration] Failed: ");
                Serial.println(link.getLastError());
                break;
            case DaughterCmdStatus::TIMEOUT:
                Serial.println("[Calibration] Safety timeout -- no DONE/FAIL reply ever arrived; check the link/daughter board.");
                break;
            default:
                break;
        }
    }
    m_wasCalibrating = isCalibratingNow;

    uint32_t now = millis();
    bool readyToSend = !link.isBusy() && (now - m_lastVolCommandMs >= DAUGHTER_VOL_REPEAT_MS);

    if (wantVolUpHeld && readyToSend) {
        link.sendVolUp();
        m_lastVolCommandMs = now;
    } else if (wantVolDownHeld && readyToSend) {
        link.sendVolDown();
        m_lastVolCommandMs = now;
    }
}