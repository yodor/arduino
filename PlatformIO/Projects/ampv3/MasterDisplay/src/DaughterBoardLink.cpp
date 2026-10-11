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
    // Arduino-Pico's Serial1 keeps received bytes in a software queue of
    // only 32 bytes by default (plus the UART's own 32-byte hardware FIFO);
    // anything beyond that is silently dropped. A STATUS line is ~145 bytes
    // (up to ~220 with ERR=) and at 115200 baud arrives in ~13-19ms, while
    // update() only drains it once per 10ms loop -- so the middle of every
    // status line was being lost (fields missing, PROTO read as 0, the link
    // flapping between up and lost). Must be set before begin().
    Serial1.setFIFOSize(kRxFifoSize);
    Serial1.begin(baud);
}

bool DaughterBoardLink::isStatusFresh() const {
    return m_statusData.valid && (millis() - m_statusData.receivedMs < DAUGHTER_STATUS_STALE_MS);
}

void DaughterBoardLink::setError(const char* text) {
    strncpy(m_lastError, text, sizeof(m_lastError) - 1);
    m_lastError[sizeof(m_lastError) - 1] = '\0';
    m_status = DaughterCmdStatus::ERROR;
}

bool DaughterBoardLink::sendLine(const char* line, PendingCmd cmd) {
    if (m_status == DaughterCmdStatus::PENDING) return false; // one command in flight at a time

    if (DAUGHTER_LINK_TRACE) { Serial.print("[UART TX] "); Serial.println(line); }

    // Bare '\n' terminator -- the daughter accepts '\r', '\n' or both.
    Serial1.print(line);
    Serial1.print('\n');

    m_status        = DaughterCmdStatus::PENDING;
    m_pending       = cmd;
    m_commandSentMs = millis();
    if (cmd == PendingCmd::STATUS) m_lastStatusSentMs = m_commandSentMs;
    m_lineLen       = 0;     // fresh reply incoming; discard anything stale
    m_lineOverflow  = false;
    return true;
}

bool DaughterBoardLink::sendMuteOn()     { return sendLine("MUTE ON",      PendingCmd::MUTE_ON); }
bool DaughterBoardLink::sendMuteOff()    { return sendLine("MUTE OFF",     PendingCmd::MUTE_OFF); }
bool DaughterBoardLink::sendVolUp()      { return sendLine("VOL UP",       PendingCmd::VOL_UP); }
bool DaughterBoardLink::sendVolDown()    { return sendLine("VOL DOWN",     PendingCmd::VOL_DOWN); }
bool DaughterBoardLink::sendAmpMuteOn()  { return sendLine("AMP_MUTE ON",  PendingCmd::AMP_MUTE_ON); }
bool DaughterBoardLink::sendAmpMuteOff() { return sendLine("AMP_MUTE OFF", PendingCmd::AMP_MUTE_OFF); }
bool DaughterBoardLink::sendCalibrate()  { return sendLine("CAL",          PendingCmd::CAL); }
bool DaughterBoardLink::sendStatus()     { return sendLine("STATUS",       PendingCmd::STATUS); }

bool DaughterBoardLink::sendVolSet(int16_t step) {
    // The valid range lives on the daughter now (VOLMIN/VOLMAX in STATUS),
    // so only send what a fresh, idle status says is valid.
    const DaughterStatus& s = m_statusData;
    if (!isStatusFresh() || s.busy || s.volMin < 0 || step < s.volMin || step > s.volMax) return false;

    char line[16];
    snprintf(line, sizeof(line), "VOL %d", static_cast<int>(step));
    return sendLine(line, PendingCmd::VOL_SET);
}

// Parses "OK STATUS=... KEY=value ... [ERR=free text]" into m_statusData.
// Returns false (touching nothing) if the line isn't a status line. Unknown
// keys are ignored, as the protocol requires, so the daughter can append
// fields in later revisions.
bool DaughterBoardLink::parseStatusLine(const char* line) {
    static const char kPrefix[] = "OK STATUS=";
    if (strncmp(line, kPrefix, sizeof(kPrefix) - 1) != 0) return false;

    DaughterStatus s; // fields absent from the line keep their "unknown" defaults
    const char* p = line + 3; // skip "OK "
    char key[12];
    char val[24];

    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;

        const char* sp = strchr(p, ' ');
        if (!sp) sp = p + strlen(p);
        const char* eq = strchr(p, '=');
        if (!eq || eq > sp) { p = sp; continue; } // token without '=' -- skip

        size_t klen = static_cast<size_t>(eq - p);

        // ERR is always last and its value is free text to the end of the line.
        if (klen == 3 && strncmp(p, "ERR", 3) == 0) {
            strncpy(s.err, eq + 1, sizeof(s.err) - 1);
            s.err[sizeof(s.err) - 1] = '\0';
            s.hasErr = true;
            break;
        }

        size_t vlen = static_cast<size_t>(sp - (eq + 1));
        if (klen >= sizeof(key) || vlen >= sizeof(val)) { p = sp; continue; } // unknown/oversized -- ignore
        memcpy(key, p, klen);      key[klen] = '\0';
        memcpy(val, eq + 1, vlen); val[vlen] = '\0';

        if      (!strcmp(key, "STATUS"))   s.busy      = !strcmp(val, "BUSY");
        else if (!strcmp(key, "TEMP"))     s.tempC     = static_cast<float>(atof(val));
        else if (!strcmp(key, "UP"))       s.upSeconds = strtoul(val, nullptr, 10);
        else if (!strcmp(key, "VOL"))      s.vol       = static_cast<int16_t>(atoi(val));
        else if (!strcmp(key, "DB"))       s.db        = static_cast<float>(atof(val));
        else if (!strcmp(key, "MUTE"))     s.mute      = (val[0] == '1');
        else if (!strcmp(key, "AMP_MUTE")) s.ampMute   = (val[0] == '1');
        else if (!strcmp(key, "CAL")) {
            if      (!strcmp(val, "PROCESSING")) s.cal = DaughterCalState::PROCESSING;
            else if (!strcmp(val, "OK"))         s.cal = DaughterCalState::OK;
            else if (!strcmp(val, "NONE"))       s.cal = DaughterCalState::NONE;
            else if (!strcmp(val, "ERR"))        s.cal = DaughterCalState::ERR;
        }
        else if (!strcmp(key, "VOLMIN"))   s.volMin    = static_cast<int16_t>(atoi(val));
        else if (!strcmp(key, "VOLMAX"))   s.volMax    = static_cast<int16_t>(atoi(val));
        else if (!strcmp(key, "RANGE"))    s.rangeDb   = static_cast<float>(atof(val));
        else if (!strcmp(key, "R"))        s.rOhms     = strtoul(val, nullptr, 10);
        else if (!strcmp(key, "RMIN"))     s.rMinOhms  = strtoul(val, nullptr, 10);
        else if (!strcmp(key, "RMAX"))     s.rMaxOhms  = strtoul(val, nullptr, 10);
        else if (!strcmp(key, "RAUTO"))    s.rAuto     = (val[0] == '1');
        else if (!strcmp(key, "LASTCAL")) { strncpy(s.lastCal, val, sizeof(s.lastCal) - 1); s.lastCal[sizeof(s.lastCal) - 1] = '\0'; }
        else if (!strcmp(key, "PROTO"))    s.proto     = static_cast<uint8_t>(atoi(val));
        // anything else: ignore (forward compatibility)

        p = sp;
    }

    // The protocol allows matching only these leading words of ERR= -- it's
    // how a master tells the boot hold apart from a calibration.
    static const char kHold[] = "boot mute hold active";
    if (s.busy && s.hasErr && strncmp(s.err, kHold, sizeof(kHold) - 1) == 0) {
        s.bootHold = true;
        unsigned long ms = 0;
        if (sscanf(s.err, "boot mute hold active, %lums", &ms) == 1) s.bootHoldRemainingMs = static_cast<uint32_t>(ms);
    }

    s.valid      = true;
    s.receivedMs = millis();
    m_statusData = s;
    ++m_statusCount;

    if (s.proto != DAUGHTER_PROTO_EXPECTED && !m_protoWarned) {
        m_protoWarned = true;
        Serial.print("[Daughter] WARNING: daughter reports PROTO=");
        Serial.print(s.proto);
        Serial.print(", this master is written for PROTO=");
        Serial.println(DAUGHTER_PROTO_EXPECTED);
    }
    return true;
}

void DaughterBoardLink::handleResponseLine(const char* line) {
    if (DAUGHTER_LINK_TRACE) { Serial.print("[UART RX] "); Serial.println(line); }

    PendingCmd cmd        = m_pending;
    bool       wasPending = (m_status == DaughterCmdStatus::PENDING);
    m_pending = PendingCmd::NONE;

    // Status lines are kept whenever they arrive (they're always the
    // daughter's truth), even a late one that missed its timeout.
    if (parseStatusLine(line)) {
        if (!wasPending) return; // late reply -- don't clobber a newer result

        const DaughterStatus& s = m_statusData;
        if (cmd == PendingCmd::CAL) {
            if (s.calibrating()) {
                m_status = DaughterCmdStatus::OK; // started
                Serial.println("[Daughter] Calibration started");
            } else {
                // Rejected (IDLE + CAL=ERR) or the boot hold -- either way
                // nothing was changed on the daughter.
                setError(s.hasErr ? s.err : "calibration did not start");
            }
        } else if (cmd != PendingCmd::STATUS && s.bootHold) {
            // Any setter during the boot hold is answered with a status
            // line instead of being executed.
            setError(s.err);
        } else {
            m_status = DaughterCmdStatus::OK;
        }
        return;
    }

    if (!wasPending) {
        Serial.print("[Daughter] Unexpected line (no command in flight): ");
        Serial.println(line);
        return;
    }

    if (strcmp(line, "OK") == 0) {
        m_status = DaughterCmdStatus::OK;
        // Re-read the daughter's real VOL/DB/MUTE right away so the display
        // shows what actually happened rather than a local guess.
        requestStatusRefresh();
    } else if (strncmp(line, "ERR", 3) == 0) {
        // e.g. "ERR busy" (a calibration is running -- the command was
        // dropped, not queued) or "ERR step limit or no calibration loaded".
        setError(line);
        requestStatusRefresh();
    } else {
        // Every branch must leave m_status != PENDING, or every later send
        // would be refused until the timeout (a real bug, fixed earlier).
        Serial.print("[Daughter] Unrecognized reply: ");
        Serial.println(line);
        setError(line);
    }
}

uint32_t DaughterBoardLink::currentPollIntervalMs() const {
    const DaughterStatus& s = m_statusData;
    if (!s.valid || !s.busy) return DAUGHTER_STATUS_POLL_IDLE_MS;
    if (s.bootHold) {
        // Ask again right after the hold should be over, not up to 2s later.
        uint32_t wait = s.bootHoldRemainingMs + 50;
        if (wait < 100) wait = 100;
        return (wait < DAUGHTER_STATUS_POLL_BUSY_MS) ? wait : DAUGHTER_STATUS_POLL_BUSY_MS;
    }
    return DAUGHTER_STATUS_POLL_BUSY_MS;
}

void DaughterBoardLink::update() {
    while (Serial1.available() > 0) {
        char c = static_cast<char>(Serial1.read());

        if (c == '\r') continue;
        if (c == '\n') {
            if (m_lineOverflow) {
                Serial.println("[Daughter] Reply longer than the line buffer -- dropped");
            } else if (m_lineLen > 0) {
                m_lineBuf[m_lineLen] = '\0';
                handleResponseLine(m_lineBuf);
            }
            m_lineLen      = 0;
            m_lineOverflow = false;
        } else if (m_lineLen < kLineBufSize - 1) {
            m_lineBuf[m_lineLen++] = c;
        } else {
            m_lineOverflow = true; // drop the whole line rather than parse a truncated one
        }
    }

    // Should never fire with kRxFifoSize -- if it does, a reply was mangled.
    if (Serial1.overflow()) {
        Serial.println("[Daughter] UART receive overflow -- bytes were lost");
    }

    uint32_t now = millis();

    if (m_status == DaughterCmdStatus::PENDING &&
        (now - m_commandSentMs > DAUGHTER_RESPONSE_TIMEOUT_MS)) {
        if (DAUGHTER_LINK_TRACE) Serial.println("[UART] TIMEOUT -- no reply within DAUGHTER_RESPONSE_TIMEOUT_MS");
        m_status  = DaughterCmdStatus::TIMEOUT;
        m_pending = PendingCmd::NONE;
    }

    // Link up/lost is logged on transitions only -- a unit without a
    // daughter board would otherwise log a timeout every poll forever.
    bool fresh = isStatusFresh();
    if (fresh != m_linkUp) {
        m_linkUp = fresh;
        if (fresh) {
            Serial.print("[Daughter] Link up, PROTO=");
            Serial.println(m_statusData.proto);
        } else {
            Serial.println("[Daughter] Link lost -- no STATUS reply");
        }
    }

    // Poll STATUS whenever the link is free and it's due (or was asked for).
    if (m_status != DaughterCmdStatus::PENDING) {
        bool due = (now - m_lastStatusSentMs >= currentPollIntervalMs());
        if (m_statusRefreshRequested || due) {
            if (sendStatus()) m_statusRefreshRequested = false;
        }
    }
}