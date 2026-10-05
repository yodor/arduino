# Daughter Board — Bench `DEBUG` Console Reference

This is the human-technician command set, as distinct from the strict
master-facing protocol (`daughter-board-uart-protocol-v2.md`). It's
available over USB by default, and over UART0 by sending the line `DEBUG`
(switches that link out of the strict protocol; `EXIT` or `PROD` switches
it back — see that doc's "DEBUG / EXIT" section). Replies here are
verbose, human-readable text, not the strict wire format.

Commands are case-insensitive. One command per line, `\n`-terminated.

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

`CALAUTO` (and the strict protocol's `CAL`/`CAL FULL`/`CAL FAST`) run
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
| `STATUS` | Current duty/relay/mute/volume state of both sides, plus the computed attenuation depth and Rtotal |
| `OFF` | All 4 channels to 0 |

`<ch>` is `SHUNT_L`, `SER_L`, `SHUNT_R`, `SER_R`, or the raw index `0`–`3`.

## Relay / amp / diagnostics

| Command | Effect |
|---|---|
| `RELAY [L|R] <ON|OFF>` | Energize/de-energize a calibration relay directly. Brackets the actual contact transition with a brief forced amp mute (pop suppression) either way. |
| `AMPMUTE [ON|OFF]` | Get/set the amp's own mute (4N25 opto) — independent of either channel's `MUTE`. Both directions refused during the boot hold window; the amp auto-unmutes on its own once that window expires, no command needed. |
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
| `CALDUMP [L|R]` | Print curves + solved LUT |
| `CALTRIM [L|R]` | Verify + trim every LUT step against a live measurement, re-saves when done. The `old->new` columns now show the real change (earlier builds printed new->new, so every trim looked like it moved nothing). Starts by settling the channel at step 0 for 8 s so measurements begin from rest, and restores the current volume before the audio reconnects. |
| `CALAUTO [L|R] <FULL|FAST>` | Automatic sweep + solve, no manual points. Auto-saves to flash, returns to audio mode. Both channels (no `L|R`) run interleaved. Skips as a no-op if no ADS1115 is detected on that channel. **FULL** = coarse sweep + adaptive knee refinement + solve + trim (so a separate `CALTRIM` afterwards is no longer needed). **FAST** = trim-only drift touch-up of the saved calibration, no sweep. |
| `CALSCAN [L|R]` | Diagnostic: fine two-way (ascending + descending) scan of the ~22–28% duty window, 5 raw samples per point. Does **not** touch saved curves/LUT — purely for characterizing hysteresis. Paste the output back for analysis. |
| `CALREF [L|R] [ohms]` | Get/set Rref used for that channel's readings |
| `CALRTOTAL [L|R] [ohms]` | Get/set the Rs+Rsh target (default 10000Ω) |
| `CALMODE [L|R] [RTOTAL\|FIXEDSERIES]` | Get/set attenuation mode |
| `CALSERIESDUTY [L|R] [duty]` | Get/set the fixed series duty (`FIXEDSERIES` mode only) — affects future solves only, re-run `CALAUTO`/`CALSOLVE` after changing it |
| `CALSAVE [L|R]` | Manually save current curves+LUT to flash |
| `CALLOAD [L|R]` | Manually (re)load from flash |

## Volume (uses the solved LUT)

| Command | Effect |
|---|---|
| `VOL [L|R] <step>` | Apply LUT step directly (step 0 = quietest) |
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
CALMODE RTOTAL
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
VOL 1
VOL 32
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
— link is now in bench-console mode, every command above is available —
```
EXIT
```
— link is back to the strict protocol (`CAL`, `VOL`, `MUTE`, `GET VOL`,
etc. from `daughter-board-uart-protocol-v2.md`). A real master should
never send `DEBUG` itself; this is purely for a technician with a
terminal on the same physical wire.
