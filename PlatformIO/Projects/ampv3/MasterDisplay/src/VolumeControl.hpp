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

    // Revision 10: the daughter powers up with its LDR mute ON and refuses
    // VOL / MUTE OFF until CAL=OK; a finished calibration doesn't unmute
    // either. So whenever CAL becomes OK (at power-up, or after any
    // calibration) the master restores the saved volume step (VOL <n>,
    // silent while muted) and then sends MUTE OFF -- unless the user had
    // muted. ready() = that has happened and the volume is usable; while
    // not ready, the daughter's forced mute is NOT taken as the user's mute.
    bool ready() const { return m_ready; }

    // The step restored at power-up -- the last step the volume settled on.
    // Saved with the settings (PersistentSettings), a few seconds after it
    // stops changing; -1 = none saved yet.
    int16_t savedVolumeStep() const        { return m_savedStep; }
    void    setSavedVolumeStep(int16_t s)  { m_savedStep = s; }

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
    DaughterBoardLink::Forget m_forgetWhat = DaughterBoardLink::Forget::CAL;
    CalR     m_calMode           = CalR::KEEP_R;
    uint32_t m_calOhms           = 0;
    uint32_t m_lastVolCommandMs  = 0;     // throttles VOL UP/DOWN repeats
    uint32_t m_lastSeenStatusCount = 0;
    bool     m_wasCalibrating    = false; // edge-detects the daughter's calibration start/end for logging
    bool     m_ready             = false; // CAL=OK seen and the restore/unmute done (see ready())
    int16_t  m_savedStep         = -1;
    bool     m_volRestorePending = false; // VOL <m_savedStep> waiting for the link
    uint32_t m_volRestoreSentMs  = 0;     // statuses older than this still show the pre-restore step
};
