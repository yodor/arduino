# Attenuator bench console (DEBUG mode)

Send `DEBUG` on either link (USB or UART0) to enter the bench console; `EXIT` returns to
the strict controller protocol (`ldr-attenuator-protocol.md`). The console is small on
purpose: the calibration decides for itself, and everything worth looking at is in `REPORT`.

| Command | What it does |
|---|---|
| `CAL [R=<ohms>\|R=AUTO]` | The calibration, exactly as the controller's `CAL`: a cell check against the stored calibration, then a full characterization (nothing stored, or the cells changed), a resync (same cells) or a re-solve + resync (same cells, new R). `R=` must lie in the measured range. |
| `CAL FULL` | Always a full characterization, even with known cells; AUTO chooses Rtotal. The learning, the model and the settings are kept (unlike `FORGET CAL`). |
| `FORGET CAL` / `FORGET COMP` / `FORGET ALL` | `CAL`: both stored calibrations and what belonged to them (reference rail, source/load model, environment at calibration, last match); the next `CAL` (or boot) runs a full characterization. `COMP`: the thermal learning log; the compensation switches off until the next resyncs relearn it. `ALL`: both. A plain `FORGET` only shows these options. The same commands exist on the controller protocol (revision 9). |
| `VOL [<n>\|UP\|DOWN]` | Sets the volume (0-based, both channels) and always reports, per channel: the step, its dB (`TRANSPARENT` for the top step: series 150 Ω on both channels, shunt off, about −0.47 dB; `[muted]` if muted), the series and shunt duties and their target Rs/Rsh. With `RELAY ON` it also measures both cells live (after the 3 s settling time when the volume changed) and shows each as a ratio to its target plus the real dB; in audio mode the sensors are disconnected, so no live values. |
| `TEMP` | The temperatures and the rail: RP2040 die, the driver block (NTC on the aluminium block; a faulty NTC shows its state: `OPEN`, `SHORT`, `BAD`), the 3.3 V rail (via the TL431), the change since the last calibration, and the rail and thermal compensation in effect. The thermal compensation applies its full correction within 30 C of the calibration temperature, says when it is extrapolating beyond the span it has learned (a `CAL` there teaches it the new range), and switches off with the reason if the NTC is faulty. When a resync would help, a "resync recommended:" line says why (the same hint the controller reads in `ENV` as `RESYNC=`/`WHY=`). |
| `LEARN` | The thermal learning log: one line per resync with each channel's knee shift (PWM counts) next to how much the driver block and the 3.3 V rail changed since the previous calibration; resyncs whose quiet-point seek failed, and the catch-up resync after each, are marked and left out. Then the fit per channel (knee shift = k x change) against the block and the rail. The compensation uses the block fit once it is trusted (3+ resyncs, 0.5 C spread, R2 >= 0.6). `FORGET COMP` starts the log over. The log keeps a spread over the temperatures seen, not just the newest: whenever it passes 48 entries it keeps the newest 12 plus the newest two in every 1 C band of block temperature (failed seeks and their catch-ups are dropped), so a winter resync still counts in summer. |
| `COMP` / `COMP ON` / `COMP OFF` | Thermal compensation from the learned fit: each side's duties get k x (driver-block temperature now - at the last calibration), from the block's 1-minute average; k comes from `LEARN`'s fit vs the block, used once trusted (3+ resyncs, 0.5 C spread, R2 >= 0.6). Off during every calibration (resyncs keep measuring the raw shift, so the learning refines itself). `COMP OFF` for an A/B listen; not stored, boots ON. `TEMP` and `REPORT` show it. |
| `SET SRC <ohms>` / `SET LOAD <ohms>` | The audio model: the source (splitter) output impedance, and the load after the attenuator (amplifier input; `0` = unloaded). Stored in flash. If the calibration was solved for other values it becomes `CAL=STALE` (LDR mute on, volume refused) until `CAL` re-solves; setting the solved values back undoes that. Same commands on the controller protocol (revision 12). |
| `MODEL` | The source and load in use, what the calibration was solved for, and the `CAL=` state. |
| `SETTINGS` | The stored settings, `settings.txt` (`key=value`): `source_ohms` (`SET SRC`), `load_ohms` (`SET LOAD`), `auto_pref` (`SET AUTO`). Unknown keys are kept. The first boot with this firmware takes over the values from the older files (`/audio_model.txt`, `/auto_pref.txt`) and removes them. |
| `EXPORT [ALL\|SETTINGS\|CAL\|LEARN]` | A backup of the stored files as one text block, from `===== EXPORT BEGIN` to `===== EXPORT END`. `SETTINGS`: `settings.txt`; `CAL`: both calibrations and the state recorded with them (model, quiet-point match, reference rail, temperatures); `LEARN`: the learning log; `ALL` (default): everything. Text files appear as readable lines (each prefixed with `\|`), the binary calibration files as base64; every file carries its size and CRC-32. Save the block in a text file on your computer. |
| `IMPORT` | Restore: send `IMPORT`, then paste a saved `EXPORT` block. Nothing is written until every file has been received and verified (sizes, checksums, allowed file names); then each file is replaced atomically and the attenuator restarts to load them. A damaged or incomplete paste changes nothing. `IMPORT CANCEL` or `EXIT` stops; a minute without data abandons it. The calibration files still pass their own hardware and revision checks when loaded. |
| `SET AUTO SNAPPY` / `BALANCED` / `DEEP` | AUTO's depth-versus-settling preference, stored: how deep the dark-end walk measures, AUTO's memory and slope limits, and a cap on its pick (600 k / 1.0 M / 1.6 M). Prints the preference, AUTO's new pick and whether the stored walk reaches deep enough (if not: `FORGET CAL`, `CAL`). In AUTO mode a changed pick makes the calibration `STALE` until `CAL`. Same command on the controller protocol (revision 16). |
| `VER` / `FW` | Firmware version and release note, build date, protocol, hardware and calibration revisions, chip and silicon revision, CPU clock, flash size and unique ID, board, Arduino-Pico core and Pico SDK versions. The controller reads the same as one line (`VER`, protocol 15). |
| `BAL` / `BAL <n>` | Balance, n = -16..+16 in 0.25 dB units (negative lowers R, positive lowers L); shows the attenuation of each channel. Not stored. Same command on the controller protocol (revision 18). |
| `VOL INFO` | Every step in one table, L and R side by side: target dB, series duty and target Rs, shunt duty and target Rsh per channel (no live check), then — from the last `VERIFY` verify since boot — each channel's measured dB, L−R and the **match %** (the quieter channel's amplitude as a share of the louder's: 0.8 dB → 91%, 0.1 dB → 99%), with a mean/worst summary. Without a `VERIFY` since boot those columns show `--`. `>` marks the current step. It ends with the **health findings** (below). |
| `MUTE ON\|OFF` | The LDR mute (both channels). |
| `AMP_MUTE ON\|OFF` | The amplifier mute. ON from power-up until changed (no boot hold any more). |
| `SET <channel> <duty>` | Raw duty 0–4095 on `SER_L`, `SER_R`, `SHUNT_L` or `SHUNT_R` — for meter work (driver currents, β). |
| `RELAY ON\|OFF` | Both relays. `ON` disconnects the audio (the sensors are only connected then); `OFF` restores the volume first, then reconnects. |
| `READ` | One sensor reading per side: Vref, Rs, Rsh (after `RELAY ON`). |
| `CAL VERIFY` | Measures how well the stored table holds (about 8 minutes): the cell check, every step up and down (level, error, each cell against its target), the dark-end walk again compared with the stored one, creep after volume jumps, and the health findings. **Each run is stored in flash** (the last 4, surviving restarts) and **compared with the previous run** (`CHANGES`), and ends with a `VERDICT`: `OK`, `RESYNC` (the table is off; a resync fixes it) or `CHECK` (L and R off differently, or a shape change). A `RESYNC` verdict also raises the resync hint (`WHY=VERIFY`) until the next calibration. The controller can start the same run (protocol 20). Was `DIAG ALL`. |
| `VERIFY [HISTORY]` | The stored runs, one line each: when, firmware, block temperature, compensation offsets, calibration, Rtotal, step rms, how far the cells moved. |
| (automatic) | After every `CAL VERIFY` the run's walk is adopted as the stored walk -- what AUTO, the depth reach, the deep-step refinement and the comparisons use -- unless the verdict is `CHECK` (maybe a fault: left for a person), AUTO would then choose another Rtotal (left to `CAL FULL`), or the walk does not reach a manual Rtotal. The step readings are deliberately **not** fed back: they are taken 3 s after arriving, before the cells fully settle; the resync remains the tool for the table. |
| `REPORT` | The compact summary to send for analysis (about 70-90 lines instead of about 1000): firmware and uptime, state, temperatures, compensation and resync hint, the stored table, the last calibration (kind, duration, quiet-point seek, match, hold, deep-step corrections, guards), the last cell check, the last dark-end walk with memory peaks (and its comparison after a `VERIFY`), the verify as error columns with rms and worst values, the creep condensed to settling time, peak and end, and the health findings. It prints what the last `VERIFY` and calibration recorded, so run those first; nothing is measured by `REPORT` itself. |
| `DUMP` | The stored calibration in full: firmware, state, both channels' curves and step tables, the capabilities per Rtotal. For deep debugging. Was `REPORT`. |
| `HELP` | The list above. |
| `EXIT` | Back to the strict protocol. |


Typing: Backspace and Delete edit the line before Enter (both codes work, whichever your terminal sends).

## Concepts

The attenuator works in stages. Each has one name, used the same way in the firmware, its
output and these documents. How the stages work together -- and why temperature, slow ageing,
cell memory and the impedance model are handled differently -- is described in the README,
"The big picture".

| Term | What it is | Started by |
|---|---|---|
| **Characterization** | Measures the cells from scratch: the series and shunt curves, the dark-end walk (how deep each series cell settles, and its memory after a loud passage). Chooses Rtotal and builds the step table. A few minutes. | `CAL` when nothing is stored or the cell check finds the cells changed; `CAL FULL` to force it |
| **Resync** | Re-trims the stored table to the cells as they are now (quiet-point seek, deep-step refinement, trims). About 2-3 minutes. Each resync is one entry in the learning log. | `CAL` with known cells; recommended by the resync hint |
| **Re-solve** | A new Rtotal from the stored curves, without measuring the cells again, followed by a resync. | `CAL R=<ohms>`, `CAL R=AUTO`, or a changed `SET AUTO` / `SET SRC` / `SET LOAD` |
| **Cell check** | A quick bright-end fingerprint compared with the stored one (temperature-aware): are these the cells the table was made for? | Every power-up, every `CAL`, `VERIFY` |
| **Learning** | From the resyncs, how the cells' operating point follows the driver block's temperature. Keeps a spread over the temperatures seen. | Runs by itself; `LEARN` shows it |
| **Compensation** | Applies what was learned, continuously, so the table stays right as the temperature changes -- and resyncs become rare. | Runs by itself; `COMP ON/OFF` |
| **Resync hint** | Says when a resync would help (the temperature moved further than the learning covers, or the last seek failed); waits while the device is warming up. | `ENV` (`RESYNC=`, `WHY=`), `TEMP`, `REPORT` |
| **AUTO preference** | How AUTO trades depth against settling speed: `SNAPPY`, `BALANCED`, `DEEP`. | `SET AUTO` |
| **Verify** | `CAL VERIFY`: measures how well the stored table holds: the cell check, every step up and down, the dark-end walk again (compared with the stored one), creep after volume jumps, and the health findings. About 8 minutes; changes nothing in the table (its walk is adopted when safe). Stored and compared with the previous run; a `RESYNC` verdict raises the resync hint. | `CAL VERIFY` (console and protocol) |
| **Report** | The compact summary for analysis: state, the last calibration, the last verify. Measures nothing itself. | `REPORT` |
| **Settings** | The user's choices, in one readable file `settings.txt` (`key=value`): source and load impedance, AUTO preference. | `SET SRC`, `SET LOAD`, `SET AUTO`; `SETTINGS` shows them |
| **Backup** | All stored files as one text block (`EXPORT`), restorable on a freshly flashed board (`IMPORT`): settings, calibration, learning -- together or separately. | `EXPORT`, `IMPORT` |
| **Dump** | The stored calibration in full: curves, step tables, capabilities per Rtotal. For deep debugging. | `DUMP` |


## Reading VERIFY's walk lines

The walk is measured with the temperature compensation active, so every comparison is made at
equal **effective drive**: the commanded duty plus the compensation offset of that moment.

- **cells moved** -- how many counts the cells' operating point moved since the stored walk (or,
  under `CHANGES`, since the previous `VERIFY`), averaged over the depths both walks measured.
  Negative: the cells now need less LED drive for the same resistance (typically warmer).
- **spread** -- the range of that movement over the depths. Small (about 2-3 counts or less): a
  **uniform shift**, which is what temperature does. Large: the **shape changed** -- some depths
  moved more than others.
- **table + compensation -> residual** -- the table's accumulated shift (from the resyncs) plus
  today's compensation offset is what the firmware supplies; minus what the cells moved is the
  residual. Near 0: the model fits. Positive: the cells are driven brighter than they need,
  so the quiet and middle steps play louder than labelled; negative: quieter.
- **per C of block change** (under `CHANGES`) -- a uniform shift divided by the temperature
  change between the two runs: an independent measurement of counts per C, next to the learned
  slope the compensation uses. More than 20 % apart means the compensation over- or
  under-corrects.
- **L and R moved differently** -- a change in one channel only: that cell, or its LED driver.

## What CAL does

1. **Cell check** (a few seconds, when a calibration is stored on both channels): each of
   the four cells is read at four bright duties (1023, 1474, 2457, 3276) and compared with
   the fingerprint stored at its last full characterization. More than 5% at any duty =
   the cells changed. (Bench data: the same cells moved ≤1.5% from one day to the next;
   different cells of the same series differ by 12–50%.)
2. **Full characterization** — nothing usable stored, or the cells changed (~4–5 min):
   sweeps of both cells; the **dark-end walk** (each series cell settled at whole-count
   duties going darker, arriving from the dark side, read once three one-second readings
   agree within 2%; at each point the **loudest real state** (the transparent top step: series 150 Ω, shunt off, 10 s) and back, read 5 s later — the **memory**: how far the quiet level sits off after max volume, once settled; the settled points replace
   the sweep's lag-biased dark end); `RMIN`, `RMAX` and the AUTO Rtotal from those
   measurements; solve; quick trim of the shunts and the brighter series steps; the
   quiet-end series point placed with settled readings; a new fingerprint and history.
3. **Resync** — same cells (~3 min): trim (each step within 1.5%), a 60 s hold at the
   quiet step (the trim ends at the loud steps, and a cell needs a while to recover), then
   the settled quiet-end point, sought on both channels together until each is within
   1.5% and the two agree within 1.5% (~0.13 dB) — **stereo matching first**; the log ends
   with `stereo match at the quiet point: R is x.xx dB quieter/louder than L`. The knee of these
   cells shifts by fractions of a count to a couple of counts between sessions (driver
   Vbe, the 3.3 V rail behind the PWM) while its shape stays; so the seek's correction, in
   counts, is applied to every step whose series duty comes from the settled curve, not
   just to the quietest steps (`knee shift … counts applied to N deeper-curve steps`).
4. **Re-solve + resync** — same cells, `R=<ohms>` or `R=AUTO`: solved again from the stored
   measurements at the new R, then resynced.

At boot the same check runs: same cells → the stored calibration is used as saved;
changed cells or nothing stored → full characterization.

The limits in `Config.hpp` (`CAPS_AUTO_*`, `CAPS_RMAX_*`) are policy — how much error you
accept — not hardware data: everything about the cells is measured.

## Accepted behaviour

Vactrols do not change instantly. After any volume change the cells take a few seconds
to settle, so for those seconds the level (and Rtotal) is on its way to the final value;
and because volume changes always ramp, the cells slide towards the target rather than
step. This is accepted for this design. What the calibration does guard against is the
level the cells settle **at**: after loud listening a cell can settle off its quiet-end
target (its *memory*), and that grows with depth. The dark-end walk measures it at every
depth. The larger effect is slower: the drivers drift by millivolts with temperature (the
board warms during a calibration and cools after it), and the cell moves by the series
curve's slope times that drift — a slope that rises steeply with depth. So AUTO and
`RMAX` are also limited by the measured slope at the quiet-end point (`CAPS_AUTO_MAX_SLOPE`
0.30, `CAPS_RMAX_MAX_SLOPE` 0.45 ln-R per count; bench: 95k ±1.5 dB, 344k ±3 dB, 552k
±5–6 dB). On this hardware that puts AUTO near 90k (~59 dB). If a firmware changes these
limits, the next `CAL` or boot re-solves from the stored measurements — no full scan.
Verify reads every step after 3 s, i.e. settled.

## Health findings (end of `VOL INFO` and `VERIFY`)

Plain-language findings from the stored calibration and the last `VERIFY` verify, each
naming the element and what to try next. `OK no findings` when there is nothing.

| Finding | From | Suggests |
|---|---|---|
| `WARN L/R series: sits N% above/below target …` — a series cell more than 12% off where it sets the level, or the worse of two series more than 8% apart (with the resulting L−R in dB) | last verify, quiet and middle steps | `CAL` once warm; if it returns, clean/inspect that series driver (PNP, 1M bleed, base traces — flux leaks nanoamps there), then swap the LDR boards between channels to tell cell from driver |
| `INFO both series sit about N% above/below target together …` — both channels off by the same amount (a common drift since the last `CAL`, usually warm-up): the levels moved, the L/R balance did not | last verify | `CAL` once the board is warm — nothing to fix in either driver |
| `WARN L/R shunt: sits N% …` — a shunt more than 12% off at the steps where it sets the level (Rsh ≤ 2k) | last verify | the same, for the shunt driver |
| `WARN L/R series: memory xN …` — the quiet level sits off after max volume beyond the AUTO limit, near the operating depth | the dark-end walk | compare channels; a spare cell in that position tells cell from driver |
| `WARN L/R series: needed N s to settle …` (≥ 10 s) / `did not settle beyond Nk` | the dark-end walk | — |
| `INFO AUTO is set by the L/R series: …` — which channel, and which limit (slope, memory, step error), limits the depth | the capability model, per channel | — |
| `WARN die N C now, M C at the last CAL` (≥ 3 °C) | die temperature | `CAL` |
| `INFO no VERIFY since boot …` | — | run `VERIFY` for the measured checks |

### Sense wiring per LDR board layout

`REPORT` shows which ADS1115 input carries each board's top junction: the original LDR board has
it on A1 (A3 = middle junction), the second edition on A3 (A1 = middle junction). `Config.hpp`
sets the starting assumption per channel (`LDR_L_TOP_ON_A3`, `LDR_R_TOP_ON_A3`, default: second
edition). A reading that shows the series drop with the wrong sign switches the mapping by itself
and prints `[sensor L] ADS inputs: top junction is on ... -- mapping switched` once, so a mixed
or swapped pair of boards still measures correctly.
