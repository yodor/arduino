# Daughter Board — Bench `DEBUG` Console Reference

This is the human-technician command set, as distinct from the strict
master-facing protocol (`daughter-board-uart-protocol-v2.md`). It's
available over USB by default, and over UART0 by sending the line `DEBUG`
(switches that link out of the strict protocol; `EXIT` or `PROD` switches
it back — see that doc's "DEBUG / EXIT" section). Replies here are
verbose, human-readable text, not the strict wire format.

Commands are case-insensitive. One command per line, `\n`-terminated.

## While a long operation runs: one line at a time

`CALAUTO`, `CALTRIM`, `CALSCAN` and `SWEEP` run to completion — **none of them can be
aborted** (there is no keypress-to-stop any more). While one runs, the link keeps
answering: a line typed meanwhile gets `ERR busy` — or, for `STATUS`, the strict
protocol's status line (`OK STATUS=BUSY … CAL=PROCESSING`, so even in `DEBUG` mode
that one command prints the machine format while busy) — and is **dropped, not queued**.
So **do not paste a block of commands that starts with one of these**: in
`CALAUTO FAST` followed by `CALDUMP`, the `CALDUMP` is answered `ERR busy` and lost.
Send the next command once the operation prints its closing line. (Short waits, like the
relay click after `RELAY L ON`, do not answer busy, so pasting ordinary command blocks
still works.)

## Amp mute during calibration, and the saved-calibration stamp

- **Amp mute:** `CALAUTO`, `CALTRIM`, `CALSCAN` and the strict-protocol `CAL …` hold the amp
  muted for the whole run (audio is disconnected by the relay anyway) and restore it
  afterwards: a muted amp stays muted, an open one opens again once the relay is back.
  `AMP_MUTE OFF` cannot open it mid-calibration. `SWEEP` does not touch the amp mute.
- **Stamp:** each saved calibration records the hardware revision (`CAL_HW_REV`) and
  calibration revision (`CAL_ALGO_REV`) from `Config.hpp`. On boot a file with a different
  stamp is refused with a message such as
  `Saved calibration /cal_left.bin was taken on hardware rev 1, this firmware is built for
  rev 2 -- REFUSED`, and the board recalibrates from scratch. An accepted file prints
  `Calibration stamp accepted: hardware rev N, calibration rev M`.
- **After changing hardware:** power off, modify, bump `CAL_HW_REV`, flash with BOOTSEL held
  (the old firmware never runs on the modified board), power on — the first boot calibrates.
  Never flash a bumped revision onto the *unmodified* board and let it calibrate: the file
  would carry the new stamp for the old hardware.

## `[L|R]` is optional everywhere

Every command below that takes `[L|R]` can be given `L`, `R`, or nothing
at all — **omitting it applies the command to both channels**. This
matches how the strict protocol already behaves (its `VOL`/`MUTE`/`CAL`
always address both channels together); the bench console just makes
single-channel addressing available *in addition*, for isolating one
side during bring-up or debugging.

When both channels are targeted, commands that only *act* (`CALSTART`,
`VOL 10`, `MUTE ON`) reply once; commands that *report* something
per-channel (`CALDUMP`, `ADCREAD`, `CALRTOTAL` with no value) label each
channel's output `L:`/`R:` so you can tell them apart.

`CALAUTO` (and the strict protocol's `CAL FULL`/`CAL FAST`) run
**interleaved** when both channels are targeted — alternating between
channels point-by-point rather than finishing one fully before starting
the other — which roughly halves total sweep time. Single-channel
`CALAUTO L` or `CALAUTO R` runs that one channel alone, unchanged.

---

## Driver test (raw PWM, no calibration involved)

| Command | Effect |
|---|---|
| `SET <ch> <duty>` | Raw duty (0–4095) on one channel |
| `PCT <ch> <percent>` | Duty by percent on one channel |
| `ALL <duty>` | Raw duty on all 4 channels at once |
| `ALLPCT <percent>` | Duty by percent on all 4 channels |
| `SWEEP <ch> <start%> <end%> <step%> <dwell_ms>` | Ramp one channel, dwelling at each step |
| `STATUS` | Current duty/relay/mute/volume state of both sides, plus the computed attenuation depth and Rtotal, a `Volume:` summary line (same number as the master's `GET VOL`) and the RP2040 die temperature with uptime |
| `TEMP` (alias `UPTIME`) | RP2040 die temperature (proxy for board temperature) **and the time since power-up**, e.g. `Die temp: 28.0 C (RP2040 die)  uptime 01:02:05 (3725 s)`. `STATUS`, `CALTRIM` and `CALAUTO FAST`/`FULL` print the same pair at the start and end of a calibration, and the end line also gives the total duration (`... uptime 00:02:25 (145 s)  took 39.4 s`), so cold-vs-warm drift can be logged against temperature, time since power-on and how long the run took |
| `OFF` | All 4 channels to 0 |

`<ch>` is `SHUNT_L`, `SER_L`, `SHUNT_R`, `SER_R`, or the raw index `0`–`3`.

## Relay / amp / diagnostics

| Command | Effect |
|---|---|
| `RELAY [L|R] <ON|OFF>` | Energize/de-energize a calibration relay directly. Brackets the actual contact transition with a brief forced amp mute (pop suppression) either way. |
| `AMP_MUTE [ON|OFF]` | (renamed from `AMPMUTE` — now the same name as the master protocol's) Get/set the amp's own mute (4N25 opto) — independent of either channel's `MUTE`. Both directions refused during the boot hold window; the amp auto-unmutes on its own once that window expires, no command needed. |
| `DIAGLED <ON|OFF>` | Steady on/off for the diagnostic LED. No blinking — the GPIO toggling itself was confirmed audible as noise on real hardware, so this is a plain indicator you explicitly switch on only when you want it, off by default at boot. |

## Calibration

`CALAUTO` is the normal path. `CALDRIVE`/`CALPOINT`/`CALSOLVE` are for
manual point-by-point work — e.g. characterizing with a bench DMM instead
of trusting the ADS1115, or hand-correcting one bad point.

| Command | Effect |
|---|---|
| `CALSTART [L|R]` | Energize relay, clear curves |
| `CALEND [L|R]` | De-energize relay |
| `CALRESET [L|R]` | Clear curves + LUT, de-energize |
| `CALDRIVE [L|R] <SER|SHUNT> <duty>` | Drive a duty and leave it — so you can go measure with a meter |
| `CALPOINT [L|R] <SER|SHUNT> <duty> <ohms>` | Record a characterization point at that duty |
| `CALSOLVE [L|R] [steps] [rTotalOhms]` | Solve the LUT from whatever curve points exist so far (no sweep). The range is always computed — it is not an argument. With no `L|R` both sides share one common range; `L` or `R` alone uses that side's own range. An explicit `rTotalOhms` is applied as the channel's Rtotal, like `CALRTOTAL` first. |
| `CALDUMP [L|R]` | Print curves + solved LUT, preceded by a `Divider model: Rsrc=… Rload=…` line. The LUT's dB column is the **loaded** gain (amp input + buffer output impedance), so the Rs/Rsh targets differ from the bare-ratio values of earlier dumps at the loud steps (e.g. step 30 at 50k: Rs≈1.4k / Rsh≈48.6k instead of 9.1k / 40.9k). Compare dumps only within the same calibration revision. |
| `CALCAPS [L|R]` | **Read-only** capability report from the curves currently in memory (it does not sweep, so run `CALAUTO FULL` first). For a grid of Rtotal values plus the current one it prints depth per channel and stereo-common, the loud-end input impedance the source sees, max gain (insertion loss), the quiet-end step size in %/count, the worst-case ±half-step gain error, and the hysteresis prior — then `RMIN`, the curve-supported maximum, and the **AUTO pick**: the deepest Rtotal whose hysteresis prior and step error stay under `CAPS_AUTO_MAX_HYST` / `CAPS_AUTO_MAX_QUANT_DB` (`Config.hpp`, tunables). Rows marked `beyond curve` / `*extrap` go past the curve's consistent (non-lag) data, so treat them as estimates. The hysteresis column is a prior from earlier CALSCAN data (up to 100k) and must be re-measured after any hardware change. Nothing here alters calibration. |
| `CALTRIM [L|R]` | Verify + trim every LUT step against a live measurement, re-saves when done. The `old->new` columns now show the real change (earlier builds printed new->new, so every trim looked like it moved nothing). Starts by settling the channel at step 0 for 8 s so measurements begin from rest, and restores the current volume before the audio reconnects. |
| `CALAUTO [L|R] <FULL|FAST>` | Automatic sweep + solve, no manual points. Auto-saves to flash, returns to audio mode. Both channels (no `L|R`) run interleaved. Skips as a no-op if no ADS1115 is detected on that channel. **FULL** = coarse sweep + adaptive knee refinement + solve + trim (so a separate `CALTRIM` afterwards is no longer needed). **FAST** = trim-only drift touch-up of the saved calibration, no sweep. During a `FULL` sweep the non-swept element is held at `CAL_BRIGHT_HOLD_DUTY` (80% of full scale, not 100%: the cells bottom out earlier and extra current only heats them). If the coarse list's first point (15% duty) already reads below 2× Rtotal on either channel, the knee is below the list and the sweep **extends downward automatically** (up to 4 rounds of 5 points, printed as `-- extending low end --`); with the original driver nothing triggers and the sweep is unchanged. |
| `CALSCAN [L|R] [start end]` | Diagnostic: fine two-way (ascending + descending) scan of a raw-duty window (default **900–1148**, tuned to the original 100k-bleed driver — after changing the bleeds, pass the window around the new knee, e.g. `CALSCAN L 560 700`; the window is trimmed to a multiple of the 4-count step), 5 raw samples per point. The non-scanned element is held at `CAL_BRIGHT_HOLD_DUTY`. Does **not** touch saved curves/LUT — purely for characterizing hysteresis. Paste the output back for analysis. |
| `CALREF [L|R] [ohms]` | Get/set Rref used for that channel's readings |
| `CALRTOTAL [L|R] [ohms]` | Get/set the Rs+Rsh target (default 50000Ω) |
| `CALSAVE [L|R]` | Manually save current curves+LUT to flash |
| `CALLOAD [L|R]` | Manually (re)load from flash |

## Volume (uses the solved LUT)

| Command | Effect |
|---|---|
| `VOL [L|R] [<step>]` | Apply LUT step directly (step 0 = quietest; **the same numbering as the master's `VOL`/`GET VOL`**). Jumps of more than one step auto-ramp. **With no step it shows the current volume**, e.g. `VOL=8 (-40.04 dB)` (or one `L:`/`R:` line each if they differ; `[muted]` is appended when muted). |
| `VOLUP [L|R]` | One step louder |
| `VOLDOWN [L|R]` | One step quieter |
| `MUTE [L|R] <ON|OFF>` | Fast mute/unmute — works even before any calibration is loaded |

## ADS1115 / I2C

The I2C bus for a channel is deactivated except while that channel's
relay is energized (calibration mode) — `ADCREAD` will `ERR` otherwise.

| Command | Effect |
|---|---|
| `I2CSCAN [L|R]` | Scan for ACKing addresses — works regardless of relay state |
| `ADCREAD [L|R]` | Read Vref/Rs/Rsh — requires the relay energized first |

---

## Sample sessions

### 1. Clean-state full calibration, both channels, interleaved

```
CALRTOTAL 10000
CALAUTO FULL
```
Progress prints one line per duty point with *both* channels' readings
side by side:
```
--- DualCal: series sweep (interleaved, shunt held bright) ---
  duty=1024  L=14877.0(44ms)  R=11375.4(52ms)
  ...
--- DualCal: shunt sweep (interleaved, series held bright) ---
  ...
--- DualCal done: L solved 31/32, R solved 31/32 ---
Saved: L=OK R=OK
```
Then bring it up:
```
VOL 16
STATUS
```

### 2. Manual point-by-point characterization (one channel, DMM in hand)

Useful if you want to hand-correct a single bad point, or don't trust
the ADS1115 for a particular run:
```
CALSTART L
CALDRIVE L SER 1024
```
— go read the DMM across the series LDR —
```
CALPOINT L SER 1024 14877
CALDRIVE L SER 1064
```
— read again —
```
CALPOINT L SER 1064 11375
...
CALSOLVE L
CALDUMP L
CALEND L
```

### 3. Diagnostic hysteresis scan (doesn't touch saved calibration)

```
CALSCAN L
```
Prints ascending and descending resistance at every 4-duty-count step
through the known steep/volatile zone, 5 raw samples per point, for
both series and shunt. Safe to run any time — existing LUT is
untouched. Paste the full output back for analysis.

### 4. Quick volume/mute smoke test after calibration

```
STATUS
VOL            (no step: shows the current volume)
VOL 0
VOL 30
VOL 31         (expect ERR: the top step is out of range)
MUTE ON
STATUS
MUTE OFF
VOLUP
VOLDOWN
```

### 5. Re-targeting RTOTAL without re-characterizing

If curves already exist from a prior `CALAUTO`, changing `RTOTAL` or
`MODE` re-solves immediately from the existing curves — no new sweep
needed:
```
CALRTOTAL 5000
CALDUMP
```
(compare against the `RTOTAL 10000` dump to judge the tradeoff directly,
without waiting through another full sweep)

### 6. Switching a UART0 link between strict protocol and bench console

From the master's (or a terminal emulating the master's) side of UART0:
```
DEBUG
```
— the daughter answers with the single line `OK -- entering DEBUG mode. Send EXIT to
return to the strict protocol.` (no banner — type `HELP` for the list); the link is now
in bench-console mode and every command above is available —
```
EXIT
```
— link is back to the strict protocol (`CAL FAST`/`CAL FULL`, `VOL`, `MUTE`,
`AMP_MUTE`, `STATUS` from `daughter-board-uart-protocol-v2.md`). A real master should
never send `DEBUG` itself; this is purely for a technician with a
terminal on the same physical wire.
