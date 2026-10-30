#pragma once
#include "Config.hpp"
#include "AttenuatorBoardLink.hpp"

// Mute/volume control, all via the attenuator board.
//  - Volume: discrete VOL UP/DOWN steps while a volume key is held.
//  - Mute is the attenuator board's alone (its LDR mute). There is no local
//    mute pin any more; isMuted() mirrors what the attenuator reports in its
//    STATUS line, updated optimistically the moment the user toggles it.
class VolumeControl {
public:
    static VolumeControl& instance();

    void init();

    // Call every loop iteration -- services the attenuator link, adopts fresh
    // STATUS data, retries any command the busy link couldn't take yet, and
    // while a volume key is held, steps the volume.
    void update(bool wantVolUpHeld, bool wantVolDownHeld);

    void muteOn();
    void muteOff();
    void toggleMute();
    bool isMuted() const { return m_muted; }

    // The attenuator's separate amp mute (AMP_MUTE), used by the power
    // sequence. Not bundled into muteOn()/muteOff() -- the attenuator treats
    // them as unrelated. Retried until the link takes it, like mute.
    void ampMuteOn();
    void ampMuteOff();
    bool isAmpMuted() const { return m_ampMuted; }
    // True once the attenuator has confirmed the requested amp-mute state in a
    // STATUS that arrived after the command went out.
    bool ampMuteConfirmed() const;

    // rev 9 FORGET (CAL / COMP / ALL), retried until the link takes it.
    void forget(AttenuatorBoardLink::Forget what);

    // Thermal compensation on/off (Diagnostics), retried until
    // the link takes it. The attenuator doesn't store it: it boots ON.
    void setCompensation(bool on);
    // rev 12 audio model (Calibration -> Model): SET SRC / SET
    // LOAD, retried until the link takes them. A change makes the attenuator
    // CAL=STALE (volume muted) until a calibration re-solves.
    void setModelSource(uint32_t ohms);
    void setModelLoad(uint32_t ohms);
    // rev 18 balance (Settings -> Balance): -16..+16 in 0.25 dB, positive
    // lowers the left channel, negative the right. Kept here (and saved with
    // the settings -- the attenuator doesn't store it): sent whenever it
    // changes (the latest value wins if it changes faster than the link), and
    // again after every attenuator power-up (where it's 0) -- before the
    // unmute. Not sent while the attenuator is busy.
    void   setBalance(int8_t n);
    // At boot, from the settings: only remembers the value -- it goes to the
    // attenuator with its power-up steps (once CAL=OK, before the unmute).
    void   restoreBalance(int8_t n) { m_balance = (n < -16) ? -16 : (n > 16 ? 16 : n); }
    int8_t balance() const { return m_balance; }

    // rev 16 AUTO preference (Calibration -> Auto Depth), retried likewise.
    void setAutoPref(AttenuatorBoardLink::AutoPref pref);

    // For the menu: 1 on, 0 off, -1 unknown. Taken from ENV's COMP; right
    // after a change, the requested state until ENV has caught up.
    int8_t compensationState() const;
    // What the attenuator's compensation is doing, from its ENV: -1 unknown,
    // 0 off (OFF, or NONTC: the block sensor is faulty), 1 not fully active
    // (LEARNING, or PART: one side), 2 ON. A just-sent switch shows as 1/0
    // until the attenuator confirms it.
    int8_t compensationLevel() const;
    // ENV's COMP word, or "--".
    const char* compensationText() const;

    // Revision 10: the attenuator powers up with its LDR mute ON and refuses
    // VOL / MUTE OFF until CAL=OK; a finished calibration doesn't unmute
    // either. So whenever CAL becomes OK (at power-up, or after any
    // calibration) the master sends MUTE OFF -- unless the user had muted.
    // ready() = that has happened and the volume is usable; while not ready,
    // the attenuator's forced mute is NOT taken as the user's mute.
    //
    // The volume itself is never stored on the master: the master is
    // always powered, the attenuator starts at step 0 on a first power-up, and
    // nothing changes the volume in standby -- so the attenuator's STATUS is
    // always the truth.
    bool ready() const { return m_ready; }

    // Asks the attenuator to calibrate. KEEP_R sends plain "CAL" (the attenuator
    // decides how much work it needs, Rtotal unchanged); AUTO_R "CAL R=AUTO";
    // SET_R "CAL R=<ohms>" (must lie within the attenuator's RMIN..RMAX).
    // Retried until the link takes it; progress/outcome come from STATUS and
    // PROGRESS polling and are logged by update().
    // KEEP_R "CAL", AUTO_R "CAL R=AUTO", SET_R "CAL R=<ohms>", FULL "CAL FULL"
    // (rev 19), VERIFY "CAL VERIFY" (rev 20).
    enum class CalR : uint8_t { KEEP_R, AUTO_R, SET_R, FULL, VERIFY };
    void calibrate(CalR mode = CalR::KEEP_R, uint32_t ohms = 0);
    bool isCalibrationPending() const { return m_calPending; } // requested, not yet sent

private:
    VolumeControl() = default;

    void requestMute(bool muted);
    void syncFromAttenuatorStatus();
    void retryPendingCommands();

    bool     m_muted             = false; // attenuator's MUTE (until the first STATUS arrives: assumed unmuted)
    bool     m_ampMuted          = false;
    uint32_t m_lastMuteChangeMs  = 0;     // last user mute change -- see ATTENUATOR_MUTE_ADOPT_GRACE_MS
    bool     m_mutePending       = false; // a MUTE ON/OFF still waiting for the link to be free
    bool     m_calPending        = false; // a CAL still waiting for the link to be free
    bool     m_ampMutePending    = false; // an AMP_MUTE ON/OFF still waiting for the link
    uint32_t m_ampMuteSentMs     = 0;     // when it went out (for ampMuteConfirmed())
    bool     m_forgetPending     = false;
    bool     m_compPending       = false;
    bool     m_srcPending        = false;
    bool     m_loadPending       = false;
    uint32_t m_srcOhms           = 0;
    uint32_t m_loadOhms          = 0;
    bool     m_autoPrefPending   = false;
    int8_t   m_balance           = 0;
    uint32_t m_lastUp            = 0;     // the attenuator's uptime in its last STATUS (restart detection)
    bool     m_haveUp            = false;
    bool     m_balPending        = false;
    AttenuatorBoardLink::AutoPref m_autoPref = AttenuatorBoardLink::AutoPref::BALANCED;
    bool     m_compOn            = true;
    bool     m_compRequested     = false; // a COMP command was ever sent
    uint32_t m_compSentMs        = 0;
    AttenuatorBoardLink::Forget m_forgetWhat = AttenuatorBoardLink::Forget::CAL;
    CalR     m_calMode           = CalR::KEEP_R;
    uint32_t m_calOhms           = 0;
    uint32_t m_lastVolCommandMs  = 0;     // throttles VOL UP/DOWN repeats
    uint32_t m_lastSeenStatusCount = 0;
    bool     m_wasCalibrating    = false; // edge-detects the attenuator's calibration start/end for logging
    bool     m_ready             = false; // CAL=OK seen and the unmute done (see ready())
};
