#pragma once
#include "Config.hpp"

// Result of the most recently completed (or in-flight) command sent to the
// daughter board.
enum class DaughterCmdStatus : uint8_t {
    IDLE,     // no command in flight, ready to send
    PENDING,  // command sent, awaiting a reply
    OK,       // last command completed successfully
    ERROR,    // last command completed with an "ERR ..." reply -- see getLastError()
    TIMEOUT   // last command got no reply within DAUGHTER_RESPONSE_TIMEOUT_MS
};

// Non-blocking, line-based text protocol link to a daughter board over
// UART0 (remapped to GPIO12/13). Only one command is ever in flight at a
// time -- send*() calls fail immediately (returning false, transmitting
// nothing) if a previous command is still pending a reply, or if we're
// within the post-CAL hold window. update() must be called every loop()
// iteration to service incoming bytes and time out stalled commands;
// nothing here ever blocks waiting on the UART.
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

    bool isBusy() const;         // a command is currently in flight, awaiting a reply
    bool isCalibrating() const;  // within the post-CAL hold window; sendXxx() calls will fail

    DaughterCmdStatus getLastStatus() const { return m_status; }
    const char* getLastError() const { return m_lastError; } // valid when getLastStatus()==ERROR

    // Valid after a completed GET MUTE / GET VOL (i.e. once getLastStatus()
    // == OK following that specific request) -- not automatically kept in
    // sync otherwise, since nothing here polls on its own.
    bool    getLastMute() const { return m_lastMute; }
    uint8_t getLastVol() const  { return m_lastVol; }

private:
    DaughterBoardLink() = default;

    bool sendLine(const char* line);
    void handleResponseLine(const char* line);

    enum class PendingCmd : uint8_t {
        NONE, MUTE_ON, MUTE_OFF, VOL_UP, VOL_DOWN, VOL_SET, GET_MUTE, GET_VOL, CAL
    };

    static constexpr size_t kLineBufSize = 64;
    char   m_lineBuf[kLineBufSize] = {};
    size_t m_lineLen = 0;

    DaughterCmdStatus m_status  = DaughterCmdStatus::IDLE;
    PendingCmd        m_pending = PendingCmd::NONE;
    uint32_t          m_commandSentMs = 0;

    bool     m_calibrating = false;
    uint32_t m_calStartMs  = 0;

    char    m_lastError[32] = {};
    bool    m_lastMute = false;
    uint8_t m_lastVol  = 0;
};