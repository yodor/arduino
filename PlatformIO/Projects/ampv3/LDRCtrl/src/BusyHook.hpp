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
// Listeners are serviced only while a Scope is alive -- i.e. inside a long
// blocking operation (a calibration, DIAG ALL). A short settle
// wait elsewhere (the 100 ms after a relay click, say) must NOT answer "busy" and
// swallow a line a person pasted right behind the command, which matters now
// that a link in DEBUG mode is serviced too.
//
// Deliberately tiny and dependency-free so both LDRVolume and
// DualCalibration can use it without including each other.
// ============================================================================
namespace BusyHook {

using Fn = void (*)(void *ctx);

// Registers a listener (up to 4). Returns false if the table is full.
bool add(Fn fn, void *ctx);

// Declare a long operation: construct one at the top of it. Nests (a calibration
// that calls a trim is two Scopes). While none is alive, service() does nothing.
class Scope {
public:
  Scope();
  ~Scope();
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;
};

// Calls every listener once, if a Scope is alive. A nested call from inside a
// listener is ignored, and with no listeners registered this is nearly free.
void service();

// delay(ms) that, inside a Scope, services the listeners at least every ~10 ms
// (and once even when ms == 0). Outside a Scope it is just a delay(). Drop-in
// replacement for delay() inside long operations.
void wait(uint32_t ms);

} // namespace BusyHook
