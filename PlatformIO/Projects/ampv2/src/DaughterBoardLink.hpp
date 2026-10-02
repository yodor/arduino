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
// Protocol v2 notes (see daughter-board-uart-protocol-v2.md for the full
// spec this implements): one daughter controller runs both channels, so
// VOL/MUTE/GET apply to both together -- there is no per-channel
// addressing here. AMP_MUTE is a third, independent mute path (the
// daughter's own amp-output opto mute) with no relation to MUTE (the LDR
// network's own mute) or this master's separate local MUTE_PIN circuit --
// all three can be engaged independently of each other.
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
    bool sendVolSet(uint8_t level); // DAUGHTER_VOL_MIN..DAUGHTER_VOL_MAX; rejected locally (not sent) if out of range
    bool sendGetMute();
    bool sendGetVol();
    bool sendCalibrate();

    // Independent amp-output mute path (daughter's own opto circuit, not
    // the LDR network's MUTE above). The daughter gates both directions
    // during its own boot hold and replies ERR either way during that
    // window rather than silently dropping the command -- that ERR flows
    // through getLastStatus()/getLastError() like any other, this class
    // doesn't attempt to track or replicate the daughter's own hold timer.
    bool sendAmpMuteOn();
    bool sendAmpMuteOff();
    bool sendGetAmpMute();

    bool isBusy() const;         // a command is currently in flight, awaiting a reply
    bool isCalibrating() const;  // a CAL is in progress (from the initial OK until DONE/FAIL/safety-timeout); sendXxx() calls will fail

    // Valid once isCalibrating() has gone from true back to false:
    // OK = CAL DONE, ERROR = CAL FAIL (see getLastError() for the reason),
    // TIMEOUT = DAUGHTER_CAL_SAFETY_TIMEOUT_MS elapsed with no DONE/FAIL
    // ever arriving (a crash/disconnect guard, not an expected outcome --
    // see that constant's comment in Config.hpp). Reusing the same status/
    // error fields as ordinary commands is safe here specifically because
    // sendLine() refuses to send anything else at all while isCalibrating()
    // is true, so nothing else can overwrite this in between.
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
};