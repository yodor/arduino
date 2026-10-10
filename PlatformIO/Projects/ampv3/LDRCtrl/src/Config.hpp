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
// the bench scans ("bright" = near the cell's floor resistance). Full scale is not
// needed: the NSL-32SR3 bottoms out well before it (duty ~3685 with 100k
// base bleeds, ~3150 with 1M bleeds), and beyond that extra LED current only
// heats the cell, which then reads ~7% HIGHER. 80% of WRAP sits just past the
// floor for either bleed value, as a fraction so it follows LED_PWM_WRAP.
// ---------------------------------------------------------------------------
// PWM dithering. Each LED drive duty is held in FINE units, PWM_DITHER_LEVELS per
// PWM count, and a PWM-wrap interrupt alternates the hardware level between N and
// N+1 (first-order sigma-delta) so the AVERAGE is N + k/PWM_DITHER_LEVELS. The
// pattern repeats at least every PWM_DITHER_LEVELS wraps (>= 2.3 kHz at 16 levels
// and 36.6 kHz PWM); the 10k/1uF base filter (16 Hz) leaves a few microvolts of it
// at the transistor -- well under 1% of one count -- and the cells cannot follow
// kilohertz anyway. Why it matters: with the 1M base bleeds the quiet-end knee moves
// the series cell ~25-40% PER COUNT, so whole counts alone cannot land within
// +-1 dB; 16 levels make that ~2% per fine step. Power of two (the ISR divides by
// shifting); 1 = dithering off, exactly the old behaviour.
constexpr uint32_t PWM_DITHER_LEVELS = 16;

// Which source this firmware was built from. Printed at boot, at the top of HELP and in
// DIAG, together with the compile date/time, so it is always obvious which build is
// actually running on the board. Bump it with every delivered source package.
#define FW_VERSION "2026-10-09q (health: common vs per-channel drift)"
static_assert(PWM_DITHER_LEVELS >= 1 && (PWM_DITHER_LEVELS & (PWM_DITHER_LEVELS - 1)) == 0,
              "PWM_DITHER_LEVELS must be a power of two");

// DIAG ALL verify: how long after driving a LUT step its cells are read. The cells take a
// few seconds to settle after a volume change -- an accepted property of this design --
// so verify reports what is heard once they have settled, not the transient.
constexpr uint32_t VERIFY_SETTLE_MS = 3000;

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
constexpr uint16_t CAL_HW_REV   = 2; // the board now has the 1M bleeds
// CAL_ALGO_REV -- bump when a firmware change alters what saved curves/LUT
// entries MEAN. Ordinary firmware updates do not bump it, so they keep the
// saved calibration.
constexpr uint16_t CAL_ALGO_REV = 4; // 2: LUT solved against the LOADED divider; 3: the walk measures memory; 4: memory after the real top step (150 ohm, shunt off)

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
// lag the bench scans showed). So before the relay hands the audio path back, the
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
// Rtotal (Rs+Rsh) is NOT configured: CAL measures what the cells can do and
// chooses it (AUTO), or takes the master's R= within the measured range; a saved
// calibration restores its own value.

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
// across repeated characterizations of the same cell.
constexpr float RANGE_SAFETY_MARGIN_DB = 2.0f;


// ---------------------------------------------------------------------------
// Capability model: how a full characterization turns what it MEASURED into RMIN, RMAX and the
// AUTO Rtotal. Everything about the cells comes from the calibration itself (the
// sweep plus the settled dark-end walk, DarkProfile.hpp); what is set here is
// POLICY -- how much error you are willing to accept -- not hardware data.
//
// CAPS_MIN_ZIN_OHMS     lowest acceptable input impedance at the loudest step
//                       (what the JFET buffer sees); sets the smallest sensible Rtotal.
// CAPS_*_MAX_MEMORY     the largest MEASURED memory allowed at the quiet-end series
//                       point: how far the quiet level sits off after a loud passage,
//                       once the cells have settled (the few seconds of settling
//                       themselves are an accepted property of this design), as a
//                       resistance ratio (1.12 ~ 1 dB, 1.25 ~ 2 dB).
// CAPS_*_MAX_QUANT_DB   the largest worst-case +-half-step gain error allowed at the
//                       quietest step (resolution, with dithering).
//   AUTO -- what the calibration picks by itself -- uses the tight pair; RMAX -- the
//   deepest Rtotal a master may still ASK for -- the looser pair. Neither ever goes
//   deeper than the deepest point the dark-end walk could settle on both channels.
// CAPS_DITHER_LEVELS    sub-count levels per PWM count the step-error estimates
//                       assume -- tied to PWM_DITHER_LEVELS (the real dithering).
// CAPS_LAG_*            capability report display only: points in a sparse dark tail are
//                       ignored, a first fine interval steeper than SLOPE_FACTOR x
//                       the next is FLAGGED "lag?".
constexpr float    CAPS_MIN_ZIN_OHMS      = 5000.0f;
constexpr float    CAPS_AUTO_MAX_MEMORY   = 1.12f;
constexpr float    CAPS_AUTO_MAX_QUANT_DB = 0.45f;
constexpr float    CAPS_RMAX_MAX_MEMORY   = 1.25f;
constexpr float    CAPS_RMAX_MAX_QUANT_DB = 1.0f;
// CAPS_*_MAX_SLOPE      the steepest series curve allowed at the quiet-end point, as
//                       ln(R) per PWM count (the capability table's "step %/ct" / 100).
//                       A driver drifts by millivolts with temperature (the board warms
//                       during a calibration and cools after it); the cell then moves by
//                       roughly slope x the drift in counts, and the slope rises steeply
//                       with depth. Bench, 1M bleeds: ~0.28/count (95k) -> R within
//                       +-1.5 dB; ~0.45 (344k) -> +-3 dB; ~0.56 (552k) -> +-5-6 dB, with
//                       the quick loud-passage test (memory) showing nothing.
constexpr float    CAPS_AUTO_MAX_SLOPE    = 0.30f;
// The top (transparent) step's series resistance, the SAME on both channels: stereo
// matching first. 150 ohm costs ~0.08 dB against a series at full brightness (~40-55 ohm)
// but needs several times less LED current -- and a fully bright series cell recovers
// slowly after max volume (bench: R ~2 dB quieter than L for over a minute). The dark-end
// walk's memory test uses this same state, so AUTO accounts for what max volume costs.
constexpr float    TRANSPARENT_SERIES_OHMS = 150.0f;
constexpr float    CAPS_RMAX_MAX_SLOPE    = 0.45f;
constexpr float    CAPS_DITHER_LEVELS     = (float)PWM_DITHER_LEVELS; // follows the real dithering
constexpr float    CAPS_LAG_SLOPE_FACTOR  = 1.6f;
constexpr uint16_t CAPS_LAG_MAX_GAP       = 10;

// Strict master<->daughter protocol revision, reported as PROTO= in STATUS so the master
// can detect an incompatible daughter board (daughter-board-uart-protocol-v2.md; the file
// keeps its v2 name). Revision 7: a single CAL [R=<ohms>|R=AUTO] -- the daughter checks the
// cells against the stored calibration and decides how much work it needs -- and STATUS
// with LASTCAL=.
constexpr uint8_t PROTO_VERSION = 7;
