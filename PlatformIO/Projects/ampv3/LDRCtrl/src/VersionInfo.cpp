#include "VersionInfo.hpp"
#include "Config.hpp"
#include "hardware/structs/sysinfo.h"
#include "hardware/clocks.h"
#if __has_include("pico/version.h")
#include "pico/version.h"
#endif
#include <string.h>

namespace {
const char *chipName() {
#if defined(PICO_RP2350) && PICO_RP2350
  return "RP2350";
#else
  return "RP2040";
#endif
}
const char *coreVersion() {
#ifdef ARDUINO_PICO_VERSION_STR
  return ARDUINO_PICO_VERSION_STR;
#else
  return "--";
#endif
}
const char *sdkVersion() {
#ifdef PICO_SDK_VERSION_STRING
  return PICO_SDK_VERSION_STRING;
#else
  return "--";
#endif
}
const char *boardName() {
#ifdef BOARD_NAME
  return BOARD_NAME;
#else
  return "--";
#endif
}
long flashKiB() {
#ifdef PICO_FLASH_SIZE_BYTES
  return (long)(PICO_FLASH_SIZE_BYTES / 1024);
#else
  return -1;
#endif
}
uint8_t chipRev() { return (uint8_t)((sysinfo_hw->chip_id >> 28) & 0xF); }
} // namespace

namespace VersionInfo {

void isoBuild(char *buf, size_t n) {
  static const char *M = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char *d = __DATE__;                      // "Oct 27 2026" (day space-padded)
  int month = 0;
  for (int i = 0; i < 12; i++) if (strncmp(d, M + 3 * i, 3) == 0) { month = i + 1; break; }
  const int day = (d[4] == ' ' ? 0 : (d[4] - '0') * 10) + (d[5] - '0');
  snprintf(buf, n, "%c%c%c%c-%02d-%02dT%s", d[7], d[8], d[9], d[10], month, day, __TIME__);
}

void printLine(Print &out) {
  char iso[24]; isoBuild(iso, sizeof iso);
  out.print(F("OK VER FW=")); out.print(F(FW_VERSION_ID));
  out.print(F(" PROTO=")); out.print(PROTO_VERSION);
  out.print(F(" BUILD=")); out.print(iso);
  out.print(F(" HW=v1b HWREV=")); out.print(CAL_HW_REV);
  out.print(F(" CALREV=")); out.print(CAL_ALGO_REV);
  out.print(F(" CHIP=")); out.print(chipName());
  out.print(F(" CHIPREV=")); out.print(chipRev());
  out.print(F(" MHZ=")); out.print(rp2040.f_cpu() / 1000000UL);
  out.print(F(" FLASH="));
  if (flashKiB() > 0) { out.print(flashKiB()); out.print('K'); } else out.print(F("--"));
  out.print(F(" UID=")); out.print(rp2040.getChipID());
  out.print(F(" BOARD=")); out.print(boardName());
  out.print(F(" CORE=")); out.print(coreVersion());
  out.print(F(" SDK=")); out.print(sdkVersion());
  out.print(F(" NOTE=")); out.println(F(FW_VERSION_NOTE));
}

void printReadable(Print &out) {
  char iso[24]; isoBuild(iso, sizeof iso);
  out.print(F("Firmware      ")); out.println(F(FW_VERSION_ID));
  out.print(F("  note        ")); out.println(F(FW_VERSION_NOTE));
  out.print(F("  built       ")); out.println(iso);
  out.print(F("  protocol    ")); out.println(PROTO_VERSION);
  out.print(F("Hardware      driver v1b, calibration hardware rev ")); out.print(CAL_HW_REV);
  out.print(F(", calibration algorithm rev ")); out.println(CAL_ALGO_REV);
  out.print(F("Chip          ")); out.print(chipName()); out.print(F(" rev ")); out.print(chipRev());
  out.print(F(", ")); out.print(rp2040.f_cpu() / 1000000UL); out.println(F(" MHz"));
  out.print(F("  flash       "));
  if (flashKiB() > 0) { out.print(flashKiB()); out.println(F(" KiB")); } else out.println(F("--"));
  out.print(F("  unique ID   ")); out.println(rp2040.getChipID());
  out.print(F("Board         ")); out.println(boardName());
  out.print(F("Arduino-Pico  ")); out.println(coreVersion());
  out.print(F("Pico SDK      ")); out.println(sdkVersion());
}

} // namespace VersionInfo
