#pragma once
// Console: all text output to one stream (USB Serial, or the UART to the controller), with
// printf-style helpers -- modelled on the controller board's Console.
//
//   usb.line("Board: %s", BOARD_NAME);                    -> "Board: rpipicow\r\n"
//   usb.log("sensor L", "top junction on %s", "A3");      -> "[sensor L] top junction on A3\r\n"
//   usb.line("block %s C", Console::num(t, 2).s);         floats via num(): this core's printf has none
//   usb.blank();                                          an empty line
//
// A Console is also an Arduino Print, so code written against Print (out.print(...)) keeps
// working when handed a Console. Every line() / log() ends in \r\n, like println().
#include <Arduino.h>
#include <stdarg.h>

class Console : public Print {
public:
  explicit Console(Print &out) : out_(out) {}

  void line(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  void log(const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
  void blank() { out_.write("\r\n", 2); }

  // A float as text, exactly as Arduino's print(value, decimals) writes it ("nan", "inf",
  // "ovf", and "-0.00" for a small negative value), so converted output stays identical.
  // (Capabilities::fmtFixed is the table-column variant: no "-0.00".)
  struct Num { char s[24]; };
  static Num num(float value, int decimals);

  // Print: pass-through, so a Console can stand in wherever a Print is expected.
  size_t write(uint8_t c) override { return out_.write(c); }
  size_t write(const uint8_t *buf, size_t n) override { return out_.write(buf, n); }
  using Print::write;

  Print &raw() { return out_; }

private:
  void emit(const char *tag, const char *fmt, va_list ap);
  Print &out_;
};

// USB Serial: the boot information and background messages. (Both ports -- USB and the UART on
// the pins -- also carry the protocol and console modes; see SerialLink.)
extern Console usb;
