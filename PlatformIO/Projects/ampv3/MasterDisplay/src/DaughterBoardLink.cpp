#include "DaughterBoardLink.hpp"
#include "Console.hpp"
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

    // If the master rebooted while the daughter was left in its DEBUG bench
    // console (e.g. mid pass-through), every STATUS reply would be human
    // text. EXIT puts it back on the strict protocol; on a daughter already
    // in strict mode it's only answered with an error (or the boot-hold
    // status line), which is swallowed here.
    Serial1.print("EXIT\n");
    drainAfterExit(kExitDrainMs);
}

void DaughterBoardLink::drainAfterExit(uint32_t ms) {
    m_discardUntilMs = millis() + ms;
    m_lineLen        = 0;
    m_lineOverflow   = false;
    m_status         = DaughterCmdStatus::IDLE;
    m_pending        = PendingCmd::NONE;
    m_statusRefreshRequested = true; // re-sync as soon as the drain window ends
}

void DaughterBoardLink::enterPassthrough() {
    if (m_passthrough) return;
    m_passthrough = true;
    // Anything in flight is abandoned -- its reply (if any) is forwarded raw.
    m_status  = DaughterCmdStatus::IDLE;
    m_pending = PendingCmd::NONE;
    m_lineLen = 0;
    Serial1.print("DEBUG\n");
}

void DaughterBoardLink::exitPassthrough() {
    if (!m_passthrough) return;
    m_passthrough = false;
    Serial1.print("EXIT\n");
    drainAfterExit(kExitDrainMs);
    m_linkUp = isStatusFresh(); // no "link lost" log just because the session ran long
}

void DaughterBoardLink::passthroughWrite(char c) {
    if (m_passthrough) Serial1.write(static_cast<uint8_t>(c));
}

void DaughterBoardLink::passthroughPump(Print& out) {
    if (!m_passthrough) return;
    while (Serial1.available() > 0) {
        out.write(static_cast<uint8_t>(Serial1.read()));
    }
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
    if (m_passthrough) return false;                          // the console owns the UART
    if (m_status == DaughterCmdStatus::PENDING) return false; // one command in flight at a time

    if (DAUGHTER_LINK_TRACE) Console::log("UART TX", "%s", line);

    // Bare '\n' terminator -- the daughter accepts '\r', '\n' or both.
    Serial1.print(line);
    Serial1.print('\n');

    m_status        = DaughterCmdStatus::PENDING;
    m_pending       = cmd;
    m_commandSentMs = millis();
    if (cmd == PendingCmd::STATUS)   m_lastStatusSentMs   = m_commandSentMs;
    if (cmd == PendingCmd::ENV)      m_lastEnvSentMs      = m_commandSentMs;
    if (cmd == PendingCmd::PROGRESS) m_lastProgressSentMs = m_commandSentMs;
    if (cmd == PendingCmd::LEARN)    m_lastLearnSentMs    = m_commandSentMs;
    if (cmd == PendingCmd::MODEL)    m_lastModelSentMs    = m_commandSentMs;
    if (cmd == PendingCmd::VER)      m_lastVerSentMs      = m_commandSentMs;
    if (cmd == PendingCmd::CAL)      m_calStart           = DaughterCalStart::PENDING;
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
bool DaughterBoardLink::sendCalibrateAuto() { return sendLine("CAL R=AUTO", PendingCmd::CAL); }
bool DaughterBoardLink::sendCalibrateR(uint32_t ohms) {
    char line[24];
    snprintf(line, sizeof(line), "CAL R=%lu", static_cast<unsigned long>(ohms));
    return sendLine(line, PendingCmd::CAL);
}
bool DaughterBoardLink::sendStatus()     { return sendLine("STATUS",       PendingCmd::STATUS); }
bool DaughterBoardLink::sendEnv()        { return sendLine("ENV",          PendingCmd::ENV); }
bool DaughterBoardLink::sendProgress()   { return sendLine("PROGRESS",     PendingCmd::PROGRESS); }
bool DaughterBoardLink::sendLearn()      { return sendLine("LEARN",        PendingCmd::LEARN); }
bool DaughterBoardLink::sendComp(bool on) {
    bool sent = sendLine(on ? "COMP ON" : "COMP OFF", PendingCmd::COMP);
    if (sent) { m_lastEnvSentMs = 0; requestEnv(); } // re-read COMP right after
    return sent;
}

bool DaughterBoardLink::sendSetSrc(uint32_t ohms) {
    char line[24];
    snprintf(line, sizeof(line), "SET SRC %lu", static_cast<unsigned long>(ohms));
    bool sent = sendLine(line, PendingCmd::SET_MODEL);
    if (sent) { m_lastModelSentMs = 0; requestModel(); requestStatusRefresh(); } // read both back (CAL may go STALE)
    return sent;
}

bool DaughterBoardLink::sendSetLoad(uint32_t ohms) {
    char line[24];
    snprintf(line, sizeof(line), "SET LOAD %lu", static_cast<unsigned long>(ohms));
    bool sent = sendLine(line, PendingCmd::SET_MODEL);
    if (sent) { m_lastModelSentMs = 0; requestModel(); requestStatusRefresh(); }
    return sent;
}

bool DaughterBoardLink::sendSetAuto(AutoPref pref) {
    const char* line = (pref == AutoPref::SNAPPY) ? "SET AUTO SNAPPY" : (pref == AutoPref::DEEP) ? "SET AUTO DEEP" : "SET AUTO BALANCED";
    bool sent = sendLine(line, PendingCmd::SET_MODEL);
    if (sent) { m_lastModelSentMs = 0; requestModel(); requestStatusRefresh(); } // read both back (CAL may go STALE)
    return sent;
}

bool DaughterBoardLink::sendBal(int8_t n) {
    if (n < -16) n = -16;
    if (n > 16)  n = 16;
    char line[16];
    snprintf(line, sizeof(line), "BAL %d", static_cast<int>(n));
    return sendLine(line, PendingCmd::BAL);
}

bool DaughterBoardLink::sendModel() { return sendLine("MODEL", PendingCmd::MODEL); }

bool DaughterBoardLink::sendVer() { return sendLine("VER", PendingCmd::VER); }

bool DaughterBoardLink::sendForget(Forget what) {
    const char* line = (what == Forget::CAL) ? "FORGET CAL" : (what == Forget::COMP) ? "FORGET COMP" : "FORGET ALL";
    return sendLine(line, PendingCmd::FORGET);
}

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
            else if (!strcmp(val, "CHANGED"))    s.cal = DaughterCalState::CHANGED;
            else if (!strcmp(val, "STALE"))      s.cal = DaughterCalState::STALE;
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
        Console::log("Daughter", "WARNING: daughter reports PROTO=%u, this master is written for PROTO=%u",
                     static_cast<unsigned>(s.proto), static_cast<unsigned>(DAUGHTER_PROTO_EXPECTED));
    }
    return true;
}

namespace {
// Walks "KEY=value KEY=value ..." starting at p, calling fn(key, value) for
// each pair (values without spaces; tokens without '=' are skipped).
template <typename Fn>
void forEachField(const char* p, Fn fn) {
    // Values up to 31 characters (VER's BUILD and UID are 19 and 16); longer
    // ones are skipped. Each parser copies into its own sized field.
    char key[16], val[32];
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        const char* sp = strchr(p, ' ');
        if (!sp) sp = p + strlen(p);
        const char* eq = strchr(p, '=');
        if (eq && eq < sp) {
            size_t klen = static_cast<size_t>(eq - p), vlen = static_cast<size_t>(sp - eq - 1);
            if (klen < sizeof(key) && vlen < sizeof(val)) {
                memcpy(key, p, klen);      key[klen] = '\0';
                memcpy(val, eq + 1, vlen); val[vlen] = '\0';
                fn(key, val);
            }
        }
        p = sp;
    }
}

template <size_t N>
void copyField(char (&dst)[N], const char* src) {
    strncpy(dst, src, N - 1);
    dst[N - 1] = '\0';
}
} // namespace

// "OK ENV BLOCK=22.81 CELLL=21.75 ..." (rev 8)
bool DaughterBoardLink::parseEnvLine(const char* line) {
    static const char kPrefix[] = "OK ENV ";
    if (strncmp(line, kPrefix, sizeof(kPrefix) - 1) != 0) return false;
    DaughterEnv e;
    forEachField(line + sizeof(kPrefix) - 1, [&](const char* k, const char* v) {
        if      (!strcmp(k, "BLOCK"))  copyField(e.block, v);
        else if (!strcmp(k, "RAIL"))   copyField(e.rail, v);
        else if (!strcmp(k, "COMP"))   copyField(e.comp, v);
        else if (!strcmp(k, "OFFL"))   copyField(e.offL, v);
        else if (!strcmp(k, "OFFR"))   copyField(e.offR, v);
        else if (!strcmp(k, "KL"))     copyField(e.kL, v);
        else if (!strcmp(k, "KR"))     copyField(e.kR, v);
        else if (!strcmp(k, "DBLOCK")) copyField(e.dBlock, v);
        else if (!strcmp(k, "MATCH"))  copyField(e.match, v);
        else if (!strcmp(k, "NTC"))    copyField(e.ntc, v);
        else if (!strcmp(k, "RESYNC")) e.resync = (v[0] == '1') ? 1 : (v[0] == '0' ? 0 : -1);
        else if (!strcmp(k, "WHY"))    copyField(e.why, v);
    });
    e.valid      = true;
    e.receivedMs = millis();
    m_env = e;
    return true;
}

// "OK PROGRESS KIND=RESYNC PHASE=TRIM SIDE=L STEP=12/32 PCT=11 ETA=122" (rev 8)
bool DaughterBoardLink::parseProgressLine(const char* line) {
    static const char kPrefix[] = "OK PROGRESS";
    if (strncmp(line, kPrefix, sizeof(kPrefix) - 1) != 0) return false;
    DaughterProgress pr;
    forEachField(line + sizeof(kPrefix) - 1, [&](const char* k, const char* v) {
        if      (!strcmp(k, "KIND"))  copyField(pr.kind, v);
        else if (!strcmp(k, "PHASE")) copyField(pr.phase, v);
        else if (!strcmp(k, "SIDE"))  copyField(pr.side, v);
        else if (!strcmp(k, "STEP"))  copyField(pr.step, v);
        else if (!strcmp(k, "PCT"))   pr.pct  = (v[0] == '-') ? -1 : static_cast<int16_t>(atoi(v));
        else if (!strcmp(k, "ETA"))   pr.etaS = (v[0] == '-') ? -1 : static_cast<int32_t>(atol(v));
    });
    pr.valid      = true;
    pr.receivedMs = millis();
    m_progress = pr;
    return true;
}

// "OK LEARN N=7 SPREAD=1.24 COMP=ON KL=-5.84 R2L=0.99 ..." (rev 9)
bool DaughterBoardLink::parseLearnLine(const char* line) {
    static const char kPrefix[] = "OK LEARN ";
    if (strncmp(line, kPrefix, sizeof(kPrefix) - 1) != 0) return false;
    DaughterLearn l;
    forEachField(line + sizeof(kPrefix) - 1, [&](const char* k, const char* v) {
        if      (!strcmp(k, "N"))         copyField(l.n, v);
        else if (!strcmp(k, "SPREAD"))    copyField(l.spread, v);
        else if (!strcmp(k, "COMP"))      copyField(l.comp, v);
        else if (!strcmp(k, "KL"))        copyField(l.kL, v);
        else if (!strcmp(k, "R2L"))       copyField(l.r2L, v);
        else if (!strcmp(k, "KR"))        copyField(l.kR, v);
        else if (!strcmp(k, "R2R"))       copyField(l.r2R, v);
        else if (!strcmp(k, "MINN"))      copyField(l.minN, v);
        else if (!strcmp(k, "MINSPREAD")) copyField(l.minSpread, v);
        else if (!strcmp(k, "MINR2"))     copyField(l.minR2, v);
    });
    l.valid      = true;
    l.receivedMs = millis();
    m_learn = l;
    return true;
}

// "OK VER FW=2026-10-27e PROTO=15 BUILD=... ... NOTE=free text to the end" (rev 15).
// NOTE is always last and its value has spaces, so it's split off first.
bool DaughterBoardLink::parseVerLine(const char* line) {
    static const char kPrefix[] = "OK VER ";
    if (strncmp(line, kPrefix, sizeof(kPrefix) - 1) != 0) return false;
    DaughterVersion v;
    char fields[256];
    snprintf(fields, sizeof(fields), "%s", line + sizeof(kPrefix) - 1);
    char* note = strstr(fields, " NOTE=");
    if (note) {
        snprintf(v.note, sizeof(v.note), "%s", note + 6);
        *note = '\0';
    } else if (!strncmp(fields, "NOTE=", 5)) {
        snprintf(v.note, sizeof(v.note), "%s", fields + 5);
        fields[0] = '\0';
    }
    forEachField(fields, [&](const char* k, const char* val) {
        if      (!strcmp(k, "FW"))      copyField(v.fw, val);
        else if (!strcmp(k, "PROTO"))   v.proto = static_cast<uint8_t>(atoi(val));
        else if (!strcmp(k, "BUILD"))   copyField(v.build, val);
        else if (!strcmp(k, "HW"))      copyField(v.hw, val);
        else if (!strcmp(k, "HWREV"))   copyField(v.hwRev, val);
        else if (!strcmp(k, "CALREV"))  copyField(v.calRev, val);
        else if (!strcmp(k, "CHIP"))    copyField(v.chip, val);
        else if (!strcmp(k, "CHIPREV")) copyField(v.chipRev, val);
        else if (!strcmp(k, "MHZ"))     copyField(v.mhz, val);
        else if (!strcmp(k, "FLASH"))   copyField(v.flash, val);
        else if (!strcmp(k, "UID"))     copyField(v.uid, val);
        else if (!strcmp(k, "BOARD"))   copyField(v.board, val);
        else if (!strcmp(k, "CORE"))    copyField(v.core, val);
        else if (!strcmp(k, "SDK"))     copyField(v.sdk, val);
    });
    bool first   = !m_version.valid;
    v.valid      = true;
    v.receivedMs = millis();
    m_version    = v;
    if (first) {
        Console::log("Daughter", "Firmware %s (built %s), %s %s rev %s, protocol %u", v.fw, v.build, v.chip, v.hw, v.hwRev, v.proto);
        if (v.note[0]) Console::log("Daughter", "Release note: %s", v.note);
    }
    return true;
}

// "OK MODEL SRC=300 LOAD=10000 SOLVED_SRC=47 SOLVED_LOAD=10000" (rev 12)
bool DaughterBoardLink::parseModelLine(const char* line) {
    static const char kPrefix[] = "OK MODEL ";
    if (strncmp(line, kPrefix, sizeof(kPrefix) - 1) != 0) return false;
    DaughterModel m;
    auto num = [](const char* v) -> int32_t { return (v[0] == '-' && v[1] == '-') ? -1 : static_cast<int32_t>(atol(v)); };
    forEachField(line + sizeof(kPrefix) - 1, [&](const char* k, const char* v) {
        if      (!strcmp(k, "SRC"))         m.src        = num(v);
        else if (!strcmp(k, "LOAD"))        m.load       = num(v);
        else if (!strcmp(k, "SOLVED_SRC"))  m.solvedSrc  = num(v);
        else if (!strcmp(k, "SOLVED_LOAD")) m.solvedLoad = num(v);
        else if (!strcmp(k, "AUTO"))        copyField(m.autoPref, v);
        else if (!strcmp(k, "REACH"))       m.reach      = num(v);
    });
    m.valid      = true;
    m.receivedMs = millis();
    m_model = m;
    return true;
}

void DaughterBoardLink::handleResponseLine(const char* line) {
    if (DAUGHTER_LINK_TRACE) Console::log("UART RX", "%s", line);

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
                m_status   = DaughterCmdStatus::OK; // started
                m_calStart = DaughterCalStart::STARTED;
                m_progress = DaughterProgress();     // forget the previous run's "DONE"
                m_lastProgressSentMs = 0;            // ask for progress right away
                Console::log("Daughter", "Calibration started");
            } else {
                // Rejected (IDLE + CAL=ERR) or the boot hold -- either way
                // nothing was changed on the daughter.
                setError(s.hasErr ? s.err : "calibration did not start");
                m_calStart = DaughterCalStart::REJECTED;
                strncpy(m_calStartError, m_lastError, sizeof(m_calStartError) - 1);
                m_calStartError[sizeof(m_calStartError) - 1] = '\0';
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

    if (parseEnvLine(line) || parseProgressLine(line) || parseLearnLine(line) || parseModelLine(line) || parseVerLine(line)) {
        if (wasPending) m_status = DaughterCmdStatus::OK;
        return;
    }

    if (!wasPending) {
        Console::log("Daughter", "Unexpected line (no command in flight): %s", line);
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
        if (cmd == PendingCmd::COMP || cmd == PendingCmd::FORGET || cmd == PendingCmd::SET_MODEL || cmd == PendingCmd::BAL ||
            cmd == PendingCmd::AMP_MUTE_ON || cmd == PendingCmd::AMP_MUTE_OFF) {
            Console::log("Daughter", "Command refused: %s", line);
        }
        requestStatusRefresh();
    } else {
        // Every branch must leave m_status != PENDING, or every later send
        // would be refused until the timeout (a real bug, fixed earlier).
        Console::log("Daughter", "Unrecognized reply: %s", line);
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
    if (m_passthrough) return; // MasterConsole moves the bytes

    if (static_cast<int32_t>(m_discardUntilMs - millis()) > 0) {
        while (Serial1.available() > 0) Serial1.read();
        return; // no polls either until the EXIT reply has been swallowed
    }

    while (Serial1.available() > 0) {
        char c = static_cast<char>(Serial1.read());

        if (c == '\r') continue;
        if (c == '\n') {
            if (m_lineOverflow) {
                Console::log("Daughter", "Reply longer than the line buffer -- dropped");
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
        Console::log("Daughter", "UART receive overflow -- bytes were lost");
    }

    uint32_t now = millis();

    if (m_status == DaughterCmdStatus::PENDING &&
        (now - m_commandSentMs > DAUGHTER_RESPONSE_TIMEOUT_MS)) {
        if (DAUGHTER_LINK_TRACE) Console::log("UART", "TIMEOUT -- no reply within DAUGHTER_RESPONSE_TIMEOUT_MS");
        if (m_pending == PendingCmd::CAL) {
            m_calStart = DaughterCalStart::REJECTED;
            strncpy(m_calStartError, "no reply from the daughter board", sizeof(m_calStartError) - 1);
        }
        m_status  = DaughterCmdStatus::TIMEOUT;
        m_pending = PendingCmd::NONE;
    }

    // Link up/lost is logged on transitions only -- a unit without a
    // daughter board would otherwise log a timeout every poll forever.
    bool fresh = isStatusFresh();
    if (fresh != m_linkUp) {
        m_linkUp = fresh;
        if (fresh) {
            Console::log("Daughter", "Link up, PROTO=%u", static_cast<unsigned>(m_statusData.proto));
        } else {
            Console::log("Daughter", "Link lost -- no STATUS reply");
        }
    }

    // A calibration just ended: one fresh ENV for its result (MATCH etc.).
    bool calibrating = m_statusData.valid && m_statusData.calibrating();
    if (m_wasCalibrating && !calibrating) m_lastEnvSentMs = 0, m_envWantedUntilMs = now + 3000;
    m_wasCalibrating = calibrating;

    // Polls, whenever the link is free: STATUS when due (or asked for);
    // PROGRESS every second while a calibration runs; ENV while a page
    // showing it is open.
    if (m_status != DaughterCmdStatus::PENDING && !m_holdPolls) {
        bool statusDue   = m_statusRefreshRequested || (now - m_lastStatusSentMs >= currentPollIntervalMs());
        bool progressDue = calibrating && (now - m_lastProgressSentMs >= DAUGHTER_PROGRESS_POLL_MS);
        bool envWanted   = static_cast<int32_t>(m_envWantedUntilMs - now) > 0 &&
                           (m_lastEnvSentMs == 0 || now - m_lastEnvSentMs >= DAUGHTER_ENV_POLL_MS);
        bool envBackground = m_statusData.valid && !m_statusData.busy && isStatusFresh() &&
                             (m_lastEnvSentMs == 0 || now - m_lastEnvSentMs >= DAUGHTER_ENV_BACKGROUND_POLL_MS);
        bool envDue      = envWanted || envBackground;
        bool learnDue    = static_cast<int32_t>(m_learnWantedUntilMs - now) > 0 &&
                           (m_lastLearnSentMs == 0 || now - m_lastLearnSentMs >= DAUGHTER_ENV_POLL_MS);
        bool modelDue    = static_cast<int32_t>(m_modelWantedUntilMs - now) > 0 &&
                           (m_lastModelSentMs == 0 || now - m_lastModelSentMs >= DAUGHTER_ENV_POLL_MS);
        // VER: while a page wants it, and once on its own after the link is
        // up (for the boot log) -- from a daughter that knows it (rev 15+).
        bool verDue      = (static_cast<int32_t>(m_verWantedUntilMs - now) > 0 &&
                            (m_lastVerSentMs == 0 || now - m_lastVerSentMs >= DAUGHTER_ENV_POLL_MS)) ||
                           (!m_version.valid && m_lastVerSentMs == 0 && m_statusData.valid && isStatusFresh() &&
                            m_statusData.proto >= 15);
        if (statusDue) {
            if (sendStatus()) m_statusRefreshRequested = false;
        } else if (progressDue) {
            sendProgress();
        } else if (envDue) {
            sendEnv();
        } else if (learnDue) {
            sendLearn();
        } else if (modelDue) {
            sendModel();
        } else if (verDue) {
            sendVer();
        }
    }
}
