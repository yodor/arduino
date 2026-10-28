#pragma once
#include "Config.hpp"
#include "DaughterBoardLink.hpp"

// Mute/volume control, all via the daughter board.
//  - Volume: discrete VOL UP/DOWN steps while a volume key is held.
//  - Mute is the daughter board's alone (its LDR mute). There is no local
//    mute pin any more; isMuted() mirrors what the daughter reports in its
//    STATUS line, updated optimistically the moment the user toggles it.
class VolumeControl {
public:
    static VolumeControl& instance();

    void init();

    // Call every loop iteration -- services the daughter link, adopts fresh
    // STATUS data, retries any command the busy link couldn't take yet, and
    // while a volume key is held, steps the volume.
    void update(bool wantVolUpHeld, bool wantVolDownHeld);

    void muteOn();
    void muteOff();
    void toggleMute();
    bool isMuted() const { return m_muted; }

    // The daughter's separate amp mute (AMP_MUTE), used by the power
    // sequence. Not bundled into muteOn()/muteOff() -- the daughter treats
    // them as unrelated. Retried until the link takes it, like mute.
    void ampMuteOn();
    void ampMuteOff();
    bool isAmpMuted() const { return m_ampMuted; }
    // True once the daughter has confirmed the requested amp-mute state in a
    // STATUS that arrived after the command went out.
    bool ampMuteConfirmed() const;

    // rev 9 FORGET (CAL / COMP / ALL), retried until the link takes it.
    void forget(DaughterBoardLink::Forget what);

    // Thermal compensation on/off (Diagnostics), retried until
    // the link takes it. The daughter doesn't store it: it boots ON.
    void setCompensation(bool on);
    // rev 12 audio model (Calibration -> Model): SET SRC / SET
    // LOAD, retried until the link takes them. A change makes the daughter
    // CAL=STALE (volume muted) until a calibration re-solves.
    void setModelSource(uint32_t ohms);
    void setModelLoad(uint32_t ohms);
    // rev 18 balance (Settings -> Balance): -16..+16 in 0.25 dB, positive
    // lowers the left channel, negative the right. Kept here (and saved with
    // the settings -- the daughter doesn't store it): sent whenever it
    // changes (the latest value wins if it changes faster than the link), and
    // again after every daughter power-up (where it's 0) -- before the
    // unmute. Not sent while the daughter is busy.
    void   setBalance(int8_t n);
    // At boot, from the settings: only remembers the value -- it goes to the
    // daughter with its power-up steps (once CAL=OK, before the unmute).
    void   restoreBalance(int8_t n) { m_balance = (n < -16) ? -16 : (n > 16 ? 16 : n); }
    int8_t balance() const { return m_balance; }

    // rev 16 AUTO preference (Calibration -> Auto Depth), retried likewise.
    void setAutoPref(DaughterBoardLink::AutoPref pref);

    // For the menu: 1 on, 0 off, -1 unknown. Taken from ENV's COMP; right
    // after a change, the requested state until ENV has caught up.
    int8_t compensationState() const;

    // Revision 10: the daughter powers up with its LDR mute ON and refuses
    // VOL / MUTE OFF until CAL=OK; a finished calibration doesn't unmute
    // either. So whenever CAL becomes OK (at power-up, or after any
    // calibration) the master sends MUTE OFF -- unless the user had muted.
    // ready() = that has happened and the volume is usable; while not ready,
    // the daughter's forced mute is NOT taken as the user's mute.
    //
    // The volume itself is never stored on the master: the master is
    // always powered, the daughter starts at step 0 on a first power-up, and
    // nothing changes the volume in standby -- so the daughter's STATUS is
    // always the truth.
    bool ready() const { return m_ready; }

    // Asks the daughter to calibrate. KEEP_R sends plain "CAL" (the daughter
    // decides how much work it needs, Rtotal unchanged); AUTO_R "CAL R=AUTO";
    // SET_R "CAL R=<ohms>" (must lie within the daughter's RMIN..RMAX).
    // Retried until the link takes it; progress/outcome come from STATUS and
    // PROGRESS polling and are logged by update().
    enum class CalR : uint8_t { KEEP_R, AUTO_R, SET_R };
    void calibrate(CalR mode = CalR::KEEP_R, uint32_t ohms = 0);
    bool isCalibrationPending() const { return m_calPending; } // requested, not yet sent

private:
    VolumeControl() = default;

    void requestMute(bool muted);
    void syncFromDaughterStatus();
    void retryPendingCommands();

    bool     m_muted             = false; // daughter's MUTE (until the first STATUS arrives: assumed unmuted)
    bool     m_ampMuted          = false;
    uint32_t m_lastMuteChangeMs  = 0;     // last user mute change -- see DAUGHTER_MUTE_ADOPT_GRACE_MS
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
    uint32_t m_lastUp            = 0;     // the daughter's uptime in its last STATUS (restart detection)
    bool     m_haveUp            = false;
    bool     m_balPending        = false;
    DaughterBoardLink::AutoPref m_autoPref = DaughterBoardLink::AutoPref::BALANCED;
    bool     m_compOn            = true;
    bool     m_compRequested     = false; // a COMP command was ever sent
    uint32_t m_compSentMs        = 0;
    DaughterBoardLink::Forget m_forgetWhat = DaughterBoardLink::Forget::CAL;
    CalR     m_calMode           = CalR::KEEP_R;
    uint32_t m_calOhms           = 0;
    uint32_t m_lastVolCommandMs  = 0;     // throttles VOL UP/DOWN repeats
    uint32_t m_lastSeenStatusCount = 0;
    bool     m_wasCalibrating    = false; // edge-detects the daughter's calibration start/end for logging
    bool     m_ready             = false; // CAL=OK seen and the unmute done (see ready())
};
