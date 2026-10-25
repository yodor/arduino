# RP2350 Amplifier Main Controller

Oct 29, 2026 · @Todor Gyumyushev

This is the main controller of my DIY class-D amplifier: a Raspberry Pi Pico 2 (RP2350) that runs the front-panel display, the remote, the buttons and the power sequence, and talks to a separate LDR volume attenuator board. It is a hobby project, shared in case parts of it are useful to someone building something similar.

The amplifier is built from three parts: a 2N5457 JFET splitter on the input, an LDR (vactrol) attenuator on its own daughter board, and a TPA3255 power stage. The controller sits beside the signal path, not in it: it reads the audio before the volume control for its displays, and tells the daughter board what to do over a serial link. The LDR board is described on its own page.

## What it does

The controller drives a 428 × 142 px bar display and cycles through six screens:

| Screen | Shows |
| --- | --- |
| Info | Volume in dB and steps, Rtotal, thermal compensation, L/R match, block temperature, mute/resync alerts, time |
| Clock | Seven-segment clock from a DS3231, with seconds or the date; colour is selectable |
| Waveform | Oscilloscope with a zero-crossing trigger, or a longer "track preview" window |
| Spectrum | FFT bars: octave-doubling, fixed ISO bands, or full resolution; peak hold |
| Digital VU | Horizontal bars per channel with peak markers and a dB readout |
| Analog VU | Needle meter(s) on a circular scale, pivot below the screen edge |

Beyond the screens it handles:

- **Volume and mute**, through the LDR board. The first press of a volume key only shows the Info screen; the next ones change the level.
- **Power**: an SSR switches the amplifier's mains, with a mute → relay → settle → unmute sequence, a standby clock (or dark screen), and optional auto-standby after a period of silence.
- **Calibration of the LDR board** from the menu: Rtotal (auto or a chosen value), the source/load model, live progress, status, temperature and thermal-learning pages, and a reminder when a resync is advisable.
- **Settings** kept in flash: theme, brightness, screen options, clock, standby behaviour.

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

**Core 1, audio.** The ADC samples both channels round-robin, and DMA writes them into a continuous 16-chunk ring buffer (about 46 ms). Core 1 reads it chunk by chunk and feeds two pipelines:

- **Spectrum**: a 2048-point real FFT, grouped into bands (octave-doubling, fixed ISO bands, or full resolution), with peak or quadrature energy.
- **Waveform**: a sliding window (about 12 ms in oscilloscope mode, 93 ms in track-preview mode), double-buffered so the screen never reads half an update.

**Core 0, everything else.** The main loop runs at about 100 frames per second:

- **Screens**: each one redraws only what changed since the last frame.
- **Menu**: a nested menu with option lists, editors, live info pages and a colour picker.
- **Overlays**: the Info screen doubles as a volume popup; power and notice popups sit above it.
- **Inputs**: debounced buttons with long-press (3 s: left opens the menu, right toggles power) and IR remote decoding.
- **Power**: the standby sequence and the silence detector for auto-standby.
- **Settings**: kept in the last flash sector, with older layouts migrated on load. During a flash write core 1 is parked in RAM through a small hand-made interlock.

**LDR board link.** A line-based, strict protocol: setters answer `OK` or `ERR …`, and one `STATUS` line carries every readable value. The master polls `STATUS`, plus `PROGRESS` during calibrations, and `ENV`, `LEARN` and `MODEL` when needed. Only one command is in flight at a time, user commands go ahead of background polls, and everything is retried until the link takes it. The board's bench console is reachable through the master's USB console (`DAUGHTER`, `~.` to return).

## Using it

The four buttons and the remote do the same things:

| Control | Short press | Hold 3 s |
| --- | --- | --- |
| Right | Next screen / enter in the menu | Power on / standby |
| Left | Previous screen / back in the menu | Open / close the menu |
| Up / Down | Volume (first press shows the Info screen); move in the menu | — |
| Remote extras | Power, Mute, Home (menu), Options (stereo / mono) | — |

The menu, in short:

- **Theme**: colours, stereo / mono, spectrum, waveform and peak-hold options.
- **Settings**
  - **Brightness**
  - **Clock**: set time and date; display (clock only / seconds / date); 12 or 24 hour; colour.
  - **Power**: standby display (clock / dim clock / off); auto-standby time; silence level, with a live input-level page.
  - **Calibration**: Rtotal → Cancel / Start, model (source and load impedance), progress, status, temperature, learning, forget.
  - **Diagnostics**: amp mute, thermal compensation on/off, a display test.
  - Firmware version.

When the LDR board recommends a resync, a short notice comes back every 30 s; pressing Right on it opens the calibration start directly.

## Lessons learned

A few things cost me real time; noted here in case they save someone else some.

- **The NV3007 panel wants whole pixel pairs.** Any write window that splits a pair of columns (a 1-pixel-wide line, an odd start) can leave wrong pixels behind, and very short windows misbehave too. The wiring, SPI speed and CS/DC lines were all fine. The driver now widens every window to whole even-aligned pairs and at least 4 rows, filling the extra pixels from a shadow copy of the screen (about 119 KB). A built-in display test (Settings → Diagnostics) narrowed this down step by step.
- **Ping-pong DMA silently drops audio.** With two alternating buffers, one chunk that takes longer to process (the FFT ones) made the next capture get skipped, splicing gaps into every analysis window. It showed as an oscilloscope trace that jumped phase partway across. A continuous ring buffer read sequentially fixed it.
- **Data shared between the cores needs double buffering.** The display copied the waveform while the audio core rewrote it, and showed half of each.
- **Arduino-Pico's `Serial1` keeps only 32 received bytes by default.** A 145–220-byte status line lost its middle; `setFIFOSize(512)` before `begin()` fixed it.
- **Incremental drawing never repairs a lost pixel**, so a display problem shows up as permanent trails rather than flicker. That's useful when diagnosing it.
- **A blinking LED can be heard.** The one-second heartbeat on the on-board LED made a faint tick in an inductor; with the LED left alone the board is silent.
- **A meter needle should be a radius.** The first analog meter used an ellipse to fill the screen, and the needle always looked slightly off; a fitted circle with its centre below the screen edge fixed it.

## Building the firmware

The firmware is plain C++ on the Arduino-Pico core (Earle Philhower's RP2040/RP2350 core), built for the Raspberry Pi Pico 2.

- **Libraries**: Arduino\_GFX (display; the NV3007 workaround subclasses its driver), IRremote, and kissfft, which the code expects in `lib/kissfft`.
- **Configuration**: `Pins.hpp` holds the pin map and `Config.hpp` everything tunable, such as timings, thresholds, SPI speed, debug switches and the expected LDR protocol revision.
- **Version**: `Version.hpp` uses a build date plus a letter, e.g. `2026.10.29b`. It's printed at boot and shown in the menu.
- **USB console** (115200 baud): `HELP`; `TIME [YYYY-MM-DD HH:MM:SS]` to show or set the clock; `BUTTONS` for a live button monitor; `DAUGHTER` to open the LDR board's console.

The clock is never set automatically. After fitting a new DS3231 or battery, set it in the menu or with `TIME`.

## Status and notes

The controller is finished and in daily use in the amplifier. After the driver block was potted in thermal epoxy, the LDR board's automatic Rtotal reached 1431 kΩ on my cells, my best so far.

This is written for one specific build, so expect to adapt it:

- The display workaround is tuned to my NV3007 module.
- Levels are relative dBFS of the ADC input, not calibrated volts.
- The serial protocol matches my LDR board's firmware (revision 14).

If you build something from it, or spot a mistake, I'd be glad to hear about it.

**Thanks** to the authors of Arduino-Pico, Arduino\_GFX, IRremote and kissfft, whose work this sits on.

**License**: MIT.
