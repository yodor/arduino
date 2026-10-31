# RP2350 Amplifier Main Controller

Oct 29, 2026 · @Todor Gyumyushev

This is the main controller of my DIY class-D amplifier: a Raspberry Pi Pico 2 (RP2350) that runs the front-panel display, the remote, the buttons and the power sequence, and talks to a separate LDR volume attenuator board. It is a hobby project, shared in case parts of it are useful to someone building something similar.

The amplifier is built from three parts: a 2N5457 JFET splitter on the input, an LDR (vactrol) attenuator on its own daughter board, and a TPA3255 power stage. The controller sits beside the signal path, not in it: it reads the audio before the volume control for its displays, and tells the daughter board what to do over a serial link. The LDR board is described on its own page.

## What it does

The controller drives a 428 × 142 px bar display and steps through six screens with RIGHT and LEFT:

| Screen | Shows |
| --- | --- |
| Info | Volume in dB or as a step (e.g. `14/31`), Rtotal, thermal compensation (green when on, yellow while learning, red when off), L/R match, block temperature, mute and resync alerts, plus a small readout chosen with the remote's Options key: the detected tempo (BPM), a mini oscilloscope or a mini spectrum |
| Waveform | One zoom from 5.8 ms to 20 s: a triggered oscilloscope from 256 to 8192 samples per screen, then a scrolling min/max timeline from 1.2 s to 20 s across; optional auto-gain |
| Spectrum | FFT bars: octave-doubling, fixed ISO bands, or full resolution; peak markers at five fall speeds, from Turtle to Cheetah |
| Clock | Seven-segment clock from a DS3231, with the date, with seconds, or alone; its colour follows the palette or is chosen |
| Digital VU | A bar per channel with peak markers, L/R labels and a dB readout |
| Analog VU | Needle meter(s) on a circular scale, pivot below the screen edge |

The colours come from a palette: four are built in (Sunset, Aurora, Punk, Classic), and five slots hold your own, either a copy of any palette or a colour you pick, with the bottom and top colours derived from it around the colour wheel. Each palette also has a single "mix" colour, adjustable in position and brightness, for the mono waveform and the clock. At silence the bars keep a one-pixel floor, so a quiet input still reads as a line rather than a blank screen.

Beyond the screens it handles:

- **Volume, mute and balance**, through the LDR attenuator. The first press of a volume key only shows the Info screen; the next ones change the level. A double-click on the DOWN button toggles mute.
- **Power**: an SSR switches the amplifier's mains, with a mute → relay → settle → unmute sequence, a standby clock (bright, dimmed or dark), and optional auto-standby after a period of silence.
- **The attenuator's calibration** from the menu: a resync or full calibration, an 8-minute verify that measures how well the calibration holds without changing it, the source/load model, live progress, and a reminder when a resync is advisable.
- **Tempo detection**: the bass onsets are autocorrelated to estimate BPM, shown on the Info screen.
- **Settings** kept in a plain-text file on the board's flash.

## Hardware

The board is built around a Raspberry Pi Pico 2 (RP2350, two Cortex-M33 cores at 200 MHz). The master and the LDR board are powered whenever the mains switch is on; "off" is standby, with only the amplifier's mains cut by the SSR.

- **Display**: NV3007 bar TFT, 428 × 142 px, SPI at 33–50 MHz, PWM backlight.
- **Audio input**: both channels of the JFET splitter output, before the volume control, into the RP2350 ADC (44.1 kHz per channel). The displays therefore show the source level, independent of the volume setting.
- **Controls**: four buttons (up, down, left, right) and an IR receiver for a small NEC remote.
- **Power**: a 2N7000 (with gate resistor and pull-down) driving the SSR input; a power-button LED output.
- **Clock**: DS3231 module on I²C with a backup cell.
- **LDR board link**: UART at 115200 baud.

| Function | GPIO |
| --- | --- |
| UART to LDR board (TX / RX) | 0 / 1 |
| Buttons up / right / left / down | 2 / 3 / 12 / 13 |
| SSR relay (2N7000 gate) | 5 |
| Power-button LED | 9 |
| Power button (reserved) | 10 |
| IR receiver | 11 |
| I²C SDA / SCL (DS3231) | 14 / 15 |
| TFT DC / CS / SCK / MOSI / RST / backlight | 16 / 17 / 18 / 19 / 20 / 21 |
| SMPS mode (held high: low-noise PWM) | 23 |
| Audio right / left (ADC1 / ADC2) | 27 / 28 |

GPIO 6, 7 (an earlier motorised-pot driver) and 25 (the on-board LED) are defined for reference only and left unused.

## Firmware architecture

The firmware splits the work across the RP2350's two cores: core 1 only handles audio, core 0 does everything the user sees.

**Core 1, audio.** The ADC samples both channels round-robin at 44.1 kHz per channel, and DMA writes them into a continuous 16-chunk ring (about 46 ms). A few seconds after start-up the firmware measures the rate the DMA actually produces and corrects the ADC divider if needed. Core 1 reads the ring chunk by chunk and feeds three consumers:

- **Spectrum**: a 2048-point real FFT from ARM's CMSIS-DSP (`arm_rfft_fast_f32`), with 50% overlap, about 43 times a second per channel; magnitudes with `arm_cmplx_mag_f32`, then bands (octave-doubling, ISO or full resolution), double-buffered so the screen always gets one complete FFT.
- **Waveform**: each chunk is appended to a capture ring of 16,384 samples per channel, at the same small cost at any zoom; the screen reads exactly the samples it needs when it draws. A min/max timeline for the long zoom levels is built alongside.
- **Beat detector**: a bass onset envelope, cheap enough to run on every chunk.

All of it takes well under half a millisecond of every 2.9 ms chunk, so the audio core is mostly idle.

**Core 0, everything else.** `loop()` reads like a plain event loop at about 100 frames per second: take input, route events, run power and volume, let the view manager decide what's shown, render, pace the frame. The details live in the classes that own them:

| Module | Responsibility |
| --- | --- |
| `ViewManager` | What's on the panel each frame, by priority: power layer, display test, menu, Info popup, notice, current screen. Owns the screen ring and the transitions (redraws, backlight). |
| `InputEngine` | Debounced buttons with long-press and the DOWN double-click, IR remote decoding, and "volume requested" for buttons and remote alike. |
| `VolumeControl`, `AttenuatorBoardLink` | Volume, mute, balance and calibration commands to the LDR attenuator, queued and retried. |
| `PowerControl`, `AutoStandby` | The power sequence and the silence timer. |
| `ColorTheme` | The palettes: built-in and user slots, colour-wheel derivation, the mix colour. |
| `Console` | All USB serial output: tagged lines, float formatting; core 1's lines go through a queue so it never waits on USB. |
| `PersistentSettings` | The settings file (`/settings.txt` on LittleFS). |
| `Profiler`, `SystemInfo` | Timing statistics for the debug lines; chip and build facts. |

Screens and overlays derive from one `Screen` interface, and each redraws only what changed since the last frame. The menu uses the Info screen's three bands: a header, the item's name at the size of the big dB number, and a line underneath at the size of its unit label, with live content where it helps (the calibration state, the compensation state).

**Attenuator link.** A line-based, strict protocol (revision 20): setters answer `OK` or `ERR …`, and one `STATUS` line carries every readable value. The master polls `STATUS`, `ENV` every 10 seconds, `PROGRESS` during a calibration or verify, and `LEARN`, `MODEL`, `VER` and `VERIFY RESULT` when needed. One command is in flight at a time, user commands go ahead of background polls, and everything is retried until the link takes it. If the attenuator restarts, the master notices from its uptime and restores balance and mute. Its bench console is reachable through the master's USB console (`ATTEN`, `~.` to return).

## Using it

The four buttons and the remote do the same things:

| Control | Short press | Hold 3 s |
| --- | --- | --- |
| Right | Next screen / enter in the menu | Power on / standby |
| Left | Previous screen / back in the menu | Open / close the menu |
| Up / Down | Volume (the first press shows the Info screen); move in the menu. DOWN double-click: mute | — |
| Remote extras | Power, Mute, Home (menu), Options | — |

The Options key depends on the screen: on Info it steps the small readout (BPM, mini scope, mini spectrum), on the Clock it steps the display (date, seconds, clock only), elsewhere it switches mono mix on or off.

The menu, in short:

- **Theme**: Palette (each palette has Apply, Mix Position, Mix Brightness and Save to Slot; the slots also have Color and Spread; the list opens at the palette in use), FFT, Wave (Scale, Auto Gain), Peaks (Off, Turtle, Slow, Normal, Fast, Cheetah), Mono Mix.
- **Calibration**: the first item shows the calibration live (e.g. "970K (auto)" over "resync advised"); inside are Start (choose Rtotal, then it runs), Verify (8 minutes, changes nothing) and Full (6 minutes, re-measures the cells). Then Last Verify, auto depth, model, progress, status, temperature, learning, forget.
- **Settings**: brightness, balance, volume display (dB or steps), clock (time, date, display, 12/24 h, colour), power (standby display, auto-standby, silence level, live input level).
- **Diagnostics**: versions of both boards, amp mute, thermal compensation (with its live state), a display test, debug output.

When the attenuator recommends a resync, a short notice comes back every 30 s; pressing Right on it opens Calibration with Start ready.

## Lessons learned

A few things cost me real time; noted here in case they save someone else some.

- **The NV3007 panel wants whole pixel pairs.** Any write window that splits a pair of columns (a 1-pixel-wide line, an odd start) can leave wrong pixels behind, and very short windows misbehave too. The wiring, SPI speed and CS/DC lines were all fine. The driver now widens every window to whole even-aligned pairs and at least 4 rows, filling the extra pixels from a shadow copy of the screen (about 119 KB). A built-in display test (Diagnostics) narrowed this down step by step. Drawing the waveform as one 2-pixel column per pixel pair turned out both exact and 2–3× faster than drawing lines.
- **Ping-pong DMA silently drops audio.** With two alternating buffers, one chunk that takes longer to process (the FFT ones) made the next capture get skipped, splicing gaps into every analysis window. It showed as an oscilloscope trace that jumped phase partway across. A continuous ring buffer read sequentially fixed it.
- **A threshold can hide data loss, too.** Later, a rule meant for long flash-write pauses ("after more than 10 ms, skip to fresh audio") quietly threw away 10–20% of the audio, because a USB print took just over 10 ms. The clue was the FFT refreshing at 34 Hz instead of 43. Measuring what the DMA actually produced next to what was processed made it obvious.
- **`sqrtf()` may not be the FPU instruction.** In this build a plain `sqrtf()` ended up as a software library call of several hundred cycles; the FFT magnitudes alone took 7.5 ms per run. CMSIS-DSP's `arm_cmplx_mag_f32`, which uses the `VSQRT` instruction directly, brought them to 0.36 ms. Per-stage timing found it in one step.
- **Read on demand instead of publishing.** The waveform pipeline used to copy its whole window for the display after every chunk, a cost that grew with the window. Appending to a long ring and letting the screen read what it needs when it draws made the cost flat, the data fresher, and longer zoom levels possible. The picture was proven identical frame by frame.
- **A big buffer on the stack can freeze the Pico.** Raising one display constant grew a screen's working arrays on the stack until they overflowed it, and the whole device locked up without any message. The buffers now live in the screen object, and a compile-time check rejects values that can't fit the screen.
- **Blend colours around the wheel, not through RGB.** A palette derived from one colour looked muddy at wide spreads: a straight RGB blend between colours far apart passes near grey. Blending the hue around the colour wheel keeps every bar as vivid as the chosen colour.
- **Data shared between the cores needs double buffering.** The display copied the waveform while the audio core rewrote it, and showed half of each.
- **Give the serial port one writer.** Both cores printing to USB caused stalls on the audio core; a queue that core 0 drains fixed it. Two smaller traps on the way: `printf` on this core prints nothing for floats, and terminals want `\r\n` line ends.
- **Arduino-Pico's `Serial1` keeps only 32 received bytes by default.** A 145–220-byte status line lost its middle; a larger receive FIFO, set before `begin()`, fixed it.
- **Incremental drawing never repairs a lost pixel**, so a display problem shows up as permanent trails rather than flicker. That's useful when diagnosing it.
- **A blinking LED can be heard.** The one-second heartbeat on the on-board LED made a faint tick in an inductor; with the LED left alone the board is silent.
- **A meter needle should be a radius.** The first analog meter used an ellipse to fill the screen, and the needle always looked slightly off; a fitted circle with its centre below the screen edge fixed it.

## Building the firmware

The firmware is plain C++ on the Arduino-Pico core (Earle Philhower's RP2040/RP2350 core), built with PlatformIO for the Raspberry Pi Pico 2.

- **Libraries**: Arduino\_GFX (display; the NV3007 workaround subclasses its driver) and IRremote. A subset of ARM's CMSIS-DSP 1.16.2 (the real FFT, magnitudes and scaling, with the CMSIS 6.1 compiler headers, Apache-2.0) is vendored in `lib/CMSISDSP`.
- **Flash filesystem**: `board_build.filesystem_size = 64k` reserves space for LittleFS, which holds the settings file.
- **Build flags**: `-O3 -fbuiltin -fno-math-errno -Wl,--gc-sections`.
- **Configuration**: `Pins.hpp` holds the pin map, `AttenuatorConfig.hpp` the attenuator link (protocol revision, poll intervals, buffer sizes), and `Config.hpp` everything else that's tunable, such as timings, thresholds, SPI speed, palette defaults and debug defaults. Values that can't work, such as a waveform wider than the screen, fail at compile time with an explanation.
- **Settings**: `/settings.txt`, one `key=value` per line, with choices stored by name (`theme=SUNSET`). Unknown keys are ignored and missing ones keep their defaults, so firmware updates never need a migration. The palette slots are one line each, so they can be copied between devices. A `data/settings.txt` in the project can be flashed with `pio run -t uploadfs`.
- **Version**: `Version.hpp` uses a build date plus a letter, e.g. `2026.11.20b`. It's printed at boot and shown under Diagnostics → Versions, next to the attenuator's.
- **USB console** (115200 baud): `HELP`; `TIME [YYYY-MM-DD HH:MM:SS]` to show or set the clock; `SETTINGS` (or `SETTINGS RESET`) for the settings file; `BUTTONS` for a live button and remote monitor; `ATTEN` to open the attenuator's console. Timing lines can be switched on under Diagnostics → Debug Output.

The clock is never set automatically. After fitting a new DS3231 or battery, set it in the menu or with `TIME`.

## Status and notes

The controller is finished and in daily use in the amplifier. After the driver block was potted in thermal epoxy, the LDR board's automatic Rtotal reached 1431 kΩ on my cells, my best so far.

This is written for one specific build, so expect to adapt it:

- The display workaround is tuned to my NV3007 module.
- Levels are relative dBFS of the ADC input, not calibrated volts.
- The serial protocol matches my LDR attenuator's firmware (revision 20).

If you build something from it, or spot a mistake, I'd be glad to hear about it.

**Thanks** to the authors of Arduino-Pico, Arduino\_GFX and IRremote, and to ARM for CMSIS-DSP, whose work this sits on.

**License**: MIT.
