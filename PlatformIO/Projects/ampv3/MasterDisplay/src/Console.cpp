#include "Console.hpp"
#include "pico/platform.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace {
// Core 1's lines, waiting for core 0: a single-producer (core 1) /
// single-consumer (core 0) ring of NUL-terminated lines.
constexpr size_t kQueueBytes = 2048;
char              g_queue[kQueueBytes];
volatile uint32_t g_head = 0;   // written by core 1 only
volatile uint32_t g_tail = 0;   // written by core 0 only
volatile uint32_t g_dropped = 0;

bool onCore1() { return get_core_num() == 1; }

bool enqueue(const char* text) {
    size_t len  = strlen(text) + 1;                       // with the NUL
    uint32_t head = g_head, tail = g_tail;
    size_t used = head - tail;
    if (len > kQueueBytes - used) { g_dropped = g_dropped + 1; return false; }
    for (size_t i = 0; i < len; ++i) g_queue[(head + i) % kQueueBytes] = text[i];
    __sync_synchronize();                                 // the text before the new head
    g_head = head + static_cast<uint32_t>(len);
    return true;
}
} // namespace

void Console::sendLine(const char* text) {
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') {
            if (p == text || p[-1] != '\r') Serial.write('\r');
        }
        Serial.write(static_cast<uint8_t>(*p));
    }
    Serial.write('\r');
    Serial.write('\n');
}

void Console::emit(const char* tag, const char* fmt, va_list args) {
    char buf[320];
    int n = 0;
    if (tag && *tag) n = snprintf(buf, sizeof(buf), "[%s] ", tag);
    if (n < 0) n = 0;
    vsnprintf(buf + n, sizeof(buf) - static_cast<size_t>(n), fmt, args);
    // A trailing \n (old habit) would make an empty line: drop it.
    size_t len = strlen(buf);
    while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
    if (onCore1()) enqueue(buf);
    else           sendLine(buf);
}

void Console::log(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    emit(tag, fmt, args);
    va_end(args);
}

void Console::line(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    emit(nullptr, fmt, args);
    va_end(args);
}

void Console::write(const char* text) {
    if (onCore1()) { enqueue(text); return; }   // core 1: as a line, never raw
    for (const char* p = text; *p; ++p) Serial.write(static_cast<uint8_t>(*p));
}

void Console::write(char c) {
    if (onCore1()) return;                      // raw bytes are core 0 business
    Serial.write(static_cast<uint8_t>(c));
}

Console::Num Console::num(float value, int decimals) {
    Num r;
    if (std::isnan(value)) { snprintf(r.s, sizeof(r.s), "nan"); return r; }
    if (decimals < 0) decimals = 0;
    if (decimals > 6) decimals = 6;
    long scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10;
    bool neg = value < 0.0f;
    double a = neg ? -static_cast<double>(value) : static_cast<double>(value);
    long long scaled = static_cast<long long>(a * scale + 0.5);
    long long whole = scaled / scale, frac = scaled % scale;
    if (decimals == 0) snprintf(r.s, sizeof(r.s), "%s%lld", neg && scaled ? "-" : "", whole);
    else               snprintf(r.s, sizeof(r.s), "%s%lld.%0*lld", neg && scaled ? "-" : "", whole, decimals, frac);
    return r;
}

void Console::service() {
    uint32_t tail = g_tail;
    while (tail != g_head) {
        char line[320];
        size_t i = 0;
        char c;
        do {
            c = g_queue[tail % kQueueBytes];
            ++tail;
            if (i < sizeof(line) - 1) line[i++] = c;
        } while (c != '\0');
        line[i] = '\0';
        __sync_synchronize();
        g_tail = tail;
        sendLine(line);
    }
    static uint32_t s_reportedDrops = 0;
    if (g_dropped != s_reportedDrops) {
        s_reportedDrops = g_dropped;
        char msg[64];
        snprintf(msg, sizeof(msg), "[Console] %lu core-1 lines dropped (queue full)", static_cast<unsigned long>(s_reportedDrops));
        sendLine(msg);
    }
}

uint32_t Console::droppedLines() { return g_dropped; }
