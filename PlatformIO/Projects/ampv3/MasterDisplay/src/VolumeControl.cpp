#include "VolumeControl.hpp"
#include "VolumeMotor.hpp"
#include "DaughterBoardLink.hpp"

VolumeControl& VolumeControl::instance() {
    static VolumeControl inst;
    return inst;
}

void VolumeControl::init() {
    DaughterBoardLink::instance().init(DAUGHTER_UART_TX_PIN, DAUGHTER_UART_RX_PIN, DAUGHTER_UART_BAUD);
    VolumeMotor::instance().init(DRV8833_IN1_PIN, DRV8833_IN2_PIN);
    // No mute action at boot: the daughter starts muted under its own boot
    // hold and releases on its own; its real MUTE state is adopted from the
    // first IDLE STATUS (syncFromDaughterStatus()).
}

// Optimistic: the local mirror flips immediately (so the overlay's MUTED tag
// reacts at once), the command goes out as soon as the link is free, and
// the daughter's STATUS confirms (or corrects) it afterwards.
void VolumeControl::requestMute(bool muted) {
    m_muted            = muted;
    m_lastMuteChangeMs = millis();
    m_mutePending      = true;
    retryPendingCommands();
}

void VolumeControl::muteOn()  { requestMute(true); }
void VolumeControl::muteOff() { requestMute(false); }

void VolumeControl::toggleMute() {
    requestMute(!m_muted);
}

void VolumeControl::ampMuteOn() {
    DaughterBoardLink::instance().sendAmpMuteOn();
    m_ampMuted = true;
}

void VolumeControl::ampMuteOff() {
    DaughterBoardLink::instance().sendAmpMuteOff();
    m_ampMuted = false;
}

void VolumeControl::calibrate(CalR mode, uint32_t ohms) {
    m_calMode    = mode;
    m_calOhms    = ohms;
    m_calPending = true;
    retryPendingCommands();
}

void VolumeControl::retryPendingCommands() {
    DaughterBoardLink& link = DaughterBoardLink::instance();
    if (m_mutePending && !link.isBusy()) {
        bool sent = m_muted ? link.sendMuteOn() : link.sendMuteOff();
        if (sent) m_mutePending = false;
    }
    if (m_calPending && !link.isBusy()) {
        bool sent = (m_calMode == CalR::AUTO_R) ? link.sendCalibrateAuto()
                  : (m_calMode == CalR::SET_R)  ? link.sendCalibrateR(m_calOhms)
                                                : link.sendCalibrate();
        if (sent) m_calPending = false;
    }
}

// Adopts the daughter's reported state once per new STATUS line -- but only
// when it reports IDLE (during a calibration the fields are transient, and
// during the boot hold the daughter is still settling), and never while a
// local mute change is still on its way or within the grace window after it.
void VolumeControl::syncFromDaughterStatus() {
    DaughterBoardLink& link = DaughterBoardLink::instance();
    if (link.statusCount() == m_lastSeenStatusCount) return;
    m_lastSeenStatusCount = link.statusCount();

    const DaughterStatus& s = link.status();

    // Calibration start/end logging (the protocol has no CAL DONE/FAIL --
    // completion is STATUS going back to IDLE).
    bool calibratingNow = s.calibrating();
    if (calibratingNow && !m_wasCalibrating) {
        Serial.println("[Calibration] Daughter reports CAL=PROCESSING");
    } else if (!calibratingNow && m_wasCalibrating && !s.busy) {
        if (s.cal == DaughterCalState::OK) {
            Serial.print("[Calibration] Finished OK, LASTCAL=");
            Serial.println(s.lastCal);
        } else {
            Serial.print("[Calibration] Finished with CAL=");
            Serial.print(s.cal == DaughterCalState::NONE ? "NONE" : "ERR");
            if (s.hasErr) { Serial.print(": "); Serial.print(s.err); }
            Serial.println();
        }
    }
    m_wasCalibrating = calibratingNow;

    if (s.busy) return;

    m_ampMuted = s.ampMute;

    bool inGrace = (millis() - m_lastMuteChangeMs) < DAUGHTER_MUTE_ADOPT_GRACE_MS;
    if (!m_mutePending && !inGrace) {
        m_muted = s.mute; // the daughter is the source of truth
    }
}

void VolumeControl::update(bool wantVolUpHeld, bool wantVolDownHeld) {
    // Motor: continuous drive while held -- no-op if not connected.
    VolumeMotor::instance().update(wantVolUpHeld, wantVolDownHeld);

    DaughterBoardLink& link = DaughterBoardLink::instance();
    link.update();
    syncFromDaughterStatus();
    retryPendingCommands();

    // Daughter: throttled discrete VOL UP/DOWN while held. Skipped while a
    // fresh status says the daughter is busy (it would just answer "ERR
    // busy" / the boot-hold status line) or already at the end of its range.
    uint32_t now = millis();
    if (link.isBusy() || (now - m_lastVolCommandMs < DAUGHTER_VOL_REPEAT_MS)) return;

    const DaughterStatus& s = link.status();
    bool fresh = link.isStatusFresh();
    if (fresh && s.busy) return;

    if (wantVolUpHeld) {
        if (!(fresh && s.volMax >= 0 && s.vol >= s.volMax)) link.sendVolUp();
        m_lastVolCommandMs = now;
    } else if (wantVolDownHeld) {
        if (!(fresh && s.volMin >= 0 && s.vol >= 0 && s.vol <= s.volMin)) link.sendVolDown();
        m_lastVolCommandMs = now;
    }
}
