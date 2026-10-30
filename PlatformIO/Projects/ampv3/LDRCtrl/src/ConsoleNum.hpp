#pragma once
// Float -> text exactly as Arduino's Print::printFloat writes it (the same algorithm), for
// Console::num(). Separate from Console so the host tests can check it against the original.
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace ConsoleNum {
inline void format(char *out, size_t cap, double number, int decimals) {
  if (cap == 0) return;
  out[0] = '\0';
  if (decimals < 0) decimals = 2;
  if (decimals > 8) decimals = 8;
  if (isnan(number)) { snprintf(out, cap, "nan"); return; }
  if (isinf(number)) { snprintf(out, cap, "inf"); return; }
  if (number > 4294967040.0 || number < -4294967040.0) { snprintf(out, cap, "ovf"); return; }
  size_t k = 0;
  if (number < 0.0) { out[k++] = '-'; number = -number; }
  double rounding = 0.5;
  for (int i = 0; i < decimals; i++) rounding /= 10.0;
  number += rounding;
  const unsigned long whole = (unsigned long)number;
  double rem = number - (double)whole;
  k += (size_t)snprintf(out + k, cap - k, "%lu", whole);
  if (decimals > 0 && k + 1 < cap) out[k++] = '.';
  for (int i = 0; i < decimals && k + 1 < cap; i++) {
    rem *= 10.0;
    const unsigned int d = (unsigned int)rem;       // printed like print(unsigned): exact even if d > 9
    k += (size_t)snprintf(out + k, cap - k, "%u", d);
    rem -= d;
  }
  out[k < cap ? k : cap - 1] = '\0';
}
} // namespace ConsoleNum
