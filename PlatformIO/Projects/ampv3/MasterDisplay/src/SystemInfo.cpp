#include "SystemInfo.hpp"
#include "Console.hpp"
#include "Version.hpp"
#include "hardware/clocks.h"
#if defined(PICO_RP2350) && PICO_RP2350
#include "hardware/structs/sysinfo.h"
#endif

namespace SystemInfo {

const char* chipName() {
#if defined(PICO_RP2350) && PICO_RP2350
    return "RP2350";
#else
    return "RP2040";
#endif
}

unsigned chipRevision() {
#if defined(PICO_RP2350) && PICO_RP2350
    return (sysinfo_hw->chip_id >> 28) & 0xF;
#else
    return 0;
#endif
}

const char* archName() {
#if defined(__riscv) || defined(__riscv__)
    return "RISC-V (Hazard3)";
#elif defined(__arm__)
    return "ARM Cortex-M33";
#else
    return "unknown";
#endif
}

const char* uniqueId() { return rp2040.getChipID(); }
uint32_t    cpuMHz()   { return rp2040.f_cpu() / 1000000; }
uint32_t    periMHz()  { return clock_get_hz(clk_peri) / 1000000; }
uint32_t    adcMHz()   { return clock_get_hz(clk_adc) / 1000000; }

uint32_t flashKB() {
#ifdef PICO_FLASH_SIZE_BYTES
    return PICO_FLASH_SIZE_BYTES / 1024;
#else
    return 0;
#endif
}

const char* coreVersion() {
#ifdef ARDUINO_PICO_VERSION_STR
    return ARDUINO_PICO_VERSION_STR;
#else
    return "";
#endif
}

const char* sdkVersion() {
#ifdef PICO_SDK_VERSION_STRING
    return PICO_SDK_VERSION_STRING;
#else
    return "";
#endif
}

void printBootReport() {
    Console::blank();
    Console::line("--- " FW_NAME " " FW_VERSION " ---");
    Console::line("--- HARDWARE & CLOCK VALIDATION ---");
#if defined(PICO_RP2350) && PICO_RP2350
    Console::line("Compile Target: RP2350 Core (Pico 2)");
    Console::line("Silicon Revision: RP2350 A%u", chipRevision());
#else
    Console::line("Compile Target: Older RP2040 Core (Pico 1)");
#endif
    Console::line("Arch: %s", archName());
    Console::line("Unique Flash ID: %s", uniqueId());
    Console::line("Active CPU Clock: %lu MHz", static_cast<unsigned long>(cpuMHz()));
    Console::line("Peripheral Clock (clk_peri): %lu MHz", static_cast<unsigned long>(periMHz()));
    Console::line("ADC Clock (clk_adc): %lu MHz", static_cast<unsigned long>(adcMHz()));
}

} // namespace SystemInfo
