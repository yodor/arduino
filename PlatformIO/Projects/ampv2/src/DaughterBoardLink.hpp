#pragma once
#include "Config.hpp"

// Result of the most recently completed (or in-flight) command sent to the
// daughter board. Also used for calibration's own async outcome (CAL
// DONE/FAIL) -- see isCalibrating()'s comment for why that reuse is safe.
enum class DaughterCmdStatus : uint8_t {
    IDLE,     // no command in flight, ready to send
    PENDING,  // command sent, awaiting a reply
    OK,       // last command (or calibration run) completed successfully
    ERROR,    // last command completed with an "ERR ..." reply, or calibration
              // finished with "CAL FAIL ..." -- see getLastError()
    TIMEOUT   // last command got no reply within DAUGHTER_RESPONSE_TIMEOUT_MS,
              // or calibration never reached DONE/FAIL within
              // DAUGHTER_CAL_SAFETY_TIMEOUT_MS
};

// Non-blocking, line-based text protocol link to a daughter board over
// UART0 (pins configurable via init() -- see Pins.hpp's DAUGHTER_UART_TX_PIN/
// RX_PIN for the actual current assignment). Only one command is ever in
// flight at a time -- send*() calls fail immediately (returning false,
// transmitting nothing) if a previous command is still pending a reply, or
// if a calibration is in progress. update() must be called every loop()
// iteration to service incoming bytes and time out stalled commands;
// nothing here ever blocks waiting on the UART.
//
// Protocol v2 notes (see daughter-board-uart-protocol-v2.md for the full,
// now-finalized spec this implements): one daughter controller runs both
// channels, so VOL/MUTE/GET apply to both together -- there is no per-
// channel addressing here. AMP_MUTE is a third, independent mute path (the
// daughter's own amp-output opto mute) with no relation to MUTE (the LDR
// network's own mute) or this master's separate local MUTE_PIN circuit --
// all three can be engaged independently of each other. The daughter's own
// boot-mute hold blocks EVERY command (not just AMP_MUTE) with a specific,
// parseable ERR reply -- see isBootHoldError()/getBootHoldRemainingMs().
class DaughterBoardLink {
public:
    static DaughterBoardLink& instance();

    void init(uint8_t txPin, uint8_t rxPin, uint32_t baud);
    void update();

    // Command senders. Each returns false immediately without transmitting
    // anything if isBusy() or isCalibrating() would otherwise be true --
    // check those first if you want to distinguish "didn't send" from
    // "sent but failed".
    bool sendMuteOn();
    bool sendMuteOff();
    bool sendVolUp();
    bool sendVolDown();
    // DAUGHTER_VOL_MIN..DAUGHTER_VOL_MAX; rejected locally (not sent) if
    // out of range. A jump of more than one step auto-ramps through every
    // intermediate step on the daughter's own side (confirmed on real
    // hardware: avoids an audible pop) -- worst case ~60-250ms for a
    // full-range jump, comfortably inside DAUGHTER_RESPONSE_TIMEOUT_MS but
    // not instantaneous the way sendVolUp()/sendVolDown() are.
    bool sendVolSet(uint8_t level);
    bool sendGetMute();
    bool sendGetVol();
    bool sendCalibrate();

    // Independent amp-output mute path (daughter's own opto circuit, not
    // the LDR network's MUTE above).
    bool sendAmpMuteOn();
    bool sendAmpMuteOff();
    bool sendGetAmpMute();

    bool isBusy() const;         // a command is currently in flight, awaiting a reply
    bool isCalibrating() const;  // a CAL is in progress (from the initial OK until DONE/FAIL/safety-timeout); sendXxx() calls will fail

    // The daughter's boot-mute hold blocks EVERY command (not just
    // AMP_MUTE), replying "ERR boot mute hold active, <n>ms remaining" to
    // whatever was sent -- this class doesn't track or replicate the
    // daughter's own hold timer itself, but does parse this specific,
    // stable-worded reply so a caller can schedule a sensible retry
    // instead of guessing. Valid when getLastStatus()==ERROR; check
    // isBootHoldError() first since an ordinary ERR (wrong range, no
    // calibration loaded, etc.) leaves getBootHoldRemainingMs() at 0,
    // which is indistinguishable from "just received the final ms of the
    // hold" -- isBootHoldError() is what actually tells them apart.
    bool     isBootHoldError() const        { return m_lastErrorIsBootHold; }
    uint32_t getBootHoldRemainingMs() const { return m_bootHoldRemainingMs; }

    // Valid once isCalibrating() has gone from true back to false:
    // OK = CAL DONE, ERROR = CAL FAIL (see getLastError() for the reason --
    // the exact wording is now settled on the daughter's side, but this
    // class deliberately only distinguishes DONE vs FAIL, not the reason
    // text itself, per the protocol doc's own guidance not to hardcode
    // parsing beyond that), TIMEOUT = DAUGHTER_CAL_SAFETY_TIMEOUT_MS
    // elapsed with no DONE/FAIL ever arriving (a crash/disconnect guard,
    // not an expected outcome -- see that constant's comment in
    // Config.hpp). CAL FAIL is also a whole-request failure, not per-
    // channel -- either channel coming back degenerate fails the whole
    // CAL, there's no partial-success reply to distinguish. Reusing the
    // same status/error fields as ordinary commands is safe here
    // specifically because sendLine() refuses to send anything else at
    // all while isCalibrating() is true, so nothing else can overwrite
    // this in between.
    //
    // Nothing needs to be sent after a successful CAL DONE to get usable
    // volume state -- the daughter lands on a real, defined LUT step (or
    // its own hard mute, if a channel's calibration came back degenerate)
    // automatically, same as it does at boot. GET VOL after CAL DONE
    // already reports something real.
    DaughterCmdStatus getLastStatus() const { return m_status; }
    const char* getLastError() const { return m_lastError; } // valid when getLastStatus()==ERROR

    // Valid after a completed GET MUTE / GET VOL / GET AMP_MUTE (i.e. once
    // getLastStatus() == OK following that specific request) -- not
    // automatically kept in sync otherwise, since nothing here polls on
    // its own.
    bool    getLastMute() const    { return m_lastMute; }
    bool    getLastAmpMute() const { return m_lastAmpMute; }
    uint8_t getLastVol() const     { return m_lastVol; }

private:
    DaughterBoardLink() = default;

    bool sendLine(const char* line);
    void handleResponseLine(const char* line);

    enum class PendingCmd : uint8_t {
        NONE, MUTE_ON, MUTE_OFF, VOL_UP, VOL_DOWN, VOL_SET, GET_MUTE, GET_VOL, CAL,
        AMP_MUTE_ON, AMP_MUTE_OFF, GET_AMP_MUTE
    };

    static constexpr size_t kLineBufSize = 64;
    char   m_lineBuf[kLineBufSize] = {};
    size_t m_lineLen = 0;

    DaughterCmdStatus m_status  = DaughterCmdStatus::IDLE;
    PendingCmd        m_pending = PendingCmd::NONE;
    uint32_t          m_commandSentMs = 0;

    bool     m_calibrating = false;
    uint32_t m_calStartMs  = 0;

    char    m_lastError[48] = {}; // widened from 32 -- real CAL FAIL reasons ("no ADS1115 detected on channel...") run longer than the old generic ERR text this was originally sized for
    bool    m_lastMute    = false;
    bool    m_lastAmpMute = false;
    uint8_t m_lastVol     = 0;

    bool     m_lastErrorIsBootHold = false;
    uint32_t m_bootHoldRemainingMs = 0;
};