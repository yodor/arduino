# LDR Volume Control — Attenuator

A dual-mono, light-dependent-resistor (vactrol) volume attenuator with automatic calibration, controlled by a Raspberry Pi Pico W. It is one part of a larger audio project; a controller handles the user interface, the display and power, and talks to this board over a simple serial protocol.

This is a hobby project. It works well in my system, and I am sharing it in case some of it is useful to others who enjoy the same kind of tinkering. It is not a product, and there are certainly things that could be done differently or better.

## What it does

Each channel is a series/shunt divider made of two NSL-32SR3 vactrols (an LED and a CdS photocell in one package). Setting the LED currents sets the two resistances, and therefore the attenuation. The appeal of this classic approach is a signal path with nothing in it but two resistors, no wiper, no switch contacts and no active stage.

The difficulty is equally classic: photocells differ from each other, depend on temperature, and remember their light history. Most of this project is about measuring and correcting for that, so that both channels track each other closely over the whole range.

In my system, with the current firmware:

- **32 volume steps over about 80 dB**, stereo-common (the depth both channels reach equally well), at a total divider resistance of about 970 kΩ chosen automatically.
- **Left/right match at the quiet end within about 0.01–0.1 dB** after a calibration.
- **A transparent top step:** the series cell fully bright (about 150 Ω) and the shunt switched off.
- **Temperature compensation** learned from the board's own calibration history, so the levels stay put as the enclosure warms and cools.

Absolute dB accuracy is not the goal; tracking between the channels and depth are. The middle steps can sit up to about 1 dB from their nominal level, identically on both channels.

## Hardware

### Driver board

- **Raspberry Pi Pico W (RP2040).** Four 12-bit PWM channels at about 36.6 kHz with dithering, filtered to DC, drive the four vactrol LEDs.
- **LED drivers:** a BC547C/BC557C pair per LED, with matched PNP pairs per function (series, shunt), 100 kΩ bleed resistors and WIMA MKS film capacitors in the filters.
- **All eight transistors sit on one aluminium block** with an NTC thermistor, so the drivers share one temperature and that temperature can be measured. The drivers' base-emitter drift turned out to be the dominant cause of level drift.
- **A TL431 reference** monitors the 3.3 V rail, which sets the PWM amplitude.
- Relay drive for both LDR boards, the amplifier mute output, and the UART link to the controller.

### LDR boards (one per channel, second edition)

- **Series and shunt NSL-32SR3** side by side, with the relay well away from the cells (relay coil heat was a measurable disturbance on the first layout).
- **A G5V-2 relay** switches the cells between the audio path and a measurement circuit, so calibration measures the cells directly, with the audio disconnected and the amplifier muted.
- **An ADS1115 per board,** on separate I2C buses, measures the series and shunt resistances against a 10 kΩ reference resistor. An MCP1700 LDO supplies it.
- Separate connectors for the LED drives (each LED with its own return wire) and the relay coil.

The KiCad files and the 3D-printed parts are on the project page.

## How it works

### The big picture: a system that keeps itself calibrated

The attenuator learns two things -- its cells, and how they follow temperature -- and from then on
keeps itself right without help:

1. **Characterization** measures each cell's individual properties once: its curves and how deep it
   settles reliably. From these the step table is built. It runs when nothing is stored or when the
   power-up cell check finds new cells (or on request, `CAL FULL`).
2. **Resyncs** re-trim the table to the cells as they are at that moment. Each resync is also one
   **learning** entry: how far the cells' operating point moved, against how much the temperature
   changed.
3. **The compensation** applies what was learned continuously while listening. The better the
   learning, the less often a resync is needed. The **resync hint** says when one would help: while
   learning, whenever the block has moved 1 C since the last calibration (warm-up included -- that
   is exactly the data the learning needs); once the compensation is active, only beyond what it
   has learned, and not while the device is still warming up.
4. **Verify** (`CAL VERIFY`) measures how well the table holds and reports what changed since the
   previous run. During development it is the debugging tool; afterwards an occasional health check.

Three refinements to that picture:

- **"Temperature" mostly means the driver block, not the cells.** The drift follows the driver
  block's temperature almost perfectly (R² close to 1), not the cells' own -- the transistors'
  temperature changes the LED current, and the LED current sets the cells' resistance. So the
  compensation corrects the LED drive, which is the actual cause, using the NTC on the block.
- **The cells do change slowly, so verify does not become redundant.** In daily use temperature
  dominates. Over months and years the LEDs inside the vactrols lose some efficiency, and CdS cells
  drift too -- most noticeably while new cells settle in during their first weeks. A uniform drift
  is simply absorbed by the next resync, like temperature. What a resync cannot fix is a change of
  the curve's **shape**, or one channel ageing differently from the other: that is what verify
  detects (`VERDICT=CHECK`), and a `CAL FULL` fixes it. A verify once a season -- and a few times in
  the first weeks after fitting new cells -- is enough.
- **Two effects are outside the temperature loop.** The cells' **memory** after loud passages (slower
  settling at the quiet steps) is a property of CdS itself; it is managed by how deep the attenuator
  goes (`SET AUTO SNAPPY | BALANCED | DEEP`), not by learning. The **source and load impedances** are
  part of the table's design, so changing them needs a re-solve, which the firmware requests
  (`CAL=STALE`).

Faults such as a leaking driver (flux residue around the transistors) show up in a verify as one
channel behaving differently, and the health checks name the likely place to look.

The terms are defined one by one in `ldr-attenuator-console.md` ("Concepts"); the details of each
stage follow below.

### Calibration

A full characterization runs in about six minutes and needs no external equipment:

1. **Sweeps** measure each cell's resistance against its LED drive, from bright to dark, on both channels at once.
2. **A dark-end walk** approaches the deep, slow part of each series cell's curve from the dark side, waits for each point to settle, and measures its "memory": how far it is off a few seconds after a loud passage. These measurements decide how deep the attenuator can safely go.
3. **The total resistance is chosen automatically** (the deepest value at which both channels still settle reliably), or set by hand.
4. **Every step is solved** for its target attenuation, taking the source impedance and the amplifier's input impedance into account, then **trimmed** on the real cells, both channels in parallel.
5. **The quietest point is matched** with settled readings between the channels, and plausibility guards catch trims that ended on a misleading reading.

A **resync** repeats only the trims, the quiet-point match and a check of the middle depths, in about two to three minutes. A **re-solve** chooses a new total resistance from the stored curves without measuring the cells again. A cell check at power-up recognizes when the cells have been replaced.

### Temperature and supply compensation

After every resync the firmware records how far the cells' operating point moved and how much the driver block's temperature changed. From that history it fits a slope (in my system about −5.4 PWM counts per °C, R² ≈ 0.98–0.99) and applies it continuously while listening. A resync is suggested when the temperature has moved beyond the range the fit has learned, or when a verify found the table off. Only resyncs feed the learning; a re-solve does not. Small changes of the 3.3 V rail are corrected against the TL431.

### Interface to the controller

A line-based ASCII protocol over UART or USB (currently revision 20) with commands for volume, balance, mute, amplifier mute, calibration and verify, and queries for status, progress, temperatures and compensation, the learning state, the audio model, the last verify and the firmware version. The controller owns the power state: the attenuator powers up muted, reports whether it is calibrated for the fitted cells, and never unmutes or calibrates on its own. The full specification is in `ldr-attenuator-protocol.md`.

A console mode (`DEBUG` on the same link, `EXIT` back) exposes everything for testing and diagnosis -- among it `CAL VERIFY` with its full output, `REPORT` (a compact summary for analysis), `DUMP`, and `EXPORT` / `IMPORT` for backing up and restoring the stored state. It is described in `ldr-attenuator-console.md`.

## Building

The firmware is built with PlatformIO and the Arduino-Pico core (`platformio.ini` is included). Every release package also contains a ready-built `firmware.uf2`: hold BOOTSEL, plug the Pico W in, and copy the file to the drive that appears.

The defaults in `Config.hpp` describe my system. The two values most likely to differ elsewhere, the source's output impedance and the amplifier's input impedance, can be set at runtime (`SET SRC` and `SET LOAD`) and are stored in flash; the attenuator then asks for a quick re-solve.

## Things I learned along the way

Shared in case they save someone time:

- **Drive relay coils with plain DC.** A PWM "hold" current coupled into the LED drive through stray capacitance and brightened the series cells several times over.
- **Heat matters more than expected,** but mostly in the LED drivers rather than in the cells. Putting all driver transistors on one block, with a sensor on it, made the drift predictable. Dedicated temperature sensors on the cells turned out not to be needed and were removed.
- **Very dark photocells are slow.** At hundreds of kΩ a cell takes seconds to settle; any measurement there must wait for it, or it measures the past.
- **Small measurement currents fail quietly.** Several of the harder bugs were readings that became too small to trust (a very dark cell in the measurement path) and were then treated as "no change". The firmware now checks this explicitly everywhere.
- **On the RP2040, check for NaN before doing arithmetic with it.** The fast floating-point routines in its boot ROM turn NaN − NaN into infinity.
- **A buffer after the attenuator is not automatically better.** With the amplifier's 10 kΩ input as the load, the output impedance stays low and the full depth remains usable; a buffer would bring treble roll-off and noise at a deep setting.

## Limitations

- Levels settle over a few seconds after a large volume change, more slowly at the quiet end. That is the nature of CdS cells.
- After a large temperature change outside the learned range, a resync gives the best accuracy; the attenuator says when.
- The calibration assumes the cells are reasonably similar. Very different cells will still work, but with less common depth.
- The design is tuned to my source, amplifier and cells. Other combinations should work, but they are untested.

## Acknowledgements

This project stands on the shoulders of the DIY audio community, in particular the long history of passive LDR attenuators such as the Lightspeed attenuator and the many discussions around them.

The firmware, its calibration and compensation methods, and much of the hardware analysis were developed in a long and very patient collaboration with **Claude**, Anthropic's AI assistant. From reading schematics and PCB files to tracking down subtle bugs in measurement logs, this project would not have come to life without that continuous support.

## License

*(to be added)*
