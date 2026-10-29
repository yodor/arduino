#pragma once
#include <Arduino.h>

// Facts about this board and its firmware, in one place: the boot report
// (printBootReport()) and Diagnostics > Versions > Master both use them.
namespace SystemInfo {
    const char* chipName();        // "RP2350" / "RP2040"
    unsigned    chipRevision();    // RP2350 silicon revision: 2 = A2 (0 on RP2040)
    const char* archName();        // "ARM Cortex-M33" / "RISC-V (Hazard3)"
    const char* uniqueId();        // the flash chip's unique ID, hex
    uint32_t    cpuMHz();
    uint32_t    periMHz();
    uint32_t    adcMHz();
    uint32_t    flashKB();         // 0 if unknown
    const char* coreVersion();     // Arduino-Pico core ("" if unknown)
    const char* sdkVersion();      // Pico SDK ("" if unknown)

    void printBootReport();        // the "--- HARDWARE & CLOCK VALIDATION ---" block
}
