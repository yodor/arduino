#pragma once

// Firmware version: build date + letter -- YYYY.MM.DD, then a, b, c... for
// further builds flashed the same day. Bump it on every build you keep, so
// the boot log and the menu always say exactly which firmware is running.
// A macro (not constexpr) so it can be glued into string literals, e.g. the
// menu label "FW " FW_VERSION.
#define FW_NAME    "RP2350 amp controller"
#define FW_VERSION "2026.10.28c"
