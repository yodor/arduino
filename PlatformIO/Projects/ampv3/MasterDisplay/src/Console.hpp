#pragma once
#include <Arduino.h>
#include <cstdarg>

// All USB serial OUTPUT goes through here (command input stays in
// MasterConsole):
//
//   Console::log("Audio", "measured %s Hz", Console::num(rate, 0).s);
//       -> "[Audio] measured 44100 Hz\r\n"
//   Console::line("--- HARDWARE ---");     an untagged line
//   Console::write(c) / write(text)        raw output (pass-through, dumps)
//
// - Every line ends in \r\n (any \n inside a message becomes \r\n too), so
//   it lines up in any terminal.
// - printf-style formatting, but this core's printf has no float support:
//   Console::num(value, decimals) formats a float for a %s.
// - Safe from both cores. Core 1 (audio) never touches USB: its lines go
//   into a lock-free queue that core 0 sends from service(), so the audio
//   core can't stall on the serial port. A full queue drops the line (and
//   counts it) rather than block.
class Console {
public:
    static void log(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    static void line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
    static void blank() { line("%s", ""); }      // an empty line
    static void write(const char* text);
    static void write(char c);

    struct Num { char s[24]; };
    static Num num(float value, int decimals);

    // Core 0, every loop: sends the lines core 1 queued.
    static void service();
    static uint32_t droppedLines();

private:
    static void emit(const char* tag, const char* fmt, va_list args);
    static void sendLine(const char* text);      // core 0: text + \r\n, \n -> \r\n
};
