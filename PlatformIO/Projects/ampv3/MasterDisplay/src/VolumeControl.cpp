#include "VolumeControl.hpp"
#include "PersistentSettings.hpp"
#include "DaughterBoardLink.hpp"

VolumeControl& VolumeControl::instance() {
    static VolumeControl inst;
    return inst;
}

void VolumeControl::init() {
    DaughterBoardLink::instance().init(DAUGHTER_UART_TX_PIN, DAUGHTER_UART_RX_PIN, DAUGHTER_UART_BAUD);
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
    m_ampMuted       = true;
    m_ampMutePending = true;
    retryPendingCommands();
}

void VolumeControl::ampMuteOff() {
    m_ampMuted       = false;
    m_ampMutePending = true;
    retryPendingCommands();
}

bool VolumeControl::ampMuteConfirmed() const {
    const DaughterBoardLink& link = DaughterBoardLink::instance();
    const DaughterStatus&    s    = link.status();
    return !m_ampMutePending && link.isStatusFresh() &&
           static_cast<int32_t>(s.receivedMs - m_ampMuteSentMs) > 0 && s.ampMute == m_ampMuted;
}

void VolumeControl::forget(DaughterBoardLink::Forget what) {
    m_forgetWhat    = what;
    m_forgetPending = true;
    retryPendingCommands();
}

void VolumeControl::calibrate(CalR mode, uint32_t ohms) {
    m_calMode    = mode;
    m_calOhms    = ohms;
    m_calPending = true;
    retryPendingCommands();
}

void VolumeControl::retryPendingCommands() {
    DaughterBoardLink& link = DaughterBoardLink::instance();
    // The power-up/post-calibration restore goes out FIRST, and the mute
    // waits until it has: VOL <n> must land while the LDR mute is still on,
    // so MUTE OFF brings the music in at the restored step, never at the
    // daughter's power-up step.
    if (m_volRestorePending) {
        if (!link.isBusy() && link.sendVolSet(m_savedStep)) {
            m_volRestorePending = false;
            m_volRestoreSentMs  = millis();
        }
        return; // everything else waits for the restore
    }
    if (m_mutePending && !link.isBusy()) {
        bool sent = m_muted ? link.sendMuteOn() : link.sendMuteOff();
        if (sent) m_mutePending = false;
    }
    if (m_ampMutePending && !link.isBusy()) {
        bool sent = m_ampMuted ? link.sendAmpMuteOn() : link.sendAmpMuteOff();
        if (sent) { m_ampMutePending = false; m_ampMuteSentMs = millis(); }
    }
    if (m_forgetPending && !link.isBusy()) {
        if (link.sendForget(m_forgetWhat)) m_forgetPending = false;
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
        Serial.printf("[Calibration] Finished: %s", calStateText(s.cal));
        if (s.cal == DaughterCalState::OK) Serial.printf(", LASTCAL=%s", s.lastCal);
        if (s.hasErr) Serial.printf(" (%s)", s.err);
        Serial.println();
    }
    m_wasCalibrating = calibratingNow;

    if (s.busy) return;

    // Adopt the daughter's amp mute unless our own request is still on its way.
    if (!m_ampMutePending && static_cast<int32_t>(s.receivedMs - m_ampMuteSentMs) > 0) m_ampMuted = s.ampMute;

    bool calOk = (s.cal == DaughterCalState::OK);
    if (!m_ready) {
        if (!calOk) return; // still muted by the daughter; m_muted keeps the user's intent
        // CAL=OK: bring the saved step back (silently, the LDR mute is still
        // on), then unmute -- unless the user muted meanwhile.
        if (m_savedStep >= 0 && m_savedStep >= s.volMin && m_savedStep <= s.volMax && m_savedStep != s.vol) {
            m_volRestorePending = true;
        }
        if (!m_muted) {
            m_mutePending      = true; // sends MUTE OFF (m_muted is false)
            m_lastMuteChangeMs = millis();
        }
        m_ready = true;
        Serial.printf("[Volume] Calibrated -- %s%s\n",
                      m_volRestorePending ? "restoring the saved step, " : "",
                      m_muted ? "staying muted (user mute)" : "unmuting");
        retryPendingCommands();
        return;
    }

    if (!calOk) {
        m_ready             = false;
        m_volRestorePending = false; // redone when CAL is OK again
        Serial.printf("[Volume] Calibration needed (%s) -- the daughter keeps the volume muted\n", calStateText(s.cal));
        return;
    }

    bool inGrace = (millis() - m_lastMuteChangeMs) < DAUGHTER_MUTE_ADOPT_GRACE_MS;
    if (!m_mutePending && !inGrace) {
        m_muted = s.mute; // the daughter is the source of truth
    }

    // Remember the step the volume settles on (saved a few seconds later by
    // main.cpp's deferred save). Statuses from before the restore command
    // still show the daughter's pre-restore step, so they don't count.
    if (!m_volRestorePending && static_cast<int32_t>(s.receivedMs - m_volRestoreSentMs) > 0 &&
        s.vol >= 0 && s.vol != m_savedStep) {
        m_savedStep = s.vol;
        PersistentSettings::instance().markDirty();
    }
}

void VolumeControl::update(bool wantVolUpHeld, bool wantVolDownHeld) {
    DaughterBoardLink& link = DaughterBoardLink::instance();
    link.update();
    syncFromDaughterStatus();
    retryPendingCommands();

    // Daughter: throttled discrete VOL UP/DOWN while held. Skipped while a
    // fresh status says the daughter is busy (it would just answer "ERR
    // busy" / the boot-hold status line) or already at the end of its range.
    uint32_t now = millis();
    if (!m_ready) return; // calibration needed: the daughter would refuse VOL (the overlay shows why)
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
