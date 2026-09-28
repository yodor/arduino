// ============================================================================
// Driver-board bring-up test firmware
//
// Purpose: exercise the 4 LED driver channels (SHUNT_L, SER_L, SHUNT_R,
// SER_R) directly from a USB serial terminal, BEFORE the LDR board, relay,
// or ADS1115 are connected. Lets you set a raw PWM duty on any channel and
// read the resulting LED current on a multimeter (put the meter in series
// with the LED, or read voltage across a known sense resistor if you've
// added one temporarily).
//
// This is deliberately NOT the final protocol firmware — no UART0 link to
// the master board, no calibration, no ADS1115. Just direct duty control so
// you can sweep each channel's real duty-vs-current curve and confirm the
// driver behaves the way the schematic analysis predicts before wiring in
// anything else.
//
// Hardware: RP2350 (rpipico2), Arduino-Pico (Philhower) core, PlatformIO.
// PWM is driven directly via the Pico SDK's hardware/pwm.h rather than
// analogWrite(), so we have explicit control of wrap value and clock
// divider (needed for the ~36.6kHz / 12-bit setup discussed earlier).
// ============================================================================

#include <Arduino.h>
#include "hardware/pwm.h"
#include "hardware/clocks.h"

// ---------------------------------------------------------------------------
// CONFIG — edit these to match your actual PCB pin assignment.
// These are placeholders; the driver board's real GPIO map isn't finalized
// yet, so double-check against your schematic / Pins.hpp equivalent before
// wiring anything up. Any 4 PWM-capable GPIOs work; it's fine if two of them
// happen to share a PWM slice (see note below), since we want the same
// frequency on every channel anyway.
// ---------------------------------------------------------------------------
static const uint8_t PIN_SHUNT_L = 2;
static const uint8_t PIN_SER_L   = 3;
static const uint8_t PIN_SHUNT_R = 4;
static const uint8_t PIN_SER_R   = 5;

static const uint8_t NUM_CH = 4;
static const uint8_t CH_PINS[NUM_CH] = {PIN_SHUNT_L, PIN_SER_L, PIN_SHUNT_R, PIN_SER_R};
static const char *CH_NAMES[NUM_CH] = {"SHUNT_L", "SER_L", "SHUNT_R", "SER_R"};

// 12-bit PWM (wrap = 4095) at ~36.6kHz with a 150MHz system clock, matching
// the earlier ripple/resolution discussion. If your board clocks differently
// check clock_get_hz(clk_sys) at boot (printed below) and adjust PWM_DIV.
static const uint16_t PWM_WRAP = 4095;
static const float PWM_DIV = 1.0f; // 150MHz / (4096 * 1.0) ≈ 36.6kHz

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static uint16_t currentDuty[NUM_CH] = {0, 0, 0, 0};

// ---------------------------------------------------------------------------
// PWM setup
// ---------------------------------------------------------------------------
void setupPwmChannel(uint8_t pin) {
  gpio_set_function(pin, GPIO_FUNC_PWM);
  uint slice = pwm_gpio_to_slice_num(pin);
  pwm_set_wrap(slice, PWM_WRAP);
  pwm_set_clkdiv(slice, PWM_DIV);
  pwm_set_gpio_level(pin, 0); // start dark
  pwm_set_enabled(slice, true);
}

void setDuty(uint8_t ch, uint16_t duty) {
  if (ch >= NUM_CH) return;
  if (duty > PWM_WRAP) duty = PWM_WRAP;
  currentDuty[ch] = duty;
  pwm_set_gpio_level(CH_PINS[ch], duty);
}

void setAll(uint16_t duty) {
  for (uint8_t i = 0; i < NUM_CH; i++) setDuty(i, duty);
}

int findChannel(const String &name) {
  for (uint8_t i = 0; i < NUM_CH; i++) {
    if (name.equalsIgnoreCase(CH_NAMES[i])) return i;
  }
  // also accept plain channel index 0-3
  if (name.length() == 1 && isDigit(name[0])) {
    int idx = name.toInt();
    if (idx >= 0 && idx < NUM_CH) return idx;
  }
  return -1;
}

// ---------------------------------------------------------------------------
// Command handling
// ---------------------------------------------------------------------------
void printHelp() {
  Serial.println(F("--------------------------------------------------------"));
  Serial.println(F("Driver bring-up test commands:"));
  Serial.println(F("  SET <ch> <duty>      set raw duty (0-4095) on one channel"));
  Serial.println(F("  PCT <ch> <percent>   set duty by percent (0-100) on one channel"));
  Serial.println(F("  ALL <duty>           set raw duty on all 4 channels"));
  Serial.println(F("  ALLPCT <percent>     set duty by percent on all 4 channels"));
  Serial.println(F("  SWEEP <ch> <start%> <end%> <step%> <dwell_ms>"));
  Serial.println(F("                       step duty, pausing dwell_ms at each point"));
  Serial.println(F("                       so you can read the multimeter"));
  Serial.println(F("  STATUS               print current duty of all channels"));
  Serial.println(F("  OFF                  set all channels to 0"));
  Serial.println(F("  HELP                 show this message"));
  Serial.println(F(""));
  Serial.println(F("<ch> is SHUNT_L, SER_L, SHUNT_R, SER_R, or 0-3."));
  Serial.println(F("Example: PCT SHUNT_L 50   -> 50% duty on the SHUNT_L channel"));
  Serial.println(F("Example: SWEEP SER_L 0 100 5 1000"));
  Serial.println(F("         -> sweeps SER_L from 0% to 100% in 5% steps,"));
  Serial.println(F("            holding 1000ms at each step"));
  Serial.println(F("--------------------------------------------------------"));
}

void printStatus() {
  for (uint8_t i = 0; i < NUM_CH; i++) {
    float pct = (currentDuty[i] * 100.0f) / PWM_WRAP;
    Serial.print(CH_NAMES[i]);
    Serial.print(F(" (GPIO"));
    Serial.print(CH_PINS[i]);
    Serial.print(F("): duty="));
    Serial.print(currentDuty[i]);
    Serial.print(F("/"));
    Serial.print(PWM_WRAP);
    Serial.print(F(" ("));
    Serial.print(pct, 1);
    Serial.println(F("%)"));
  }
}

void doSweep(uint8_t ch, int startPct, int endPct, int stepPct, unsigned long dwellMs) {
  if (stepPct == 0) {
    Serial.println(F("ERR step must be nonzero"));
    return;
  }
  Serial.print(F("Sweeping "));
  Serial.print(CH_NAMES[ch]);
  Serial.print(F(" from "));
  Serial.print(startPct);
  Serial.print(F("% to "));
  Serial.print(endPct);
  Serial.print(F("%, step "));
  Serial.print(stepPct);
  Serial.println(F("%. Send any character to abort early."));

  bool increasing = endPct >= startPct;
  int step = increasing ? abs(stepPct) : -abs(stepPct);

  for (int pct = startPct;
       increasing ? (pct <= endPct) : (pct >= endPct);
       pct += step) {

    uint16_t duty = (uint16_t)((pct * (long)PWM_WRAP) / 100);
    setDuty(ch, duty);

    Serial.print(F("  duty="));
    Serial.print(pct);
    Serial.print(F("%  (raw="));
    Serial.print(duty);
    Serial.println(F(")  <- read multimeter now"));

    unsigned long start = millis();
    while (millis() - start < dwellMs) {
      if (Serial.available()) {
        Serial.println(F("Sweep aborted."));
        return;
      }
    }
  }
  Serial.println(F("Sweep complete."));
}

void handleLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  // tokenize on spaces
  const int MAX_TOK = 6;
  String tok[MAX_TOK];
  int n = 0;
  int start = 0;
  while (n < MAX_TOK) {
    int sp = line.indexOf(' ', start);
    if (sp < 0) {
      tok[n++] = line.substring(start);
      break;
    }
    tok[n++] = line.substring(start, sp);
    start = sp + 1;
    while (start < (int)line.length() && line[start] == ' ') start++; // skip extra spaces
  }

  String cmd = tok[0];
  cmd.toUpperCase();

  if (cmd == "HELP" || cmd == "?") {
    printHelp();

  } else if (cmd == "STATUS") {
    printStatus();

  } else if (cmd == "OFF") {
    setAll(0);
    Serial.println(F("OK all channels off"));

  } else if (cmd == "SET" && n >= 3) {
    int ch = findChannel(tok[1]);
    if (ch < 0) { Serial.println(F("ERR unknown channel")); return; }
    int duty = tok[2].toInt();
    setDuty(ch, duty);
    Serial.println(F("OK"));

  } else if (cmd == "PCT" && n >= 3) {
    int ch = findChannel(tok[1]);
    if (ch < 0) { Serial.println(F("ERR unknown channel")); return; }
    int pct = constrain(tok[2].toInt(), 0, 100);
    uint16_t duty = (uint16_t)((pct * (long)PWM_WRAP) / 100);
    setDuty(ch, duty);
    Serial.println(F("OK"));

  } else if (cmd == "ALL" && n >= 2) {
    int duty = tok[1].toInt();
    setAll(duty);
    Serial.println(F("OK"));

  } else if (cmd == "ALLPCT" && n >= 2) {
    int pct = constrain(tok[1].toInt(), 0, 100);
    uint16_t duty = (uint16_t)((pct * (long)PWM_WRAP) / 100);
    setAll(duty);
    Serial.println(F("OK"));

  } else if (cmd == "SWEEP" && n >= 6) {
    int ch = findChannel(tok[1]);
    if (ch < 0) { Serial.println(F("ERR unknown channel")); return; }
    int startPct = tok[2].toInt();
    int endPct   = tok[3].toInt();
    int stepPct  = tok[4].toInt();
    unsigned long dwell = (unsigned long)tok[5].toInt();
    doSweep(ch, startPct, endPct, stepPct, dwell);

  } else {
    Serial.println(F("ERR unrecognized command, try HELP"));
  }
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  // Wait briefly for USB CDC to enumerate; don't block forever if nothing's
  // attached (e.g. running on battery/bench supply with no PC).
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) { delay(10); }

  for (uint8_t i = 0; i < NUM_CH; i++) {
    setupPwmChannel(CH_PINS[i]);
  }

  Serial.println();
  Serial.println(F("=== LDR driver board bring-up test ==="));
  Serial.print(F("System clock: "));
  Serial.print(clock_get_hz(clk_sys) / 1000000);
  Serial.println(F(" MHz"));
  Serial.print(F("PWM: wrap="));
  Serial.print(PWM_WRAP);
  Serial.print(F(", clkdiv="));
  Serial.print(PWM_DIV, 2);
  Serial.print(F(" -> ~"));
  Serial.print((clock_get_hz(clk_sys) / PWM_DIV) / (PWM_WRAP + 1));
  Serial.println(F(" Hz PWM frequency"));
  Serial.println(F("All channels start OFF (duty=0)."));
  printHelp();
}

void loop() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (line.length() > 0) {
        Serial.println(); // move to a fresh line before the response
        handleLine(line);
        line = "";
      }
      // else: ignore a lone/duplicate terminator (handles CR, LF, or CRLF)
    } else {
      Serial.write(c); // local echo so you can see what you're typing
      line += c;
    }
  }
}