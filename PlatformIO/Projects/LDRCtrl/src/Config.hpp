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
// SAME current driver stage and its existing 10k/1uF passive RC filter,
// before the planned op-amp current-source redesign -- which removes PWM
// edges from ever reaching the LED at all, making high resolution safe
// regardless of frequency, independently of what this test finds.
constexpr uint16_t LED_PWM_WRAP = 4095;
constexpr float LED_PWM_CLKDIV = 1.0f; // fastest achievable divider at this WRAP (cannot go below 1.0)

// Level the NON-swept element is held at during calibration sweeps and
// CALSCAN ("bright" = near the cell's floor resistance). Full scale is not
// needed: the NSL-32SR3 bottoms out well before it (duty ~3685 with 100k
// base bleeds, ~3150 with 1M bleeds), and beyond that extra LED current only
// heats the cell, which then reads ~7% HIGHER. 80% of WRAP sits just past the
// floor for either bleed value, as a fraction so it follows LED_PWM_WRAP.
constexpr uint16_t CAL_BRIGHT_HOLD_DUTY = (uint16_t)(((uint32_t)LED_PWM_WRAP * 80u) / 100u);

// ---------------------------------------------------------------------------
// Audio-path impedances the LUT solve accounts for (see DividerMath.hpp). The
// divider is not unloaded: the amp board's input is a 10k resistor to the
// op-amp's virtual ground (measured), and the JFET buffer in front has a ~400R
// output impedance. Solving against the real loaded gain makes each step's dB --
// and STATUS DB= -- the true one; with the old unloaded solve the loud steps were
// up to ~7 dB quieter than labelled. Set AMP_INPUT_LOAD_OHMS to 0 to solve
// unloaded again (and bump CAL_ALGO_REV: the meaning of saved LUTs changes).
// AUDIO_SOURCE_OHMS is approximate; its effect is <= ~0.4 dB, at the loud end only.
constexpr float AMP_INPUT_LOAD_OHMS = 10000.0f;
constexpr float AUDIO_SOURCE_OHMS   = 400.0f;

// ---------------------------------------------------------------------------
// What a saved calibration is stamped with (see CalStamp.hpp). A file whose
// stamps differ from these is REFUSED at load and the board recalibrates from
// scratch -- audio path disconnected, amp muted -- instead of playing through a
// LUT taken on different hardware.
//
// CAL_HW_REV -- bump it IN THE SAME CHANGE as any hardware modification that
// alters the duty -> resistance relationship:
//   1 = original driver: 100k PNP base bleeds (R8/R9/R16/R17), 68k NPN
//       emitters, 120R PNP emitters, 10k + 1uF PWM filters
//   2 = bleeds R8/R9/R16/R17 changed to 1M
// A PARTIAL swap (some bleeds 1M, some still 100k) is its own revision: bump for
// it, and again when the last one is done. Revisions are free.
// IMPORTANT: the revision must be bumped in the firmware that FIRST runs on the
// modified board. Change the hardware with the power off, then flash by holding
// BOOTSEL (which never runs the old firmware); if you flash over a running
// board the old firmware boots on the new hardware first and loads the stale
// file. Never flash a bumped revision onto the UNmodified board and let it
// calibrate -- the file would carry the new stamp for the old hardware.
constexpr uint16_t CAL_HW_REV   = 1;
// CAL_ALGO_REV -- bump when a firmware change alters what saved curves/LUT
// entries MEAN. Ordinary firmware updates do not bump it, so they keep the
// saved calibration.
constexpr uint16_t CAL_ALGO_REV = 2; // 2: the LUT is solved against the LOADED divider (see AMP_INPUT_LOAD_OHMS)

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

// After a calibration sweep or trim pass the LED cells are left at whatever
// the last probe drove them to -- usually bright (low resistance). The cell
// brightens in milliseconds but relaxes toward dark over seconds (the same
// lag CALSCAN shows). So before the relay hands the audio path back, the
// firmware puts each channel on its real operating point and WAITS this
// long with the relay still energized (audio disconnected), letting the
// bulk of that relaxation finish out of earshot. Without it the first
// seconds after every CAL would be a loud network decaying toward the
// intended volume.
constexpr uint32_t CAL_RELAX_BEFORE_RECONNECT_MS = 1500;

// The trim measures live resistance, and a cell that has just come down from
// bright reads LOW while it is still relaxing (it was measured: R's series
// trimmed 3 duty counts lower when the trim began straight after a sweep,
// from fully bright, than when it began from a cell resting near step 0 --
// about 2.5 dB of quiet-end level on the slower channel). So every trim
// first puts the channel on its own step-0 operating point and waits this
// long; the cells then start from (nearly) rest, the way they will actually
// be used. The bulk of the relaxation is in the first seconds; the slow
// tail beyond this is a few percent.
constexpr uint32_t TRIM_PRECONDITION_MS = 8000;

// ---------------------------------------------------------------------------
// Calibration / volume LUT
// ---------------------------------------------------------------------------
// Constant Rs+Rsh target the calibration solve step aims for, in
// CONSTANT_RTOTAL mode (keeps the JFET buffer's ~400R source impedance
// seeing a fixed load at every step). 50k is the final operating point:
// 31 of 32 steps solved with ~54 dB of depth, at the cost of a wider
// ascending/descending hysteresis band than the lower choices (see
// CAL_R_CHOICES below). These are only BOOT-TIME DEFAULTS --
// CALRTOTAL <L|R> [ohms] (or CAL FULL ... R=<n>) overrides per-channel
// at runtime, and a saved calibration restores its own value from flash.
constexpr float RTOTAL_LEFT_OHMS = 50000.0f;
constexpr float RTOTAL_RIGHT_OHMS = 50000.0f;

// Actual Rref (R2 on the LDR Board sheet), from direct DMM measurement rather than the nominal
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

// The total dB span the LUT covers (quietest step to the notional 0 dB top)
// is NOT configurable: it is always computed from the measured cell floors
// and Rtotal at every solve (LDRVolume::computeMaxRangeDb), and the two
// channels always share the smaller of their two ranges so L and R have
// identical dB at every step. There used to be per-channel fixed values
// here plus a CALRANGE override; a fixed range could only ever ask for
// targets below the cell's floor or leave depth on the table, and two
// independently chosen ranges is exactly how L and R ended up 3 dB apart.
// True silence is always available via MUTE regardless.

// Safety margin subtracted from the computed max range, so the
// quietest/loudest steps don't sit exactly at the measured floor -- floors have shown a few percent run-to-run wobble
// across repeated CALAUTO passes on the same cell.
constexpr float RANGE_SAFETY_MARGIN_DB = 2.0f;

// The ONLY values the master may request as "R" in `CAL FULL R=<ohms>`: the
// Rtotal (Rs+Rsh) the LUT is solved for. A fixed list on purpose -- the master offers a menu, not a
// free-text field, and nothing outside it has been exercised on real cells
// (100000 in particular sits at the limit of what the cells can resolve).
constexpr long CAL_R_CHOICES[] = {5000, 10000, 25000, 50000, 100000};

// ---------------------------------------------------------------------------
// Capability model (CALCAPS now; the CAL FULL auto-Rtotal choice later). All of
// these are TUNABLES to be settled from measurements, and the priors below must
// be re-measured whenever CAL_HW_REV changes.
//
// CAPS_MIN_ZIN_OHMS     lowest acceptable input impedance at the loudest step
//                       (what the JFET buffer sees); sets the smallest sensible Rtotal.
// CAPS_AUTO_MAX_HYST    AUTO may not pick an Rtotal whose cell asc/desc ratio
//                       (hysteresis prior, below) exceeds this.
// CAPS_AUTO_MAX_QUANT_DB AUTO may not pick an Rtotal whose worst-case +-half-step
//                       gain error at the quietest step exceeds this.
// CAPS_DITHER_LEVELS    sub-count levels per PWM count. 1 = no dithering; set it
//                       to the real number once dithering exists and every
//                       step-error estimate shrinks accordingly.
// CAPS_LAG_*            a sweep point is "lag-suspect" (the cell had not
//                       relaxed yet) if it sits in a sparse tail (gap above
//                       MAX_GAP counts) or its per-count slope exceeds
//                       SLOPE_FACTOR x the median slope of the next intervals.
// CAPS_HYST_PRIOR_*     asc/desc resistance ratio vs Rtotal, measured with
//                       CALSCAN on the original 100k-bleed driver. Beyond the
//                       last entry the model EXTRAPOLATES (and says so).
constexpr float    CAPS_MIN_ZIN_OHMS      = 5000.0f;
constexpr float    CAPS_AUTO_MAX_HYST     = 1.5f;
constexpr float    CAPS_AUTO_MAX_QUANT_DB = 0.45f;
constexpr float    CAPS_DITHER_LEVELS     = 1.0f;
constexpr float    CAPS_LAG_SLOPE_FACTOR  = 1.6f;
constexpr uint16_t CAPS_LAG_MAX_GAP       = 10;
constexpr float    CAPS_HYST_PRIOR_R[]     = {10000.0f, 30000.0f, 50000.0f, 100000.0f};
constexpr float    CAPS_HYST_PRIOR_RATIO[] = {1.04f, 1.17f, 1.27f, 1.52f};
constexpr uint8_t  CAPS_HYST_PRIOR_COUNT   = sizeof(CAPS_HYST_PRIOR_R) / sizeof(CAPS_HYST_PRIOR_R[0]);
constexpr uint8_t CAL_R_CHOICE_COUNT = sizeof(CAL_R_CHOICES) / sizeof(CAL_R_CHOICES[0]);

// Strict master<->daughter protocol revision, reported as PROTO= in STATUS so
// the master can detect an incompatible daughter board. History: 2 = 1-based
// VOL; 3 = 0-based VOL and the reordered STATUS line with TEMP/UP; 4 = the
// master's CAL commands are exactly CAL FAST and CAL FULL [R=..],
// always answered with a status line (BUSY if started, IDLE + CAL=ERR + a
// trailing ERR=<reason> if not -- no CAL DONE/FAIL, poll STATUS), STATUS is the
// only getter (every GET command is gone, GET STATUS included), STATUS reports R
// instead of RTOTAL, and CAL is OK / NONE / ERR (no PARTIAL); 5 = the FIXEDSERIES
// attenuation mode is gone: STATUS has no MODE= field and CAL FULL takes only an
// optional R=<ohms> (no MODE=). (The document file keeps its v2 name.)
constexpr uint8_t PROTO_VERSION = 5;