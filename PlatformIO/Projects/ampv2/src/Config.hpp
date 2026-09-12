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

// In STEREO spectrum-bar mode, mirrors the LEFT channel's bar order (bin
// NUM_BARS-1, the highest frequency, drawn at the outer/left edge; bin 0,
// the lowest, drawn nearest center) so low frequencies concentrate in the
// middle of the display and high frequencies push out to both outer edges
// -- the right channel's bars already increase left-to-right with bin 0
// nearest center, so only the left group needs reordering to match. Purely
// a rendering-position change: which array slot tracks which frequency
// bin's smoothing/peak state is unaffected. Only affects BarSpectrumScreen
// (the only screen with multiple per-channel bars that has a "mirror"
// concept at all) and only in STEREO mode (MONO has one bar group, nothing
// to be symmetric with).
constexpr bool MIRROR_LEFT_CHANNEL_BARS = true;

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
// you don't need the frame rate.
constexpr uint16_t WAVEFORM_SAMPLES = SCREEN_WIDTH / 2;

// Waveform erase/redraw is processed in chunks of this many samples rather
// than the whole trace at once, so only a small stretch of screen is ever
// blank between erasing the old chunk and drawing the new one -- doing the
// whole trace in one erase-then-redraw pass causes a full-width blank
// flash every frame, regardless of how fast that pass completes.
// 8 - 12 - 16
constexpr size_t WAVE_CHUNK_SAMPLES = 8;

// Raw waveform samples (DC-removed, +/-2048 range) with magnitude below this
// render as a flat zero line instead of jitter. Mainly there to hide small
// ADC round-robin crosstalk on a channel that isn't carrying a real signal --
// it won't hide genuine crosstalk larger than the gate, since that's the ADC
// actually reading something real.
constexpr int16_t WAVE_NOISE_GATE = 12;

// FFT bin magnitude (post Hann-coherent-gain-corrected scale) below which a
// bar reads as silent rather than showing residual noise/crosstalk. Like
// WAVE_NOISE_GATE, this targets ADC round-robin crosstalk on a channel not
// carrying real signal -- it won't hide genuine content louder than the
// gate, since that's the ADC actually reading something real.
constexpr float BAR_NOISE_GATE = 0.01f;

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
constexpr uint16_t FFT_SIZE      = 1024;
constexpr uint16_t TOTAL_SAMPLES = FFT_SIZE * 2; // Interleaved L/R
constexpr uint8_t NUM_BARS  =    9;

// FFT bin-to-bar mapping: bin count doubles per bar (1,2,4,8,...), skipping
// bins 0-1 (DC and the bin needing different scaling). An alternative
// logarithmic/linear-hybrid scheme was tried and compared numerically --
// it produced nearly identical bin boundaries for most bars, with the only
// real difference being how the single top "catch-all" bar was bounded, so
// it wasn't worth the added code path and was removed.

// Approximate per-channel ADC sample rate given the current adc_set_clkdiv()
// setting and round-robin between 2 channels (48MHz / 545 conversions/sec,
// halved across L/R). Used by OctaveScreen's fixed ISO-band-to-bin mapping
// to convert each band's Hz boundaries into FFT bin indices -- update this
// if the ADC clock divider ever changes.
constexpr float AUDIO_SAMPLE_RATE_HZ = 44100.0f;

// Number of 1/3-octave ISO bands OctaveScreen displays (20Hz-20kHz plus a
// 0-20Hz sub-bass catch-all, 31 bands total). Bin resolution at FFT_SIZE=
// 1024 and AUDIO_SAMPLE_RATE_HZ above is ~43Hz/bin -- several of the
// lowest bands (up to roughly 80-100Hz) are narrower than that single
// bin's width and will read nearly identically to their neighbors; this is
// a real resolution limit of a 1024-point FFT at this sample rate, not a
// bug in the band-to-bin mapping.
constexpr uint8_t OCTAVE_BAND_COUNT = 31;

constexpr bool CUSTOM_CLOCKS_ENABLED = true;
//constexpr int SPI_SPEED_HZ = 33333333;
constexpr uint32_t SPI_SPEED_HZ = 50000000;
constexpr uint32_t CPU_SPEED_KHZ = 200000;