// Host test: console num. Run all with: python3 test/run_tests.py
// Console::num(x, d) must write floats exactly like Arduino's print(x, d), so that converting
// out.print(x, d) to a printf-style line with Console::num changes no output at all.
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <string>
#include "ConsoleNum.hpp"
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } }while(0)

// Arduino's Print::printFloat (ArduinoCore-API/api/Print.cpp), with print() writing into a string.
static std::string arduinoPrintFloat(double number, int digits) {
  std::string s; char t[32];
  auto print = [&](const char *x){ s += x; };
  auto printU = [&](unsigned long v){ snprintf(t, sizeof t, "%lu", v); s += t; };
  if (digits < 0) digits = 2;
  if (std::isnan(number)) { print("nan"); return s; }
  if (std::isinf(number)) { print("inf"); return s; }
  if (number > 4294967040.0) { print("ovf"); return s; }
  if (number < -4294967040.0) { print("ovf"); return s; }
  if (number < 0.0) { print("-"); number = -number; }
  double rounding = 0.5;
  for (uint8_t i = 0; i < digits; ++i) rounding /= 10.0;
  number += rounding;
  unsigned long int_part = (unsigned long)number;
  double remainder = number - (double)int_part;
  printU(int_part);
  if (digits > 0) print(".");
  while (digits-- > 0) { remainder *= 10.0; unsigned int toPrint = (unsigned int)remainder; printU(toPrint); remainder -= toPrint; }
  return s;
}

int main(){
  int n = 0, bad = 0;
  auto check = [&](float v, int d){
    char out[24]; ConsoleNum::format(out, sizeof out, v, d);
    const std::string ref = arduinoPrintFloat(v, d);
    n++; if (ref != out) { if (bad < 5) printf("  mismatch: %.9g, %d -> ours '%s', Arduino '%s'\n", v, d, out, ref.c_str()); bad++; }
  };
  const float special[] = {0.0f, -0.0f, 0.004f, -0.004f, 0.005f, -0.005f, 0.995f, 9.995f, -9.995f, 1.0f, -1.0f, 3.3346f,
                           1e-9f, -1e-9f, 4294967040.0f, -4294967040.0f, 5e9f, -5e9f, NAN, INFINITY, -INFINITY, 1431000.0f, 2698000.0f};
  for (float v : special) for (int d = 0; d <= 4; d++) check(v, d);
  srand(7);
  for (int i = 0; i < 200000; i++) {
    const double mag = pow(10.0, (rand() % 13) - 5);              // 1e-5 .. 1e7
    const float v = (float)(((double)rand() / RAND_MAX * 2.0 - 1.0) * mag);
    check(v, rand() % 5);
  }
  printf("  %d values compared with Arduino's printFloat, %d mismatches\n", n, bad);
  CHECK(bad == 0);
  printf("console num host test: PASS\n"); return 0; }
