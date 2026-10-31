#pragma once
// Firmware, build and chip information for the controller's "firmware info" screen (protocol 15:
// VER / FW) and console mode (VER / FW).
#include <Arduino.h>

namespace VersionInfo {
// One line, fixed keys, NOTE last (its value runs to the end of the line):
// OK VER FW= PROTO= BUILD= HW= HWREV= CALREV= CHIP= CHIPREV= MHZ= FLASH= UID= BOARD= CORE= SDK= NOTE=
void printLine(Print &out);
void printReadable(Print &out);   // the same, one item per line
// "Oct 27 2026" + "10:31:49" -> "2026-10-27T10:31:49"
void isoBuild(char *buf, size_t n);
}
