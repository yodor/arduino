#include "DaughterBoardLink.hpp"
#include <cstring>
#include <cstdlib>
#include <cstdio>

DaughterBoardLink& DaughterBoardLink::instance() {
    static DaughterBoardLink inst;
    return inst;
}

void DaughterBoardLink::init(uint8_t txPin, uint8_t rxPin, uint32_t baud) {
    Serial1.setTX(txPin);
    Serial1.setRX(rxPin);
    Serial1.begin(baud);
}

bool DaughterBoardLink::isBusy() const {
    return m_status == DaughterCmdStatus::PENDING;
}

bool DaughterBoardLink::isCalibrating() const {
    return m_calibrating;
}

bool DaughterBoardLink::sendLine(const char* line) {
    if (m_status == DaughterCmdStatus::PENDING) return false; // one command in flight at a time
    if (m_calibrating) return false;                          // see DAUGHTER_CAL_HOLD_MS in Config.hpp

    Serial1.print(line);
    Serial1.print('\n');

    m_status        = DaughterCmdStatus::PENDING;
    m_commandSentMs = millis();
    m_lineLen       = 0; // fresh reply incoming; discard anything stale in the buffer
    return true;
}

bool DaughterBoardLink::sendMuteOn() {
    if (!sendLine("MUTE ON")) return false;
    m_pending = PendingCmd::MUTE_ON;
    return true;
}

bool DaughterBoardLink::sendMuteOff() {
    if (!sendLine("MUTE OFF")) return false;
    m_pending = PendingCmd::MUTE_OFF;
    return true;
}

bool DaughterBoardLink::sendVolUp() {
    if (!sendLine("VOL UP")) return false;
    m_pending = PendingCmd::VOL_UP;
    return true;
}

bool DaughterBoardLink::sendVolDown() {
    if (!sendLine("VOL DOWN")) return false;
    m_pending = PendingCmd::VOL_DOWN;
    return true;
}

bool DaughterBoardLink::sendVolSet(uint8_t level) {
    if (level < DAUGHTER_VOL_MIN || level > DAUGHTER_VOL_MAX) return false; // reject locally, don't send an out-of-range request at all

    char line[16];
    snprintf(line, sizeof(line), "VOL %u", level);
    if (!sendLine(line)) return false;

    m_pending = PendingCmd::VOL_SET;
    return true;
}

bool DaughterBoardLink::sendGetMute() {
    if (!sendLine("GET MUTE")) return false;
    m_pending = PendingCmd::GET_MUTE;
    return true;
}

bool DaughterBoardLink::sendGetVol() {
    if (!sendLine("GET VOL")) return false;
    m_pending = PendingCmd::GET_VOL;
    return true;
}

bool DaughterBoardLink::sendCalibrate() {
    if (!sendLine("CAL")) return false;
    m_pending = PendingCmd::CAL;
    return true;
}

void DaughterBoardLink::handleResponseLine(const char* line) {
    if (strcmp(line, "CAL DONE") == 0) {
        // Unsolicited: arrives whenever calibration genuinely finishes,
        // independent of any pending command (there normally isn't one at
        // this point -- we're just idly waiting). Ends the hold window
        // immediately rather than waiting out the rest of
        // DAUGHTER_CAL_HOLD_MS. This is a proposed addition to the
        // protocol -- if the daughter board doesn't send this, the hold
        // window's timeout is still the fallback that ends calibration.
        m_calibrating = false;
        return;
    }

    if (strcmp(line, "OK") == 0) {
        if (m_pending == PendingCmd::CAL) {
            // This OK only means calibration STARTED, not finished -- hold
            // off sending anything else for a while. See DAUGHTER_CAL_HOLD_MS
            // in Config.hpp for why this is a placeholder, not a real
            // completion signal.
            m_calibrating = true;
            m_calStartMs  = millis();
        }
        m_status = DaughterCmdStatus::OK;
    } else if (strncmp(line, "ERR", 3) == 0) {
        strncpy(m_lastError, line, sizeof(m_lastError) - 1);
        m_lastError[sizeof(m_lastError) - 1] = '\0';
        m_status = DaughterCmdStatus::ERROR;
    } else if (strncmp(line, "MUTE=", 5) == 0) {
        m_lastMute = (line[5] == '1');
        m_status   = DaughterCmdStatus::OK;
    } else if (strncmp(line, "VOL=", 4) == 0) {
        m_lastVol = static_cast<uint8_t>(atoi(line + 4));
        m_status  = DaughterCmdStatus::OK;
    } else {
        // Unrecognized line -- log it for visibility but don't treat it as
        // a hard error. The daughter board protocol is still being
        // designed, so this could be debug chatter mixed into the link
        // rather than a real protocol violation.
        Serial.print("[Daughter] Unrecognized reply: ");
        Serial.println(line);
    }

    m_pending = PendingCmd::NONE;
}

void DaughterBoardLink::update() {
    while (Serial1.available() > 0) {
        char c = static_cast<char>(Serial1.read());

        if (c == '\r') continue; // tolerate CRLF from manual terminal typing
        if (c == '\n') {
            m_lineBuf[m_lineLen] = '\0';
            if (m_lineLen > 0) handleResponseLine(m_lineBuf); // ignore blank lines
            m_lineLen = 0;
        } else if (m_lineLen < kLineBufSize - 1) {
            m_lineBuf[m_lineLen++] = c;
        }
        // else: line too long -- silently drop overflow characters
    }

    if (m_status == DaughterCmdStatus::PENDING &&
        (millis() - m_commandSentMs > DAUGHTER_RESPONSE_TIMEOUT_MS)) {
        m_status  = DaughterCmdStatus::TIMEOUT;
        m_pending = PendingCmd::NONE;
    }

    if (m_calibrating && (millis() - m_calStartMs > DAUGHTER_CAL_HOLD_MS)) {
        m_calibrating = false;
    }
}