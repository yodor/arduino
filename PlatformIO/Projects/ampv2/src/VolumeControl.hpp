#pragma once
#include "Config.hpp"

// Unified mute/volume control. Always drives BOTH the direct DRV8833 motor
// + MUTE_PIN AND UART commands to a daughter board, unconditionally,
// regardless of whether either is actually physically connected on a given
// unit -- whichever is wired takes effect, the other silently has no
// effect (an unconnected GPIO toggling, or a UART command with nothing
// listening and no reply ever arriving). Firmware never needs to know or
// decide which hardware variant is actually present.
class VolumeControl {
public:
    static VolumeControl& instance();

    void init();

    // Call every loop iteration -- always services the motor and the
    // daughter board link (so replies/timeouts are processed) and, while
    // held, drives both volume paths. wantVolUpHeld/wantVolDownHeld
    // reflect continuous held-state (same semantics as VolumeMotor::update()).
    void update(bool wantVolUpHeld, bool wantVolDownHeld);

    void muteOn();
    void muteOff();
    void toggleMute();
    bool isMuted() const { return m_muted; }

    // Fires CAL over UART unconditionally. No-op on the motor/mute path,
    // which has no defined calibration procedure. Fire-and-forget: doesn't
    // track completion/timeout here (see DaughterBoardLink directly, or a
    // future calibration UI, if that's needed).
    void calibrate();

private:
    VolumeControl() = default;

    // Actually drives the GPIO + sends the UART command; no gating, no
    // side effects on the boot-hold state. init() and muteOn()/muteOff()
    // both funnel through this so the hardware-driving logic exists once.
    void applyMuteState(bool muted);

    bool     m_muted             = true;
    uint32_t m_lastVolCommandMs  = 0; // throttles the daughter-board VOL UP/DOWN repeat rate

    // Mandatory boot-time mute hold (MUTE_BOOT_HOLD_MS in Config.hpp):
    // protects speakers/amp while coupling caps and other circuits settle.
    // ALL mute commands -- including user-issued ones -- are refused
    // entirely (not deferred, not cancelable) until this elapses. This is
    // a hard safety floor, not a default state.
    bool     m_bootHoldActive = true;
    uint32_t m_bootMs         = 0;
};