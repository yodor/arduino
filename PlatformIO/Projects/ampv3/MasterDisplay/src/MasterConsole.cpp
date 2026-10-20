#include "MasterConsole.hpp"
#include "DaughterBoardLink.hpp"
#include "Version.hpp"
#include "RtcClock.hpp"
#include <cstdio>
#include <cctype>
#include <cstring>

MasterConsole& MasterConsole::instance() {
    static MasterConsole inst;
    return inst;
}

void MasterConsole::begin() {
    Serial.println("[Console] Type HELP for console commands.");
}

void MasterConsole::printHelp() {
    Serial.println("--- " FW_NAME " " FW_VERSION " console ---");
    Serial.println("  HELP       this list");
    Serial.println("  DAUGHTER   pass-through to the daughter board's bench console");
    Serial.println("             (type ~. at the start of a line to come back)");
    Serial.println("  TIME       show the clock module's time");
    Serial.println("  TIME YYYY-MM-DD HH:MM:SS   set it");
}

void MasterConsole::enterPassthrough() {
    Serial.println("[Console] Pass-through to the daughter console. Type ~. at the start of a line to return.");
    m_atLineStart = true;
    m_heldTilde   = false;
    DaughterBoardLink::instance().enterPassthrough(); // sends DEBUG
}

void MasterConsole::leavePassthrough() {
    DaughterBoardLink::instance().exitPassthrough(); // sends EXIT
    m_len = 0;
    Serial.println();
    Serial.println("[Console] Back on the master console (daughter returned to strict protocol).");
}

// TIME                         -> print the RTC time
// TIME YYYY-MM-DD HH:MM:SS     -> set it (24h, local time)
void MasterConsole::handleTime(const char* args) {
    RtcClock& rtc = RtcClock::instance();
    if (!rtc.isPresent()) {
        Serial.println("[Console] No DS3231 clock module found");
        return;
    }

    while (*args == ' ') ++args;
    char buf[20];
    if (*args == '\0') {
        RtcDateTime now;
        if (rtc.read(now)) {
            RtcClock::format(now, buf, sizeof(buf));
            Serial.print("[Console] Time: ");
            Serial.println(buf);
        } else {
            Serial.println("[Console] Reading the clock failed");
        }
        return;
    }

    int y, mo, d, h, mi, s;
    if (sscanf(args, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) {
        Serial.println("[Console] Usage: TIME YYYY-MM-DD HH:MM:SS");
        return;
    }
    static const uint8_t kDays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (y % 4 == 0); // fine for 2000-2099
    bool ok = y >= 2000 && y <= 2099 && mo >= 1 && mo <= 12 && d >= 1 &&
              d <= (mo == 2 ? (leap ? 29 : 28) : kDays[mo - 1]) &&
              h >= 0 && h <= 23 && mi >= 0 && mi <= 59 && s >= 0 && s <= 59;
    if (!ok) {
        Serial.println("[Console] That date/time isn't valid (years 2000-2099, 24h clock)");
        return;
    }

    RtcDateTime t;
    t.year = static_cast<uint16_t>(y); t.month = static_cast<uint8_t>(mo); t.day = static_cast<uint8_t>(d);
    t.hour = static_cast<uint8_t>(h);  t.minute = static_cast<uint8_t>(mi); t.second = static_cast<uint8_t>(s);
    if (rtc.set(t)) {
        RtcClock::format(t, buf, sizeof(buf));
        Serial.print("[Console] Clock set to ");
        Serial.println(buf);
    } else {
        Serial.println("[Console] Setting the clock failed");
    }
}

void MasterConsole::handleLine(char* line) {
    // trim + uppercase
    while (*line == ' ' || *line == '\t') ++line;
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = '\0';
    if (n == 0) return;
    for (size_t i = 0; i < n; ++i) line[i] = static_cast<char>(toupper(static_cast<unsigned char>(line[i])));

    if (!strcmp(line, "HELP") || !strcmp(line, "?")) {
        printHelp();
    } else if (!strcmp(line, "DAUGHTER") || !strcmp(line, "D")) {
        enterPassthrough();
    } else if (!strncmp(line, "TIME", 4) && (line[4] == '\0' || line[4] == ' ')) {
        handleTime(line + 4);
    } else {
        Serial.print("[Console] Unknown command '");
        Serial.print(line);
        Serial.println("' -- type HELP");
    }
}

void MasterConsole::update() {
    DaughterBoardLink& link = DaughterBoardLink::instance();

    if (link.isPassthrough()) {
        link.passthroughPump(Serial);

        while (Serial.available() > 0) {
            char c = static_cast<char>(Serial.read());

            if (m_heldTilde) {
                m_heldTilde = false;
                if (c == '.') {
                    leavePassthrough();
                    return; // the rest of this line (its newline) lands in the normal console and is ignored
                }
                link.passthroughWrite('~');
                if (c == '~') { m_atLineStart = false; continue; } // "~~" = one literal '~'
            } else if (m_atLineStart && c == '~') {
                m_heldTilde = true;
                continue;
            }

            link.passthroughWrite(c);
            m_atLineStart = (c == '\r' || c == '\n');
        }
        return;
    }

    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        if (c == '\r' || c == '\n') {
            if (m_len > 0) {
                m_line[m_len] = '\0';
                handleLine(m_line);
                m_len = 0;
            }
        } else if (m_len < sizeof(m_line) - 1) {
            m_line[m_len++] = c;
        }
    }
}
