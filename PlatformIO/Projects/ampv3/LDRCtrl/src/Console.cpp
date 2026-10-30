#include "Console.hpp"
#include <stdio.h>
#include <math.h>
#include "ConsoleNum.hpp"

Console usb(Serial);

void Console::emit(const char *tag, const char *fmt, va_list ap) {
  char buf[320];
  int n = 0;
  if (tag && *tag) n = snprintf(buf, sizeof buf, "[%s] ", tag);
  if (n < 0) n = 0;
  if ((size_t)n >= sizeof buf) n = sizeof buf - 1;
  vsnprintf(buf + n, sizeof buf - (size_t)n, fmt, ap);
  size_t len = strlen(buf);
  while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';   // the line ending is ours
  out_.write((const uint8_t *)buf, len);
  out_.write("\r\n", 2);
}

void Console::line(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); emit(nullptr, fmt, ap); va_end(ap);
}

void Console::log(const char *tag, const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); emit(tag, fmt, ap); va_end(ap);
}

Console::Num Console::num(float value, int decimals) {
  Num r;
  ConsoleNum::format(r.s, sizeof r.s, value, decimals);
  return r;
}
