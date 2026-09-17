#pragma once
#include <Arduino.h>
#include <Pins.hpp>
#include <IRCodes.hpp>





// Motor PWM duty cycle (0-255) and speed-step size for increase/decrease.
// 255 is full speed; for a volume-pot motor a lower starting point often
// makes fine adjustments easier to land -- tune to your actual mechanism
// (gear ratio, pot rotation range) rather than assuming full speed is right.
constexpr uint8_t MOTOR_MIN_SPEED     = 50;
constexpr uint8_t MOTOR_MAX_SPEED     = 255;
constexpr uint8_t MOTOR_DEFAULT_SPEED = 150;
constexpr uint8_t MOTOR_SPEED_STEP    = 25;

// How long a "still commanded" signal can go stale before the motor
// safety-stops. A physical button's release is detected instantly via GPIO
// level, but IR intent is only known from the last received repeat code, so
// this bounds how long the motor keeps running after the last IR signal
// before assuming the remote button was released.
constexpr unsigned long MOTOR_COMMAND_TIMEOUT_MS = 150;

// Absolute continuous-run cutoff regardless of input source, in case of a
// mechanical jam, stuck button, or a missed release signal.
constexpr unsigned long MOTOR_MAX_RUN_TIME_MS = 10000;

// Mute/volume/calibration are always driven through BOTH paths below
// unconditionally -- the direct DRV8833 motor + MUTE_PIN (Pins.hpp), AND
// UART commands to a daughter board -- regardless of whether either is
// actually physically connected on a given unit. Whichever is wired takes
// effect; the other silently has no effect (an unconnected GPIO toggling,
// or a UART command with nothing listening). Firmware never needs to know
// or decide which hardware variant is actually present.

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
constexpr uint32_t MUTE_BOOT_HOLD_MS = 3000;

// UART daughter board link (pins in Pins.hpp).
constexpr uint32_t DAUGHTER_UART_BAUD = 115200; // confirm against the daughter board's actual firmware

constexpr uint8_t DAUGHTER_VOL_MIN = 1;
constexpr uint8_t DAUGHTER_VOL_MAX = 64;

// How often to re-fire VOL UP/DOWN while a volume button is held, since
// each command is a single discrete step on the daughter board rather than
// a continuous drive signal (unlike the motor path, which drives
// continuously for as long as wantUp/wantDown is true).
constexpr uint32_t DAUGHTER_VOL_REPEAT_MS = 250;

// How long to wait for a reply to an ordinary (non-CAL) command before
// giving up and reporting a timeout.
constexpr uint32_t DAUGHTER_RESPONSE_TIMEOUT_MS = 500;

// CAL's behavior toward other commands sent during calibration isn't
// finalized on the daughter board yet (may reply ERR, may reply nothing at
// all). Rather than depend on either possibility, the link holds off
// sending anything new for this long after CAL's initial "OK" ack (which
// only means calibration STARTED, not that it's finished), then resumes
// normal traffic. This is a placeholder guess, not a real completion
// signal -- replace with something that actually knows when calibration
// finished (an unsolicited "done" message, or a defined polling behavior)
// once the daughter board protocol settles on one.
constexpr uint32_t DAUGHTER_CAL_HOLD_MS = 8000;



// Panel Specifications
constexpr uint16_t SCREEN_WIDTH  = 428;
constexpr uint16_t SCREEN_HEIGHT = 142;
constexpr uint32_t TOTAL_PIXELS  = SCREEN_WIDTH * SCREEN_HEIGHT;
constexpr uint32_t BUFFER_BYTES  = TOTAL_PIXELS * sizeof(uint16_t);

// Colors, plain RGB565. Arduino_GFX's fillRect/fillScreen/drawLine calls
// already handle correct SPI byte order internally for whatever bus they're
// given, so these must NOT be pre-byte-swapped -- doing so scrambled hues
// (e.g. "green" rendered as red/maroon, "yellow" as purple).
constexpr uint16_t COLOR_BLACK     = 0x0000;
constexpr uint16_t COLOR_GREEN     = 0x07E0;
constexpr uint16_t COLOR_YELLOW    = 0xFFE0;
constexpr uint16_t COLOR_RED       = 0xF800;
constexpr uint16_t COLOR_WHITE     = 0xFFFF;
constexpr uint16_t COLOR_CYAN      = 0x07FF;
constexpr uint16_t COLOR_DIM_GREEN = 0x0320; // faint reference/gridline color

// Spectrum bar gradient thresholds, as a fraction of the bar's max height.
// Below the yellow threshold the segment renders green, between the two
// thresholds it renders yellow, and above the red threshold it renders red.
constexpr float BAR_COLOR_YELLOW_THRESHOLD = 0.60f;
constexpr float BAR_COLOR_RED_THRESHOLD    = 0.85f;

// Number of discrete color bands used to approximate a smooth gradient for
// ColorTheme presets with GradientStyle::SMOOTH -- a true per-pixel-row
// gradient would cost one fillRect per row during large height changes;
// this many bands looks continuous from normal viewing distance while
// keeping the same delta-redraw performance characteristics as the
// flat-zone themes.
constexpr uint16_t GRADIENT_BAND_COUNT = 24;

// Menu screen text/layout sizing. 3 is Adafruit_GFX's nearest supported
// integer multiplier to "1.5x the previous size-2 default" (2*1.5=3
// exactly -- the built-in bitmap font only scales by whole integers, no
// fractional sizes). Row height/list top were retuned to fit the same
// max-4-item lists without needing to scroll at the larger size.
constexpr uint8_t  MENU_TEXT_SIZE  = 3;
constexpr uint16_t MENU_TITLE_Y    = 2;
constexpr uint16_t MENU_LIST_TOP_Y = 30;
constexpr uint16_t MENU_ROW_HEIGHT = 26;

// Number of waveform samples rendered per frame. Each sample costs its own
// short SPI line-draw transaction (erase + redraw), and per-transaction
// overhead -- not pixel volume -- is what's limiting waveform frame rate.
// Halving this roughly halves that call count. Lower = faster, less
// horizontal detail; raise back toward SCREEN_WIDTH for max resolution if
// you don't need the frame rate. This is the DISPLAY-side decimation
// target -- how many points getWaveform() decimates down to for drawing --
// distinct from WaveformPipeline's kOscilloscopeSamples/kTrackPreviewSamples
// (WaveformPipeline.hpp), which is the ENGINE-side raw capture window
// getWaveform() decimates FROM.
constexpr uint16_t WAVEFORM_SAMPLES = SCREEN_WIDTH / 2;

// Waveform erase/redraw is processed in chunks of this many samples rather
// than the whole trace at once (see WaveformScreen::renderStereo/Mono's
// chunked loop), so only a small stretch of screen is ever blank between
// erasing the old chunk and drawing the new one -- doing the whole trace
// in one erase-then-redraw pass causes a full-width blank flash every
// frame, regardless of how fast that pass completes.
//
// Two costs pull in opposite directions as this changes:
//  - Each chunk costs a fixed handful of SPI calls (erase old segment,
//    repaint the gridline under it, draw new segment) -- a SMALLER chunk
//    means MORE chunks per frame, so MORE total SPI transactions, each
//    with its own fixed per-call overhead regardless of how little data
//    it carries.
//  - drawWavePolylineRange() coalesces consecutive equal-Y points into
//    ONE longer line call instead of many 1-pixel-wide ones, but it only
//    looks for runs WITHIN a single chunk -- a smaller chunk caps how
//    long a run can ever be, capping how much that optimization can help
//    even when the waveform is genuinely flat/quiet there.
// Both costs get WORSE, not better, as this shrinks -- there's no
// tradeoff where a smaller value wins on both counts. The only thing a
// smaller value buys is a narrower transient blank gap per chunk, which
// is already imperceptible at the values below.
//
// At the low end: 1 makes every chunk exactly one segment (2 points) --
// the maximum possible chunk count AND a complete loss of the run-length
// coalescing benefit (a 2-point "run" can never merge with anything), for
// a flicker reduction that isn't visible at any value in this range to
// begin with. There's no upside to 1, only the two costs above maxed out.
// 0 is not just a bad value but a HANG: the render loop advances by
// `chunkStart += WAVE_CHUNK_SAMPLES`, so 0 never advances and the screen
// freezes forever on the very first waveform frame -- never set this to 0.
//
// At the high end: raising this toward WAVEFORM_SAMPLES (how many points
// are actually on screen) shrinks the chunk count back down to 1, which
// is exactly the "erase everything, then redraw everything" behavior this
// whole scheme exists to avoid -- the full-width blank flash returns.
//
// 8-16 is the sensible range: few enough chunks to keep SPI call count
// reasonable, each chunk still wide enough that a flat/quiet stretch of
// waveform gets real benefit from the run coalescing above.
constexpr size_t WAVE_CHUNK_SAMPLES = 8;

static_assert(WAVE_CHUNK_SAMPLES >= 1,
              "WAVE_CHUNK_SAMPLES must be at least 1 -- at 0, the render loop's "
              "chunkStart += WAVE_CHUNK_SAMPLES never advances, hanging the device on the very first "
              "waveform frame. This is a proven hard floor, not a guideline.");
static_assert(WAVE_CHUNK_SAMPLES <= (WAVEFORM_SAMPLES / 4),
              "WAVE_CHUNK_SAMPLES this large leaves too few chunks per frame to keep the erase/redraw "
              "sweep's transient blank gap imperceptible -- this ceiling guarantees at least 4 chunks "
              "per frame. The documented sensible range (8-16) sits comfortably inside it; this is a "
              "sanity guard against a value close to WAVEFORM_SAMPLES itself (which reintroduces the "
              "full-width flash this scheme exists to avoid), not a proven hard limit like the floor "
              "above.");

// Raw waveform samples (DC-removed, +/-2048 range) with magnitude below this
// render as a flat zero line instead of jitter. Mainly there to hide small
// ADC round-robin crosstalk on a channel that isn't carrying a real signal --
// it won't hide genuine crosstalk larger than the gate, since that's the ADC
// actually reading something real. Kept as its own separate, still-tunable
// constant (not consolidated with SPECTRUM_NOISE_GATE below) -- real
// hardware testing found 0 works well here, but this one's still being
// experimented with independently for a while longer.
constexpr int16_t WAVE_NOISE_GATE = 0;

// FFT bin/band magnitude (post Hann-coherent-gain-corrected scale) below
// which a band reads as silent rather than showing residual
// noise/crosstalk. Like WAVE_NOISE_GATE, this targets ADC round-robin
// crosstalk on a channel not carrying real signal -- it won't hide genuine
// content louder than the gate, since that's the ADC actually reading
// something real. Shared by SpectrumPipeline's peak and quadrature-sum
// aggregation modes alike -- these were briefly split into separate
// BAR_NOISE_GATE/OCTAVE_NOISE_GATE constants (back when peak and
// quadrature-sum lived in two separate pipeline classes) on the theory
// that quadrature-summed bands would need a different threshold than
// peak-based bars, but real hardware testing found the same 0.01f value
// works well for both, so consolidated back to one.
constexpr float SPECTRUM_NOISE_GATE = 0.01f;

// Target frame period. The main loop paces itself against this instead of
// an unconditional delay, so measured FPS reflects the real budget rather
// than render_time + fixed_delay stacking on top of each other.
// For ~60 FPS: Set to 16000
// For ~90 FPS: Set to 11111
// For ~100 FPS: Set to 10000
// For ~120 FPS: Set to 8333
constexpr uint32_t TARGET_FRAME_US = 10000; // ~90 fps

// How often to print the rolling performance summary to Serial.
constexpr uint32_t FPS_REPORT_INTERVAL_MS = 1000;

// Audio & FFT Parameters
//
// Two independent consumers, decoupled from each other so neither is held
// hostage by the other's needs:
//  - WaveformScreen wants a SMALL window for an oscilloscope-like "a few
//    cycles" look, refreshed as fast as possible -- it never needs FFT at
//    all, only raw samples.
//  - SpectrumPipeline (feeding SpectrumScreen) runs one FFT at FFT_SIZE
//    below, aggregated according to whichever Band Layout/Energy Mode the
//    menu has selected (see SpectrumPipeline.hpp) -- Auto derives a bar
//    count from FFT_SIZE itself, Fixed Bands uses a 31-band 1/3-octave
//    ISO table needing more low-frequency resolution than Auto's bars do.
//    These two modes were originally two entirely separate pipeline
//    classes with independently-sized FFT windows (1024 for the bar
//    scheme, 8192 for octave bands), each with its own refresh cadence;
//    consolidated to one shared FFT_SIZE after testing showed the larger
//    window's much slower refresh read as sluggish in practice, and this
//    size still gives Fixed Bands mode meaningfully better resolution
//    (~21.5Hz/bin) than Auto's old 1024 size (~43Hz/bin) while refreshing
//    far faster than the old 8192 size did.
//
// Both are fed from the SAME physical ADC/DMA capture stream (there's
// only one ADC), continuously appended to at CAPTURE_CHUNK_SAMPLES
// granularity -- see AudioEngine::runCore1(). SpectrumPipeline only
// actually runs its FFT once enough new samples have accumulated to
// justify it (its own 50% overlap point); the waveform window just always
// has fresh data ready, no FFT gating needed.

// Smallest physical DMA capture unit, interleaved L+R samples. This is
// the single source both the FFT window AND the waveform window are built
// from -- small enough to give WaveformScreen very fast, snappy refresh
// without needing a separate physical capture path of its own. Must stay
// a power of two (DMA ring-wrap requirement) and must evenly divide
// FFT_SIZE's half-window overlap point below, so it never needs an
// awkward partial-chunk shift.
constexpr uint16_t CAPTURE_CHUNK_SAMPLES = 256; // 128 samples/channel, ~2.9ms/chunk at 44.1kHz/channel

// WaveformScreen's own sliding window size now lives in
// WaveformPipeline.hpp (kOscilloscopeSamples/kTrackPreviewSamples), not
// here -- it moved from a fixed compile-time value to a runtime-
// selectable pair once the menu grew a Wave submenu, matching the same
// each-pipeline-owns-its-own-size-constants pattern FFT_SIZE's
// predecessors (NUM_BARS/OCTAVE_BAND_COUNT) already followed.

// Shared FFT size for SpectrumPipeline, regardless of which Band Layout
// mode (Auto/Fixed Bands) is currently active -- see the block comment
// above for the history of why this used to be two separately-sized FFTs.
constexpr uint16_t FFT_SIZE = 2048;

static_assert((FFT_SIZE / 2) % (CAPTURE_CHUNK_SAMPLES / 2) == 0,
              "FFT_SIZE/2 must be an exact multiple of the per-channel capture chunk size");

// Approximate per-channel ADC sample rate given the current adc_set_clkdiv()
// setting and round-robin between 2 channels (48MHz / 545 conversions/sec,
// halved across L/R). Used by SpectrumPipeline's Fixed Bands mode to
// convert each ISO band's Hz boundaries into FFT bin indices -- update
// this if the ADC clock divider ever changes.
constexpr float AUDIO_SAMPLE_RATE_HZ = 44100.0f;

constexpr bool CUSTOM_CLOCKS_ENABLED = true;

constexpr uint32_t SPI_SPEED_HZ = 50000000;
constexpr uint32_t CPU_SPEED_KHZ = 200000;