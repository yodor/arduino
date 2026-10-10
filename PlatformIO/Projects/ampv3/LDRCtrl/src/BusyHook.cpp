#include "BusyHook.hpp"

namespace BusyHook {

namespace {
constexpr uint8_t MAX_LISTENERS = 4;
constexpr uint32_t SLICE_MS = 10;

struct Listener {
  Fn fn;
  void *ctx;
};

Listener gListeners[MAX_LISTENERS];
uint8_t gCount = 0;
bool gInService = false;
uint8_t gDepth = 0; // live Scopes
} // namespace

Scope::Scope() { if (gDepth < 255) gDepth++; }
Scope::~Scope() { if (gDepth > 0) gDepth--; }

bool add(Fn fn, void *ctx) {
  if (!fn || gCount >= MAX_LISTENERS) return false;
  gListeners[gCount].fn = fn;
  gListeners[gCount].ctx = ctx;
  gCount++;
  return true;
}

void service() {
  if (gInService || gCount == 0 || gDepth == 0) return;
  gInService = true;
  for (uint8_t i = 0; i < gCount; i++) gListeners[i].fn(gListeners[i].ctx);
  gInService = false;
}

void wait(uint32_t ms) {
  uint32_t start = millis();
  service();
  for (;;) {
    uint32_t elapsed = millis() - start;
    if (elapsed >= ms) break;
    uint32_t remaining = ms - elapsed;
    delay(remaining < SLICE_MS ? remaining : SLICE_MS);
    service();
  }
}

} // namespace BusyHook
