#include "VolumeControl.hpp"
#include "Console.hpp"
#include <cstdio>
#include <cstring>
#include <cstring>
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

void VolumeControl::setModelSource(uint32_t ohms) {
    m_srcOhms    = ohms;
    m_srcPending = true;
    retryPendingCommands();
}

void VolumeControl::setBalance(int8_t n) {
    if (n < -16) n = -16;
    if (n > 16)  n = 16;
    m_balance    = n;
    m_balPending = true;
    retryPendingCommands();
}

void VolumeControl::setAutoPref(DaughterBoardLink::AutoPref pref) {
    m_autoPref        = pref;
    m_autoPrefPending = true;
    retryPendingCommands();
}

void VolumeControl::setModelLoad(uint32_t ohms) {
    m_loadOhms    = ohms;
    m_loadPending = true;
    retryPendingCommands();
}

int8_t VolumeControl::compensationState() const {
    const DaughterEnv& e = DaughterBoardLink::instance().env();
    if (m_compRequested && (m_compPending || !e.valid || static_cast<int32_t>(e.receivedMs - m_compSentMs) <= 0)) {
        return m_compOn ? 1 : 0;
    }
    if (!e.valid || !strcmp(e.comp, "--")) return -1;
    return strcmp(e.comp, "OFF") ? 1 : 0; // ON, PART and LEARNING are all "enabled"
}

void VolumeControl::setCompensation(bool on) {
    m_compOn        = on;
    m_compPending   = true;
    m_compRequested = true;
    retryPendingCommands();
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
    // Balance first: after a daughter power-up it must be in place before
    // the MUTE OFF that follows (one command in flight, so the order holds).
    // Not while the daughter is busy -- it would answer ERR busy.
    if (m_balPending && !link.isBusy() && link.isStatusFresh() && !link.status().busy) {
        if (link.sendBal(m_balance)) m_balPending = false;
    }
    if (m_mutePending && !link.isBusy()) {
        bool sent = m_muted ? link.sendMuteOn() : link.sendMuteOff();
        if (sent) m_mutePending = false;
    }
    if (m_ampMutePending && !link.isBusy()) {
        bool sent = m_ampMuted ? link.sendAmpMuteOn() : link.sendAmpMuteOff();
        if (sent) { m_ampMutePending = false; m_ampMuteSentMs = millis(); }
    }
    if (m_srcPending && !link.isBusy()) {
        if (link.sendSetSrc(m_srcOhms)) m_srcPending = false;
    }
    if (m_loadPending && !link.isBusy()) {
        if (link.sendSetLoad(m_loadOhms)) m_loadPending = false;
    }
    if (m_autoPrefPending && !link.isBusy()) {
        if (link.sendSetAuto(m_autoPref)) m_autoPrefPending = false;
    }
    if (m_compPending && !link.isBusy()) {
        if (link.sendComp(m_compOn)) { m_compPending = false; m_compSentMs = millis(); }
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
        Console::log("Calibration", "Daughter reports CAL=PROCESSING");
    } else if (!calibratingNow && m_wasCalibrating && !s.busy) {
        char extra[96] = "";
        if (s.cal == DaughterCalState::OK) snprintf(extra, sizeof(extra), ", LASTCAL=%s", s.lastCal);
        if (s.hasErr) snprintf(extra + strlen(extra), sizeof(extra) - strlen(extra), " (%s)", s.err);
        Console::log("Calibration", "Finished: %s%s", calStateText(s.cal), extra);
    }
    m_wasCalibrating = calibratingNow;

    // A daughter restart (its uptime went backwards: a brownout, its reset
    // button, a firmware flash) brings it up muted, balance 0: run the
    // power-up steps again once it reports CAL=OK -- even though its busy
    // start-up statuses never show CAL leaving OK.
    if (m_haveUp && s.upSeconds + 2 < m_lastUp) {
        m_ready = false;
        Console::log("Volume", "Daughter restarted -- will restore balance and unmute once it's calibrated");
    }
    m_lastUp = s.upSeconds;
    m_haveUp = true;

    if (s.busy) return;

    // Adopt the daughter's amp mute unless our own request is still on its way.
    if (!m_ampMutePending && static_cast<int32_t>(s.receivedMs - m_ampMuteSentMs) > 0) m_ampMuted = s.ampMute;

    bool calOk = (s.cal == DaughterCalState::OK);
    if (!m_ready) {
        if (!calOk) return; // still muted by the daughter; m_muted keeps the user's intent
        // CAL=OK: unmute -- unless the user muted meanwhile. The daughter
        // keeps its own step (0 on a first power-up). A non-centre balance
        // goes over first (the daughter starts at 0 after a reset).
        if (m_balance != 0) m_balPending = true;
        if (!m_muted) {
            m_mutePending      = true; // sends MUTE OFF (m_muted is false)
            m_lastMuteChangeMs = millis();
        }
        m_ready = true;
        Console::log("Volume", "Calibrated -- %s (step %d)", m_muted ? "staying muted (user mute)" : "unmuting", s.vol);
        retryPendingCommands();
        return;
    }

    if (!calOk) {
        m_ready = false;
        Console::log("Volume", "Calibration needed (%s) -- the daughter keeps the volume muted", calStateText(s.cal));
        return;
    }

    bool inGrace = (millis() - m_lastMuteChangeMs) < DAUGHTER_MUTE_ADOPT_GRACE_MS;
    if (!m_mutePending && !inGrace) {
        m_muted = s.mute; // the daughter is the source of truth
    }
}

void VolumeControl::update(bool wantVolUpHeld, bool wantVolDownHeld) {
    DaughterBoardLink& link = DaughterBoardLink::instance();
    // Queued user commands go ahead of background polls -- but never while
    // the status is stale: some commands (BAL) wait for a fresh STATUS, and
    // holding the STATUS poll for them would deadlock the link (NO LINK).
    bool anyPending = m_mutePending || m_calPending || m_ampMutePending || m_forgetPending || m_compPending ||
                      m_srcPending || m_loadPending || m_autoPrefPending || m_balPending;
    link.holdPolls(anyPending && link.isStatusFresh());
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
