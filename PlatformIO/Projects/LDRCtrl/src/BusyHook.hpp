#pragma once
#include <Arduino.h>

// ============================================================================
// BusyHook
//
// Calibration sweeps, trims and settling waits block the main loop for
// minutes, during which nothing reads the strict master link -- a master
// that asks anything meanwhile just times out. This lets those blocking
// waits keep the link alive: they call BusyHook::wait()/service() instead
// of delay(), and any registered listener (MasterLink, for the real UART)
// gets to read its stream and answer "busy" immediately.
//
// Deliberately tiny and dependency-free so both LDRVolume and
// DualCalibration can use it without including each other.
// ============================================================================
namespace BusyHook {

using Fn = void (*)(void *ctx);

// Registers a listener (up to 4). Returns false if the table is full.
bool add(Fn fn, void *ctx);

// Calls every listener once. A nested call from inside a listener is
// ignored, and with no listeners registered this is nearly free.
void service();

// delay(ms) that services the listeners at least every ~10 ms (and once
// even when ms == 0). Drop-in replacement for delay() inside calibration.
void wait(uint32_t ms);

} // namespace BusyHook