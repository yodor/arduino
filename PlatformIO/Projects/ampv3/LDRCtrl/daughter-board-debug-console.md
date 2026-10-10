# Daughter board bench console (DEBUG mode)

Send `DEBUG` on either link (USB or UART0) to enter the bench console; `EXIT` returns to
the strict master protocol (`daughter-board-uart-protocol-v2.md`). The console is small on
purpose: the calibration decides for itself, and everything worth looking at is in `DIAG`.

| Command | What it does |
|---|---|
| `CAL [R=<ohms>\|R=AUTO]` | The calibration, exactly as the master's `CAL`: a cell check against the stored calibration, then a full characterization (nothing stored, or the cells changed), a resync (same cells) or a re-solve + resync (same cells, new R). `R=` must lie in the measured range. |
| `FORGET` | Deletes both stored calibrations (memory and flash). The next `CAL` or boot runs a full characterization, Rtotal back to AUTO. |
| `VOL [<n>\|UP\|DOWN]` | Sets the volume (0-based, both channels) and always reports, per channel: the step, its dB (`TRANSPARENT` for the top step: series 150 Ω on both channels, shunt off, about −0.47 dB; `[muted]` if muted), the series and shunt duties and their target Rs/Rsh. With `RELAY ON` it also measures both cells live (after the 3 s settling time when the volume changed) and shows each as a ratio to its target plus the real dB; in audio mode the sensors are disconnected, so no live values. |
| `VOL INFO` | Every step in one table, L and R side by side: target dB, series duty and target Rs, shunt duty and target Rsh per channel (no live check), then — from the last `DIAG ALL` verify since boot — each channel's measured dB, L−R and the **match %** (the quieter channel's amplitude as a share of the louder's: 0.8 dB → 91%, 0.1 dB → 99%), with a mean/worst summary. Without a `DIAG ALL` since boot those columns show `--`. `>` marks the current step. It ends with the **health findings** (below). |
| `MUTE ON\|OFF` | The LDR mute (both channels). |
| `AMP_MUTE ON\|OFF` | The amplifier mute (refused during the boot hold). |
| `SET <channel> <duty>` | Raw duty 0–4095 on `SER_L`, `SER_R`, `SHUNT_L` or `SHUNT_R` — for meter work (driver currents, β). |
| `RELAY ON\|OFF` | Both relays. `ON` disconnects the audio (the sensors are only connected then); `OFF` restores the volume first, then reconnects. |
| `READ` | One sensor reading per side: Vref, Rs, Rsh (after `RELAY ON`). |
| `DIAG` | Instant snapshot, nothing driven: firmware and revisions, die temperature, driver duties, relays, volume, and per channel the stored calibration — Rtotal (AUTO or manual), measured range, depth, calibration history, cell fingerprint, curves and LUT — followed by the capability report (measured dark end of each series cell with its memory, curve ends, the Rtotal table with `RMIN`/`RMAX`/AUTO). |
| `DIAG ALL` | **The one block to send back.** The snapshot, then (amp muted throughout, ~8–9 min): the **cell check** (fingerprint now vs stored, report only), **verify** of every step on both channels at once, read 3 s after arriving — once the cells have settled — going up and down (real dB, error, live/target ratio of each cell, L−R balance, up-minus-down), the **dark-end walk** run again without saving and compared with the stored profile, and the **creep** test (loudest → quietest for 90 s, then → middle for 30 s). It ends with the **health findings** (below). Send everything from `DIAG ALL BEGIN` to `DIAG ALL END`. |
| `HELP` | The list above. |
| `EXIT` | Back to the strict protocol. |

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

## Health findings (end of `VOL INFO` and `DIAG ALL`)

Plain-language findings from the stored calibration and the last `DIAG ALL` verify, each
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
| `INFO no DIAG ALL since boot …` | — | run `DIAG ALL` for the measured checks |
