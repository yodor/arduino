#pragma once
#include <Arduino.h>

// The master's USB serial console. Line-based commands:
//   HELP       list commands
//   ATTEN      pass-through to the attenuator board's bench console (also DAUGHTER, D)
//   SETTINGS [RESET]  print / delete the settings file
//   BUTTONS    button monitor on/off (raw edges + debounced presses)
//   TIME [YYYY-MM-DD HH:MM:SS]   show / set the DS3231 clock
//
// Pass-through: every byte typed goes straight to the attenuator (the link
// first switches it into its DEBUG bench console), every byte it sends comes
// straight back -- so its own echo, HELP, DIAG ALL etc. all work as if
// plugged into it directly. Type ~. at the start of a line to return (ssh-
// style escape; ~~ sends a single ~). The link sends the attenuator EXIT on the
// way out, so it's always back on the strict protocol afterwards. While in
// pass-through the master's own periodic logging ([Perf]) is paused and the
// attenuator link stops polling, so nothing interleaves with the session.
class MasterConsole {
public:
    static MasterConsole& instance();

    void begin();   // prints the console hint
    void update();  // call every loop()

private:
    MasterConsole() = default;

    void handleLine(char* line);
    void handleTime(const char* args);
    void enterPassthrough();
    void leavePassthrough();
    void printHelp();

    char   m_line[64] = {};
    size_t m_len = 0;

    // pass-through escape detection
    bool m_atLineStart = true;
    bool m_heldTilde   = false;
};
