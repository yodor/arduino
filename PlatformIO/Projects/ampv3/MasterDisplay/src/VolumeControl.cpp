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
    // force muted locally at boot. UART send deliberately skipped here --
    // see applyMuteState()'s comment in the header. No boot-hold timer on
    // this side at all -- the daughter board enforces its own independent
    // boot-mute hold and refuses every command (replying ERR) until it's
    // elapsed, so duplicating that gate here would be redundant, and user
    // mute/unmute requests are never artificially blocked on this side.
    applyMuteState(true, false);
}

void VolumeControl::applyMuteState(bool muted, bool sendOverUart) {
    digitalWrite(MUTE_PIN, (muted == MUTE_ACTIVE_HIGH) ? HIGH : LOW);
    if (sendOverUart) {
        if (muted) {
            DaughterBoardLink::instance().sendMuteOn();
        } else {
            DaughterBoardLink::instance().sendMuteOff();
        }
    }
    m_muted = muted;
}

void VolumeControl::muteOn() {
    applyMuteState(true);
}

void VolumeControl::muteOff() {
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