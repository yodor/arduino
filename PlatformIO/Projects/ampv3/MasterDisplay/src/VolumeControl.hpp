#pragma once
#include "Config.hpp"

// Unified mute/volume control.
//  - Volume is driven through BOTH the DRV8833 motor (continuous while held)
//    and the daughter board (discrete VOL UP/DOWN steps), unconditionally --
//    whichever is fitted takes effect.
//  - Mute is the daughter board's alone (its LDR mute). There is no local
//    mute pin any more; isMuted() mirrors what the daughter reports in its
//    STATUS line, updated optimistically the moment the user toggles it.
class VolumeControl {
public:
    static VolumeControl& instance();

    void init();

    // Call every loop iteration -- services the motor and the daughter link,
    // adopts fresh STATUS data, retries any command the busy link couldn't
    // take yet, and while held, drives both volume paths.
    void update(bool wantVolUpHeld, bool wantVolDownHeld);

    void muteOn();
    void muteOff();
    void toggleMute();
    bool isMuted() const { return m_muted; }

    // The daughter's separate amp mute (AMP_MUTE). Not bundled into
    // muteOn()/muteOff() -- the daughter treats them as unrelated. Not wired
    // to any button/menu yet.
    void ampMuteOn();
    void ampMuteOff();
    bool isAmpMuted() const { return m_ampMuted; }

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
    CalR     m_calMode           = CalR::KEEP_R;
    uint32_t m_calOhms           = 0;
    uint32_t m_lastVolCommandMs  = 0;     // throttles VOL UP/DOWN repeats
    uint32_t m_lastSeenStatusCount = 0;
    bool     m_wasCalibrating    = false; // edge-detects the daughter's calibration start/end for logging
};
