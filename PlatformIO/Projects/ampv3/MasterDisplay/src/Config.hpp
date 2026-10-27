#pragma once
#include <Arduino.h>
#include <Pins.hpp>
#include <IRCodes.hpp>

// The remote only reports a held volume key as a train of repeat codes
// (~every 108ms), so a GAIN_UP/GAIN_DOWN counts as "still held" until this
// long after the last one -- that's what drives the daughter's VOL UP/DOWN
// repeats while the key is down. Physical buttons report held directly.
constexpr unsigned long IR_VOLUME_HOLD_MS = 150;

// Volume and mute are the daughter board's alone (its LDR network; the amp
// mute/boot hold is its own AMP_MUTE).

// Amp mains power (PowerControl): the relay state right after boot. The
// power key toggles it; it isn't remembered across a power cut.
constexpr bool POWER_ON_AT_BOOT = false;

// Power sequence (PowerControl): how long to wait for the daughter to
// confirm the amp is muted before switching mains (it usually confirms in a
// few ms; this is only the cap if it doesn't answer), and how long the amp
// gets to power up with mains on before it's unmuted.
constexpr uint32_t POWER_MUTE_WAIT_MS = 800;
constexpr uint32_t POWER_ON_SETTLE_MS = 3000;

// Standby: how long the power popup stays up after a sequence finishes or
// after a key is pressed in standby ("Press POWER to switch on"), and the
// backlight of the "Dim Clock" standby display.
constexpr uint32_t POWER_POPUP_MS       = 2000;

// Auto standby (Settings -> Power, off by default): the inputs count as
// "playing" while the waveform's peak (DC removed, raw 12-bit ADC counts)
// exceeds the Silence Level setting -- this is its default, comfortably
// above the idle noise, far below music. Checked every AUTO_STANDBY_CHECK_MS.
constexpr int16_t  AUTO_STANDBY_AUDIO_THRESHOLD = 24;
constexpr uint32_t AUTO_STANDBY_CHECK_MS        = 250;
constexpr uint8_t  STANDBY_DIM_PERCENT  = 15;

// UART daughter board link (pins in Pins.hpp).
constexpr uint32_t DAUGHTER_UART_BAUD = 115200; // confirm against the daughter board's actual firmware

// Protocol revision 7: the volume range is no longer a master-side
// constant -- it comes from the daughter's STATUS line (VOLMIN/VOLMAX,
// 0-based, normally 0..31), so nothing here hard-codes it.

// Protocol revision this master is written for. A daughter reporting a
// different PROTO= is logged loudly (the protocol doc says a master
// written for another revision will misbehave), but still parsed.
constexpr uint8_t DAUGHTER_PROTO_EXPECTED = 16;

// How often to re-fire VOL UP/DOWN while a volume button is held -- each
// command is a single discrete step on the daughter board.
constexpr uint32_t DAUGHTER_VOL_REPEAT_MS = 250;

// How long to wait for a reply to any command before giving up and
// reporting a timeout. Revision 7 answers EVERY command within this,
// including STATUS during a calibration (~10ms).
constexpr uint32_t DAUGHTER_RESPONSE_TIMEOUT_MS = 500;

// STATUS is the only getter in revision 7, so the master polls it: every
// IDLE_MS normally, every BUSY_MS while the daughter reports BUSY
// (calibrating, or its boot hold -- the hold poll is shortened further to
// the remaining time it reports). Also sent right after every successful
// VOL/MUTE/AMP_MUTE command, so the on-screen volume reflects the
// daughter's real VOL/DB rather than a local guess.
constexpr uint32_t DAUGHTER_STATUS_POLL_IDLE_MS = 5000;
constexpr uint32_t DAUGHTER_STATUS_POLL_BUSY_MS = 2000;

// Revision 8 read-only queries: PROGRESS polled this often while a
// calibration runs (for the progress page), ENV this often while a page
// that shows it is open (LEARN, rev 9, likewise).
constexpr uint32_t DAUGHTER_PROGRESS_POLL_MS = 1000;
constexpr uint32_t DAUGHTER_ENV_POLL_MS      = 5000;
// ...and, in the background, this often anyway while the daughter is idle --
// for revision 14's resync hint and revision 13's NTC state (both based on a
// 1-minute average, so a slow poll misses nothing).
constexpr uint32_t DAUGHTER_ENV_BACKGROUND_POLL_MS = 30000;

// Notices (resync advised, NTC fault): how long the popup stays up.
constexpr uint32_t NOTICE_POPUP_MS = 5000;

// A STATUS older than this is treated as "no link" (the daughter stopped
// answering, or isn't fitted on this unit) -- comfortably more than two
// idle polls, so a single lost reply doesn't flip the display.
constexpr uint32_t DAUGHTER_STATUS_STALE_MS = 12000;

// After a local mute toggle, ignore the daughter's reported MUTE for this
// long before adopting it -- a STATUS that was already in flight when the
// user pressed mute would otherwise "undo" the press.
constexpr uint32_t DAUGHTER_MUTE_ADOPT_GRACE_MS = 1000;

// Set true to log every UART line in both directions ([UART TX]/[UART RX])
// -- bring-up/debug only; with STATUS polled every few seconds it's noisy.
constexpr bool DAUGHTER_LINK_TRACE = false;

// Debug serial output, off for normal use:
//  - DEBUG_AUDIO_DIAGNOSTICS: core1's once-a-second [Timing]/[RawADC]
//    report (chunk timing, FFT duration, DMA overlap, raw ADC peak-to-peak)
//    and SpectrumScreen's [Spectrum] loudest-bin line.
//  - DEBUG_PERF: core0's once-a-second [Perf] FPS/render-time line.
constexpr bool DEBUG_AUDIO_DIAGNOSTICS = false;
constexpr bool DEBUG_PERF              = true;

// Oscilloscope trigger for the Waveform screen (Oscilloscope mode only): each
// frame starts at a rising zero crossing, so the trace stands still and
// frames line up -- see WaveformPipeline::getWaveform(). The visible span is
// 3/4 of the window (the first quarter is where the crossing is searched).
constexpr bool WAVEFORM_TRIGGER = true;

// Panel workaround (see PanelDriver.hpp): this panel stores pixel columns in
// pairs, so every write window is widened to whole even-aligned pairs, using
// a shadow copy of the screen for the extra columns' colors. The Display Test
// flips this at runtime with the remote's mute button to compare.
constexpr bool DISPLAY_ALIGN_PIXEL_PAIRS = true;

// Second panel workaround: very short write windows (1-3 rows) are grown to
// this many rows, the extra rows again filled from the shadow. Confirmed on
// the device: the Waveform -- thousands of tiny windows per frame -- kept a
// few stray dots with only the pair fix, and is clean with this at 4.
// 0 = off. The display test toggles 0 <-> 4 with the remote's channel button.
constexpr uint8_t DISPLAY_MIN_WINDOW_ROWS = 4;

// Menu editors (Set Time / Set Date): holding a physical Up/Down button
// auto-repeats after this delay, at this rate.
constexpr uint32_t MENU_REPEAT_DELAY_MS = 500;
constexpr uint32_t MENU_REPEAT_RATE_MS  = 120;

// i2c1 bus speed for the DS3231 RTC (it supports up to 400kHz).
constexpr uint32_t RTC_I2C_CLOCK_HZ = 400000;

// On-screen volume overlay: how long it stays up after the last volume/
// mute change (from the buttons/IR, or seen in the daughter's STATUS).
constexpr uint32_t VOLUME_OVERLAY_TIMEOUT_MS = 2000;



// Panel Specifications
// Panel rotation passed to the NV3007 driver: 1 = original orientation,
// 3 = flipped 180 degrees (the mounted device).
constexpr uint8_t TFT_ROTATION = 3;
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

// The DMA captures into a continuous ring of this many chunks (16 = 4096
// samples, 8KB, ~46ms of audio), which core1 reads chunk by chunk. Any chunk
// whose processing runs long (the ones where an FFT runs) only builds up a
// backlog that the next, quicker chunks work off -- nothing is lost as long
// as processing keeps up on average. (It used to be a two-buffer ping-pong:
// one slow chunk there meant the next one was silently skipped, splicing gaps
// into every analysis window -- visible as a waveform jumping phase partway
// across the screen.) Power of two, so the ring-wrap addressing works.
constexpr uint16_t CAPTURE_RING_CHUNKS = 16;
static_assert((CAPTURE_RING_CHUNKS & (CAPTURE_RING_CHUNKS - 1)) == 0, "CAPTURE_RING_CHUNKS must be a power of two");

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

// Display SPI clock. With clk_peri bound to clk_sys (200MHz) the reachable
// rates are 200MHz / even divisor: 50MHz, 33.3MHz, 25MHz... 50MHz was fine
// on the breadboard, but on the mounted device the bar/peak artifacts point
// to writes being lost on the bus (see the note in Renderer.cpp), so this
// runs one step slower. Render time per frame is a small fraction of the
// 10ms frame budget on every bar screen, so the cost is invisible there.
constexpr uint32_t SPI_SPEED_HZ = 33333333;
constexpr uint32_t CPU_SPEED_KHZ = 200000;