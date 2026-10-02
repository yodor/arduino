#pragma once
#include <Arduino.h>

// 4N25 opto-isolated mute control on MUTE_PIN. Verified against the actual
// mute circuit: at boot (GPIO Hi-Z), current flows through the divider
// (3.3V -> 300R -> opto LED -> junction -> 1k -> GND) with the LED on,
// engaging mute purely passively before firmware ever runs. GPIO driven
// LOW keeps sinking that same current (still muted); GPIO driven HIGH
// brings the junction to 3.3V, killing the voltage across the LED (off,
// unmuted). So LOW = muted, HIGH = unmuted -- active-low.
constexpr bool MUTE_ACTIVE_HIGH = false;

// How long to mandatorily hold mute after boot, protecting speakers/amp
// while coupling caps and other circuits settle. This is a hard safety
// floor: ALL mute commands, including user-issued ones (button/IR), are
// refused outright for this entire duration -- not deferred, not
// cancelable by any explicit call. Only expires by elapsed time.
constexpr uint32_t MUTE_BOOT_HOLD_MS = 5000;

// UART board link (pins in Pins.hpp).
constexpr uint32_t UART_BAUD = 115200;


constexpr bool CUSTOM_CLOCKS_ENABLED = true;
//constexpr uint32_t SPI_SPEED_HZ = 50000000;
constexpr uint32_t CPU_SPEED_KHZ = 150000; // was 200000; back off until burned-in stable at 200

// LED driver PWM resolution and carrier frequency -- the two are coupled,
// not independent: frequency = (CPU_SPEED_KHZ*1000) / (WRAP+1) / CLKDIV,
// and CLKDIV's minimum is 1.0 (the fastest achievable), so raising
// resolution (WRAP) necessarily lowers frequency and vice versa. Change
// LED_PWM_WRAP here to test different points on that tradeoff on the
// bench -- then verify the ACTUAL resulting frequency in the boot banner
// (Board::bootInfo() prints it via DriverChannels::frequencyHz()) rather
// than computing it by hand; integer rounding means not every WRAP value
// divides CPU_SPEED_KHZ as cleanly as the two previously-tuned values did
// (1023 -> 146484Hz, 749 -> exactly 200000Hz).
//
// History: 4095 (12-bit, ~36.6kHz) was the ORIGINAL value -- moved to
// 1023 then 749 specifically because that frequency coupled audible
// noise directly into the current BJT-driven LED stage (confirmed: noise
// present at every intermediate duty, completely absent at LDR mute()'s
// static 0%/100% extremes -- an edge-switching signature, not a current-
// level one). Restoring 4095 now to test full resolution against this
// SAME current driver stage and its existing 10k/10nF passive RC filter,
// before the planned op-amp current-source redesign -- which removes PWM
// edges from ever reaching the LED at all, making high resolution safe
// regardless of frequency, independently of what this test finds.
constexpr uint16_t LED_PWM_WRAP = 4095;
constexpr float LED_PWM_CLKDIV = 1.0f; // fastest achievable divider at this WRAP (cannot go below 1.0)

// How long to force the amp muted around a calibration relay's energize/
// de-energize transition, before restoring whatever mute state was in
// effect beforehand. Covers the JFET buffer's DC-bias pop the relay
// contacts otherwise couple through.
constexpr uint32_t RELAY_POP_SETTLE_MS = 100;

// Delay between intermediate steps when a VOL request jumps more than one
// step at once (e.g. VOL 20 -> VOL 1 in a single command) -- confirmed on
// real hardware that applying such a jump in one shot produces an audible
// pop. This is a balance between two opposite failure modes: too long a
// delay (confirmed: 8ms was) makes each individual step audibly
// perceptible, producing a "zipper"/sliding sound rather than a clean
// transition; too short (effectively 0) reproduces the original pop. The
// fix for the former is a MUCH faster traversal, not a slower one -- we
// can't make each step's gain change smaller (32 is the LUT's fixed
// granularity), so speed is the only lever. Worst case is a full 31-step
// traversal (step 31 to step 0): at this delay that's 30 * 2ms = 60ms,
// still far under the strict protocol's 500ms DAUGHTER_RESPONSE_TIMEOUT_MS.
// Tune this to taste -- lower if any zipper sound remains audible, raise
// slightly if the pop reappears at this speed.
constexpr uint32_t VOL_RAMP_STEP_DELAY_MS = 2;

// ---------------------------------------------------------------------------
// Calibration / volume LUT
// ---------------------------------------------------------------------------
// Constant Rs+Rsh target the calibration solve step aims for, in
// CONSTANT_RTOTAL mode (keeps the JFET buffer's ~400R source impedance
// seeing a fixed load at every step). 2000R chosen from CALSCAN data:
// keeps every target resistance within the low-hysteresis region (<3%
// spread) rather than the ~10-65% spread seen above ~3kOhm on this cell.
// These are only BOOT-TIME DEFAULTS -- CALRTOTAL <L|R> [ohms] overrides
// per-channel at runtime without recompiling.
constexpr float RTOTAL_LEFT_OHMS = 10000.0f;
constexpr float RTOTAL_RIGHT_OHMS = 10000.0f;

// Actual Rref (R27), from direct DMM measurement rather than the nominal
// 10k 1% value. Both connectors measured ~9.99k (small reading jitter
// between 9.99k/10.0k is DMM last-digit noise at this resistance, not a
// real difference between the two boards) -- use CALREF <L|R> <ohms> to
// override at runtime if a future board swap actually needs a different
// value.
constexpr float RREF_LEFT_OHMS = 9990.0f;
constexpr float RREF_RIGHT_OHMS = 9990.0f;

// Default number of volume steps and total attenuation range (dB) the LUT
// solve spans, log/geometric across steps. Real achievable range depends
// on each board's actual measured LDR floor -- these are starting points,
// not hard limits; solve() takes them as parameters so they're easy to
// retune per-board once real curves exist.
constexpr uint8_t  NUM_VOLUME_STEPS_DEFAULT = 32;

// Total dB span the LUT solve covers, quietest to loudest. 47dB was never
// a deliberate spec -- it fell out of Rsh_floor/RTOTAL back when RTOTAL
// was 10k (~46R floor / 10000R). With RTOTAL now much lower, that range
// asks for quiet-step targets below the cell's actual floor (see project
// notes). 24dB is a safer starting point at RTOTAL~2000 -- keeps every
// step's targets within the low-hysteresis region CALSCAN identified.
// True silence is always available via MUTE regardless of this range.
// These are BOOT-TIME DEFAULTS -- CALRANGE <L|R> [db] overrides per-
// channel at runtime without recompiling.
constexpr float ATTEN_RANGE_LEFT_DB = 24.0f;
constexpr float ATTEN_RANGE_RIGHT_DB = 24.0f;

// Safety margin subtracted from the auto-computed max range (CALRANGE
// <L|R> AUTO), so the quietest/loudest steps don't sit exactly at the
// measured floor -- floors have shown a few percent run-to-run wobble
// across repeated CALAUTO passes on the same cell.
constexpr float RANGE_SAFETY_MARGIN_DB = 2.0f;

// CAL INIT (MasterLink) reports each of these as a candidate RTOTAL,
// alongside the achievable dB range (worst of the two channels) that
// computeMaxRangeDb() predicts for it from the just-characterized
// floors -- a fixed, easy-to-tweak list rather than anything computed.
constexpr float CAL_INIT_RTOTAL_CANDIDATES[] = {2000.0f, 5000.0f, 10000.0f};
constexpr uint8_t CAL_INIT_RTOTAL_CANDIDATE_COUNT = 3;