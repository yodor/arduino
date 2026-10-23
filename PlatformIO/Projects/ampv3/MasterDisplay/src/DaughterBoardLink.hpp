#pragma once
#include "Config.hpp"

// Result of the most recently completed (or in-flight) command.
enum class DaughterCmdStatus : uint8_t {
    IDLE,     // no command in flight, ready to send
    PENDING,  // command sent, awaiting a reply
    OK,       // last command succeeded
    ERROR,    // last command got "ERR ...", was refused by the boot hold, or
              // (CAL) did not start -- see getLastError()
    TIMEOUT   // no reply within DAUGHTER_RESPONSE_TIMEOUT_MS
};

// The daughter's calibration state as reported by STATUS's CAL= field.
// Revision 10: CHANGED (stored calibration belongs to other cells -> full
// characterization) and STALE (same cells, Rtotal policy or model changed ->
// quick re-solve). Anything but OK means VOL/MUTE OFF are refused and the
// LDR mute stays on until a calibration ends with CAL=OK.
enum class DaughterCalState : uint8_t { UNKNOWN, PROCESSING, OK, NONE, CHANGED, STALE, ERR };

// User-facing wording for a calibration state (menu, overlay, log).
inline const char* calStateText(DaughterCalState c) {
    switch (c) {
        case DaughterCalState::OK:         return "OK";
        case DaughterCalState::NONE:       return "no calibration";
        case DaughterCalState::CHANGED:    return "cells changed";
        case DaughterCalState::STALE:      return "re-solve needed";
        case DaughterCalState::ERR:        return "error";
        case DaughterCalState::PROCESSING: return "running";
        default:                           return "--";
    }
}

// Everything the daughter reports in one STATUS line (protocol revision 7).
// Only `valid` / `receivedMs` are master-side bookkeeping; every other field
// is exactly what the daughter sent. While a calibration runs (busy && cal ==
// PROCESSING) the protocol says every field except STATUS/TEMP/UP/CAL is
// transient and must be ignored -- see calibrating().
struct DaughterStatus {
    bool     valid      = false; // at least one status line parsed since boot
    uint32_t receivedMs = 0;     // millis() when it arrived

    bool     busy     = false;   // STATUS=BUSY
    bool     bootHold = false;   // busy because of the daughter's boot mute hold (ERR= starts "boot mute hold active")
    uint32_t bootHoldRemainingMs = 0;

    float    tempC     = 0.0f;
    uint32_t upSeconds = 0;
    int16_t  vol       = -1;     // 0-based; -1 = no usable calibration
    float    db        = 0.0f;
    bool     mute      = false;  // LDR network mute
    bool     ampMute   = false;  // the daughter's amp opto mute
    DaughterCalState cal = DaughterCalState::UNKNOWN;
    int16_t  volMin    = -1;
    int16_t  volMax    = -1;
    float    rangeDb   = 0.0f;
    uint32_t rOhms     = 0;
    uint32_t rMinOhms  = 0;
    uint32_t rMaxOhms  = 0;
    bool     rAuto     = false;
    char     lastCal[10] = "";   // FULL | RESYNC | RESOLVE | NONE
    uint8_t  proto     = 0;
    bool     hasErr    = false;
    char     err[128]  = "";     // ERR= free text (only when hasErr)

    bool calibrating() const { return busy && cal == DaughterCalState::PROCESSING; }
};

// Non-blocking, line-based link to the daughter (LDR volume) board over UART0
// (pins: Pins.hpp DAUGHTER_UART_TX_PIN/RX_PIN), implementing protocol
// revision 7 (daughter-board-uart-protocol-v2.md):
//  - setters (MUTE, VOL, AMP_MUTE) answer OK / ERR ...;
//  - STATUS is the ONLY getter -- one line carrying every readable value,
//    parsed into status();
//  - CAL answers with a status line (BUSY = started; IDLE + CAL=ERR = did
//    not start), and completion is learned by polling STATUS until IDLE --
//    there is no CAL DONE/FAIL anymore;
//  - during the daughter's boot hold, EVERY command is answered with a
//    status line (STATUS=BUSY, ERR=boot mute hold active, <n>ms remaining)
//    instead of being executed.
// One command in flight at a time; update() must run every loop() -- it
// reads replies, times out stalled commands and polls STATUS on its own
// (DAUGHTER_STATUS_POLL_*), so nothing else has to.
// Revision 8 ENV reply -- temperatures, rail, compensation. Kept as the
// daughter's own text ("--" when not applicable): it is only displayed.
struct DaughterEnv {
    bool     valid      = false;
    uint32_t receivedMs = 0;
    char block[8] = "--", cellL[8] = "--", cellR[8] = "--", rail[8] = "--", comp[10] = "--";
    char offL[8] = "--", offR[8] = "--", kL[8] = "--", kR[8] = "--", dBlock[8] = "--", match[8] = "--";
};

// Revision 8 PROGRESS reply -- what a running calibration is doing.
struct DaughterProgress {
    bool     valid      = false;
    uint32_t receivedMs = 0;
    char     kind[10]  = "";   // FULL | RESYNC | RESOLVE | NONE
    char     phase[14] = "";   // IDLE CHECK SWEEP_SERIES SWEEP_SHUNT WALK SOLVE TRIM HOLD SEEK SAVE DONE
    char     side[3]   = "";   // L / R, only for per-channel phases
    char     step[12]  = "";   // "i/n", only where the phase has a count
    int16_t  pct       = -1;   // 0..100, -1 unknown
    int32_t  etaS      = -1;   // seconds left, -1 unknown
};

// Revision 9 LEARN reply -- the thermal learning's progress (display only).
struct DaughterLearn {
    bool     valid      = false;
    uint32_t receivedMs = 0;
    char n[6] = "--", spread[8] = "--", comp[10] = "--", kL[8] = "--", r2L[8] = "--", kR[8] = "--", r2R[8] = "--";
    char minN[6] = "--", minSpread[8] = "--", minR2[8] = "--";
};

// Outcome of the last CAL command (the reply to CAL itself; completion is
// learned from STATUS afterwards).
enum class DaughterCalStart : uint8_t { NONE, PENDING, STARTED, REJECTED };

class DaughterBoardLink {
public:
    static DaughterBoardLink& instance();

    void init(uint8_t txPin, uint8_t rxPin, uint32_t baud);
    void update();

    // Setters. Each returns false without transmitting anything if another
    // command is still in flight (isBusy()) -- callers that must not lose a
    // command retry on a later loop (VolumeControl does).
    bool sendMuteOn();
    bool sendMuteOff();
    bool sendVolUp();
    bool sendVolDown();
    bool sendVolSet(int16_t step);   // 0-based; rejected locally unless a fresh status says it's within VOLMIN..VOLMAX
    bool sendAmpMuteOn();
    bool sendAmpMuteOff();
    bool sendCalibrate();                    // plain "CAL" -- the daughter decides how much work it needs (R unchanged)
    bool sendCalibrateR(uint32_t ohms);      // "CAL R=<ohms>" -- must lie within RMIN..RMAX
    bool sendCalibrateAuto();                // "CAL R=AUTO"
    bool sendStatus();
    bool sendEnv();                          // rev 8, read-only, answered even while busy
    bool sendProgress();                     // rev 8, read-only, answered even while busy
    bool sendLearn();                        // rev 9, read-only, answered even while busy

    // rev 9: "FORGET CAL" / "FORGET COMP" / "FORGET ALL" -- answered OK at
    // once, then the daughter is BUSY for ~3 s. FORGET CAL leaves the volume
    // muted until a new calibration (a full one, ~6.5 min) completes.
    enum class Forget : uint8_t { CAL, COMP, ALL };
    bool sendForget(Forget what);

    // ENV is polled only while someone wants it (the calibration pages ask
    // on every refresh); at most every DAUGHTER_ENV_POLL_MS.
    void requestEnv()   { m_envWantedUntilMs   = millis() + 3000; }
    void requestLearn() { m_learnWantedUntilMs = millis() + 3000; } // same idea, for LEARN

    // Ask for a STATUS as soon as the link is free (update() sends it).
    void requestStatusRefresh() { m_statusRefreshRequested = true; }

    bool isBusy() const { return m_status == DaughterCmdStatus::PENDING; } // a command is in flight

    const DaughterStatus& status() const { return m_statusData; }
    uint32_t statusCount() const { return m_statusCount; } // bumps on every parsed status line -- lets consumers spot fresh data cheaply
    bool isStatusFresh() const;                            // valid and younger than DAUGHTER_STATUS_STALE_MS

    const DaughterEnv&      env()      const { return m_env; }
    const DaughterProgress& progress() const { return m_progress; }
    const DaughterLearn&    learn()    const { return m_learn; }

    // The last CAL command: started, or rejected with getCalStartError().
    DaughterCalStart calStart() const        { return m_calStart; }
    const char*      getCalStartError() const { return m_calStartError; }

    DaughterCmdStatus getLastStatus() const { return m_status; }
    const char* getLastError() const { return m_lastError; } // valid when getLastStatus()==ERROR

    // --- Console pass-through (driven by MasterConsole) ---
    // While active, this class stops using the UART entirely -- no STATUS
    // polls, every send*() refuses, update() reads nothing -- and the bytes
    // are shuttled raw between the USB console and the daughter instead.
    // Entering sends "DEBUG" (the daughter's bench console); leaving sends
    // "EXIT" so the daughter is always back on the strict protocol, then
    // swallows its reply before normal polling resumes.
    void enterPassthrough();
    void exitPassthrough();
    bool isPassthrough() const { return m_passthrough; }
    void passthroughWrite(char c);          // one byte to the daughter
    void passthroughPump(Print& out);       // everything the daughter sent, to `out`

private:
    DaughterBoardLink() = default;

    enum class PendingCmd : uint8_t {
        NONE, MUTE_ON, MUTE_OFF, VOL_UP, VOL_DOWN, VOL_SET, AMP_MUTE_ON, AMP_MUTE_OFF, CAL, STATUS, ENV, PROGRESS, LEARN, FORGET
    };

    bool sendLine(const char* line, PendingCmd cmd);
    void handleResponseLine(const char* line);
    bool parseStatusLine(const char* line);
    bool parseEnvLine(const char* line);
    bool parseProgressLine(const char* line);
    bool parseLearnLine(const char* line);
    void setError(const char* text);
    uint32_t currentPollIntervalMs() const;

    // Protocol rev 7: the STATUS line runs ~130 chars, up to ~220 with ERR=,
    // and the doc requires >= 256 -- a shorter buffer silently loses the
    // tail, which is exactly where ERR= sits.
    static constexpr size_t kLineBufSize = 256;

    // Serial1's receive queue (see init()): room for two longest-possible
    // replies, so one 10ms loop of not reading can never drop bytes.
    static constexpr size_t kRxFifoSize = 512;
    char   m_lineBuf[kLineBufSize] = {};
    size_t m_lineLen      = 0;
    bool   m_lineOverflow = false;

    DaughterCmdStatus m_status  = DaughterCmdStatus::IDLE;
    PendingCmd        m_pending = PendingCmd::NONE;
    uint32_t          m_commandSentMs = 0;
    char              m_lastError[128] = {};

    DaughterStatus m_statusData;
    uint32_t       m_statusCount = 0;
    bool           m_statusRefreshRequested = true; // poll once right away at boot
    uint32_t       m_lastStatusSentMs = 0;
    bool           m_linkUp = false;                // for logging link up/lost transitions only
    bool           m_protoWarned = false;

    DaughterEnv      m_env;
    DaughterProgress m_progress;
    DaughterLearn    m_learn;
    uint32_t         m_lastLearnSentMs    = 0;
    uint32_t         m_learnWantedUntilMs = 0;
    uint32_t         m_lastEnvSentMs      = 0;
    uint32_t         m_envWantedUntilMs   = 0;
    uint32_t         m_lastProgressSentMs = 0;
    bool             m_wasCalibrating     = false;
    DaughterCalStart m_calStart = DaughterCalStart::NONE;
    char             m_calStartError[128] = {};

    bool     m_passthrough   = false;
    uint32_t m_discardUntilMs = 0;  // incoming bytes are thrown away until then (see drainAfterExit())
    void drainAfterExit(uint32_t ms);

    // How long to swallow the daughter's reply to the EXIT sent when leaving
    // pass-through (or at boot) -- the reply is a single short line.
    static constexpr uint32_t kExitDrainMs = 300;
};
