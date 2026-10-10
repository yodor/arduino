# Master Board <-> Daughter Board UART Protocol — revision 7

Supersedes the original `daughter-board-uart-protocol.md` (and revisions 2 and 3
of this file; the file name keeps its `-v2` for continuity). The daughter
reports its revision in every `STATUS` reply (`PROTO=7`) so a master can detect a
mismatch — **a master written for an earlier revision will misbehave** (see the
migration table at the end).

Implemented in `MasterLink` on the daughter; the UART0 link and the USB link run
the same code. Both default to this strict protocol, with `DEBUG`/`EXIT` as the
bench escape hatch.

## Design in one paragraph

Everything the master *sets* is a short command answered with `OK` or `ERR ...`.
Everything the master *reads* comes from **one** command, `STATUS`. A calibration
is started with `CAL`, which is always answered with a status
line: `BUSY` if it started, or `IDLE` with `CAL=ERR` and the reason if it did not.
The master then polls `STATUS` until it reads `IDLE`. There are no other getters
and no unsolicited messages.

## Physical link

UART0, pins/baud per each board's `Pins.hpp`/`Config.hpp` — **check current
values directly**, they have changed before. Plain ASCII, `\n`-terminated (the
daughter tolerates a stray `\r` before it), one command in flight at a time,
500 ms response timeout. **Every** command, including during a calibration, is
answered within that (see "While a calibration is running").

**Line length:** the longest reply is the `STATUS` line: about 130 characters
typically, at most about 145 without an error, and up to about **220** when it carries
an `ERR=` description. The master's receive line buffer must hold at least
**256 bytes** — a buffer of ~93 characters silently loses the tail (this happened:
the end of the line never arrived), and the error description is at the very end.

## Architecture

One RP2040 runs **both** channels (separate relays, I2C buses and calibration
data) behind a single UART. So `VOL` and `MUTE` apply to **both channels
together** — there is no per-channel addressing. `AMP_MUTE` is a third, independent
path: the amp's own 4N25 opto mute, unrelated to the LDR network's `MUTE`; either
can be engaged regardless of the other.

**Volume is numbered from 0** — the same numbers the bench console uses. `VOL 0` is
the quietest step. The LUT has 32 steps (0–31). Steps 0–30 are solved for constant
Rtotal; the top step, 31, is the **transparent** state — the series cell at 150 Ω on
both channels, shunt LED off — at about −0.47 dB (the source impedance and the series
resistance against the amp input; identical on L and R). 150 Ω rather than full brightness
costs ~0.08 dB but needs several times less LED current, so the cells come back from max
volume without upsetting the stereo balance for a minute. So the usable range is normally
**0–31**: read it from `STATUS` (`VOLMIN`/`VOLMAX`) rather than hard-coding it. "No calibration" is reported as
**-1**, never 0, because 0 is a real volume.

## Commands (setters)

| Command | Reply | Notes |
|---|---|---|
| `MUTE ON` | `OK` / `ERR ...` | Both channels. Instant (series→0, shunt→max); works even with no calibration loaded. |
| `MUTE OFF` | `OK` / `ERR ...` | Restores whatever was active (or last requested, if a `VOL` came in while muted) on both channels. |
| `VOL UP` | `OK` / `ERR step limit or no calibration loaded` | One LUT step louder, both channels. Stops at `VOLMAX`. |
| `VOL DOWN` | `OK` / `ERR step limit or no calibration loaded` | One LUT step quieter, both channels. Stops at `VOLMIN`. |
| `VOL <n>` | `OK` / `ERR ...` | `<n>` is the step number, **0-based**. Must be all digits: anything else, or a value outside 0–31, gets `ERR VOL out of range (0-31)`. `ERR step invalid or no calibration loaded` if that step has no valid solution on both channels (normally only 31) or a channel has no calibration. **A jump of more than one step auto-ramps through every intermediate step** (confirmed on real hardware: one big jump in one shot is an audible pop) — a couple of ms per step, worst case ~60–250 ms for a full-range jump. Inside the 500 ms timeout, but don't assume `VOL <n>` replies as instantly as `VOL UP`/`DOWN`. |
| `AMP_MUTE ON` / `AMP_MUTE OFF` (also `1` / `0`) | `OK` (during the boot hold: the `BUSY` status line, see "Boot hold") | Engages/disengages the amp's own mute (4N25 opto, independent of the LDR network) — **regardless of either channel's `MUTE` state**. Once the boot hold expires **the amp auto-unmutes on its own, with no command needed**; a master only needs `AMP_MUTE OFF` if it deliberately re-muted after boot. |
| `STATUS` | the status line | The only way to read anything. See below. Read-only. |
| `CAL` | the status line: `STATUS=BUSY` if started | **The** calibration. The daughter decides how much work it needs: a cell check against the stored calibration, then a full characterization (nothing stored, or the cells changed), a resync (same cells) or nothing more. See "Calibration". |
| `CAL R=<ohms>` | the status line: `STATUS=BUSY` if started, or `CAL=ERR` with the reason | The same, with a manual Rtotal. Accepted **only if** `RMIN <= R <= RMAX` as reported in `STATUS`; otherwise refused and nothing changes. With the same cells it re-solves from the stored measurements (no full scan). |
| `CAL R=AUTO` | the status line | The same, returning to the automatic Rtotal choice. |
| `DEBUG` / `EXIT` | `OK -- entering DEBUG mode. Send EXIT to return to the strict protocol.` / `OK -- back to strict protocol mode.` | Human-technician escape hatch — a real master never sends these. See the end of this document. |

A CAL command that cannot start is answered with a status line too, not a bare
`ERR`: `STATUS=IDLE`, `CAL=ERR` and a trailing `ERR=<reason>` (see "Calibration").
Any other unrecognized line gets `ERR unrecognized command` — including every command
that existed in earlier revisions and was removed (migration table at the end).

## STATUS

`STATUS` replies with **one line that always starts with `OK`**, followed by
`KEY=value` pairs separated by single spaces. Every key is always present, in this
order — **except `ERR`, which appears only when there is a reason to report, is always
last, and has a value that runs to the end of the line (it contains spaces)** — so a fixed-format scan of the
fixed keys works as well as a tokenizer. The order puts what a
person looks at first (`TEMP`, `UP`, volume, mute, `CAL`) and the constants last,
so a master that shows the raw line on a narrow display, or clips it, still gets
the useful part:

```
OK STATUS=IDLE TEMP=28.0 UP=3725 VOL=0 DB=-54.0 MUTE=0 AMP_MUTE=0 CAL=OK VOLMIN=0 VOLMAX=31 RANGE=54.0 R=95000 RMIN=10000 RMAX=127000 RAUTO=1 LASTCAL=FULL PROTO=7
```

A rejected `CAL R=…` on a unit whose calibration is fine — the reply carries the
reason, the volume fields keep their real values, and the next `STATUS` is back to
`CAL=OK`:

```
OK STATUS=IDLE TEMP=28.0 UP=3725 VOL=0 DB=-54.0 MUTE=0 AMP_MUTE=0 CAL=ERR VOLMIN=0 VOLMAX=31 RANGE=54.0 R=95000 RMIN=10000 RMAX=127000 RAUTO=1 LASTCAL=FULL PROTO=7 ERR=no ADS1115 detected on either channel
```

And the reply to a `CAL` that started — the same line while it runs:

```
OK STATUS=BUSY TEMP=28.5 UP=44 VOL=0 DB=-54.0 MUTE=0 AMP_MUTE=0 CAL=PROCESSING VOLMIN=0 VOLMAX=31 RANGE=54.0 R=95000 RMIN=10000 RMAX=127000 RAUTO=1 LASTCAL=FULL PROTO=7
```

| Key | Meaning |
|---|---|
| `STATUS` | `IDLE` — normal operation, every other field is current. `BUSY` — the daughter cannot take commands right now, for one of two reasons: **a calibration or trim is running** (started by the master, by a bench command, or automatically at power-up), in which case `CAL` reads `PROCESSING`; or **the boot mute hold is active**, in which case `ERR=boot mute hold active, <n>ms remaining` says how long to wait. While a calibration is running all other fields are **transient — ignore them** (the LUT may have been discarded for the new settings or be mid-rebuild); show "calibrating" and poll again. |
| `TEMP` | RP2040 die temperature in °C (1 decimal), a proxy for the board temperature. Informational, always meaningful. The LED driver stages drift with temperature (cold start vs warm moves the quiet end by ~11–14 duty counts), so a master can show or log it; absolute accuracy is only about ±2 °C, so use changes, not the absolute value. |
| `UP` | Whole seconds since the daughter powered up or reset (there is no wall clock on the daughter; for warm-up analysis time-since-power-on is what matters). Wraps after ~49.7 days. Log it next to `TEMP`. |
| `VOL` | Current volume, **0-based**: the number `VOL <n>` takes and the bench console's step number. Valid regardless of `MUTE` (mute doesn't change it). `-1` if there is no usable calibration. |
| `DB` | Gain at the current `VOL`, in dB (negative; 1 decimal). Handy for an on-screen readout. `0` if there is no usable calibration. Since calibration revision 2 this is the **real, loaded** gain: the LUT is solved against the divider as it is actually loaded (the amp board's 10 kΩ input and the ~400 Ω JFET buffer output — `AMP_INPUT_LOAD_OHMS` / `AUDIO_SOURCE_OHMS` in `Config.hpp`), not the bare Rsh/(Rs+Rsh) ratio. Before that, steps 24–29 were up to ~7 dB quieter than their `DB` said. Residual error is the PWM step size at the quiet end (about ±0.4 dB with the 100k bleeds), not the model. |
| `MUTE` | `1` if the LDR mute is engaged (what `MUTE ON` sets). |
| `AMP_MUTE` | `1` if the amplifier mute is engaged (what `AMP_MUTE ON` sets, and what the boot hold and relay clicks assert). **While any calibration runs (`CAL`, or the boot cell check / calibration) the daughter holds the amp muted for the whole run, so `AMP_MUTE=1` throughout — including in the reply that announces `CAL=PROCESSING` — and puts it back the way it was when the run ends** (an amp you had muted stays muted; one that was open opens again only after the cells are back at their step duties and the relay has returned). A first-boot calibration leaves it muted and the normal boot auto-release opens it afterwards. |
| `CAL` | `PROCESSING` — a calibration is running (only then; `STATUS=BUSY`). `OK` — both channels have a usable calibration. `NONE` — nothing is calibrated and nothing is wrong (a fresh unit). `ERR` — something is wrong, and `ERR=` says what: a rejected CAL request, or a channel that cannot run (`no ADS1115`, `no valid steps`, `not calibrated`). There is no separate "partial" value: one working channel out of two cannot do stereo, so it is an error that names the channel. After a calibration this is how the master learns whether it worked. |
| `VOLMIN`, `VOLMAX` | Lowest and highest `VOL` valid on **both** channels (0-based). Normally 0 and 31; step 31 is the transparent state (series 150 Ω on both channels, shunt off, about −0.47 dB). Use these to clamp the master's own volume UI. `-1` if there is no usable calibration. |
| `RANGE` | Total attenuation depth the LUT spans (dB from `VOLMIN` to the notional 0 dB the solved steps are spaced against; step 31, the transparent state, sits just below that at about −0.47 dB). Always computed from the measured floors; identical on both channels. `0` if there is no usable calibration. |
| `R` | The Rs+Rsh total, in ohms, that the LUT is solved for — chosen by the last full characterization (AUTO) or given by the master (`CAL R=`), the same on both channels. |
| `RMIN` | Smallest Rtotal this hardware supports, in ohms, as measured by the last full calibration (stereo-common). `0` if no calibration has measured it yet. |
| `RMAX` | Deepest Rtotal a master may still request, in ohms. **Measured by the last full characterization**: the deepest point at which both series cells settled during its dark-end walk, cut back to where the *measured* memory (level after a loud passage, settled) and the quiet-end step error stay within the RMAX limits (looser than AUTO's). Nothing about the cells is configured — swap a cell and the next `CAL` (or boot) re-characterizes it, and `RMAX`/AUTO follow. Recomputed from the stored measurements at every boot. `0` if not measured yet. |
| `RAUTO` | `1` if R was chosen by the calibration (AUTO), `0` if the master chose it with `R=`. |
| `LASTCAL` | What the last calibration did: `FULL` (full characterization: sweeps, dark-end walk, range, solve, trim), `RESYNC` (same cells: trim + settled quiet point), `RESOLVE` (same cells, new R: re-solved from the stored measurements, then resync), or `NONE`. |
| `PROTO` | Protocol revision of this daughter firmware (currently **7**). A master should refuse, or warn about, a daughter reporting a revision it was not written for. |
| `ERR` | **Present only when there is a reason to give, always the last key, and the value is free text running to the end of the line.** That is: when `CAL=ERR`, or when `STATUS=BUSY` because of the boot mute hold. Examples: `boot mute hold active, 3200ms remaining`, `no ADS1115 detected on either channel`, `R=200000 outside the achievable range 10000..170000`, `R: no ADS1115`, `L: no ADS1115; R: no valid steps`, `R: not calibrated`. Meant for display or logging, not for matching — except the leading words `boot mute hold active`, which a master may use to tell the hold from a calibration. |

Unknown keys must be ignored by the master, so fields can be appended in future
revisions without breaking an older master.

## Calibration

The master starts one with `CAL` (optionally `R=<ohms>` or `R=AUTO`), and **the reply
is a status line**. `STATUS=BUSY` with `CAL=PROCESSING` means the calibration started. If
it could not start, the reply is `STATUS=IDLE` with `CAL=ERR` and `ERR=<reason>`, and
**nothing was changed** — the existing calibration is untouched and still usable (`VOL`,
`VOLMIN`… keep their real values), and the next `STATUS` shows the true state again.

There is **one** calibration command, `CAL`, and the daughter decides what it needs:

1. **Cell check** (a few seconds, if a calibration is stored on both channels): each of
   the four cells is read at four bright duties (1023, 1474, 2457, 3276 counts) and
   compared with the fingerprint stored at its last full characterization. The bright
   end is the repeatable part of a vactrol: on the bench the same cells moved by at most
   1.5% from one day to the next, while different cells of the same series differ by
   12–50%. More than 5% at any duty = the cells have **changed**.
2. **Full characterization** — when nothing usable is stored, or the cells changed:
   sweeps; a **dark-end walk** that settles each series cell at a ladder of duties going
   darker (always arriving from the dark side, read only once it stops moving) and
   measures its **memory** at each point (how far the level settles off after a loud
   passage); from those `RMIN`, `RMAX` and the AUTO
   Rtotal; solve; trim; the quiet-end series point placed with settled readings; a new
   fingerprint. About **4–5 minutes**. Rtotal becomes AUTO unless `R=` was given (then it
   is clamped into the newly measured range if needed).
3. **Resync** — same cells, R unchanged: trim, then — after a 60 s hold at the quiet step —
   the settled quiet point, sought on both channels together until each is within 1.5%
   of target and the two agree within 1.5% (~0.13 dB): **stereo matching first**. Its
   correction is applied to every step on the settled part of the curve (about 3 minutes).
4. **Re-solve + resync** — same cells, `R=<ohms>` or `R=AUTO`: the LUT is solved again
   from the stored measurements at the new R, then resynced. No full scan.

At **boot** the same decision runs: nothing stored → full characterization; stored and
the cells match → the stored calibration is used as saved (no resync); the cells changed
→ full characterization. The master just sees `STATUS=BUSY` while any of this runs, and
`LASTCAL=` afterwards says which it was. Swap a cell for another of the same series and
the next boot (or `CAL`) finds it and re-characterizes.

These are rejected *before* anything is touched, each as `STATUS=IDLE CAL=ERR` with
`ERR=` set to:

| Reason in `ERR=` | When |
|---|---|
| `CAL args: nothing, R=<ohms> within RMIN..RMAX, or R=AUTO` | `CAL` with anything else after it |
| `R range unknown -- send CAL first` | `CAL R=…` before any calibration has measured the range |
| `R=<ohms> outside the achievable range <RMIN>..<RMAX>` | `CAL R=…` with a value outside the measured range |
| `no ADS1115 detected on either channel` | neither channel's sensor answers, so there is nothing to calibrate |

If only **one** channel's ADS1115 answers, the calibration still runs and calibrates
that channel alone; it ends with that channel usable and the other not, which reads
`CAL=ERR` with, for example, `ERR=R: no ADS1115`.

### Completion: poll STATUS

There is **no `CAL DONE` / `CAL FAIL`**. After the `BUSY` reply, poll `STATUS`
every few seconds (each poll is answered within ~10 ms even mid-calibration):

| `STATUS` reads | Meaning |
|---|---|
| `STATUS=BUSY CAL=PROCESSING` | still running — show "calibrating", ignore the other fields |
| `STATUS=IDLE CAL=OK` | finished, both channels usable |
| `STATUS=IDLE CAL=ERR ERR=…` | finished but a channel cannot run (e.g. `R: no ADS1115`, `L: no valid steps`); the reason is in `ERR=` |
| `STATUS=IDLE CAL=NONE` | nothing calibrated and nothing wrong (should not follow a calibration) |

Use a generous overall timeout (several minutes) purely as a guard against a
disconnected daughter, not as the expected completion signal. A full characterization takes about 4–5 minutes,
a resync about 2. During a full one the old calibration is discarded at the start, so the volume fields and the rest are
transient while it runs — which is why `CAL` simply reads `PROCESSING` and the other
fields are to be ignored until `STATUS` is `IDLE`.

### While a calibration is running

A calibration blocks the daughter's main loop for up to minutes, but it keeps the
master link alive: **any line the master sends meanwhile is answered within ~10 ms.**
`STATUS` gets the status line with `STATUS=BUSY`; every other command (`VOL`,
`MUTE`, `AMP_MUTE`, `CAL …`, even `DEBUG`) gets `ERR busy` and is **not** queued or
executed — resend it once `STATUS` reads `IDLE`. **Every link is serviced from the
very start** — UART and USB, including during the boot calibration — and **a link in
`DEBUG` mode is serviced too**: there, a line typed during a long bench operation
(`CAL`, `DIAG ALL`) gets `ERR busy`, or the status line for `STATUS`, and is dropped
rather than queued. Nothing can be aborted: a calibration or diagnostic always runs to
the end. Short waits (such as the relay click after a bench
`RELAY ON`) are not "busy" and answer nothing. `VOL`/`MUTE` sent during a
calibration would mean nothing anyway: the relays are energized and the drivers are
in sweep states throughout.

The same holds at **power-up**: after a flash, a PWM-resolution change or a corrupt
calibration file the daughter calibrates both channels before it is usable (a few
minutes), and the UART is already listening, so a master that boots first and asks
`STATUS` hears `STATUS=BUSY` instead of silence (on USB as well).

### State after a calibration (or at boot)

Both at boot and at the end of any calibration the daughter lands on a real, defined
volume with no command needed: the lowest valid LUT step (a resync instead returns
to the volume that was set), or the LDR's own hard mute if a channel's calibration
came back degenerate. A master doesn't need to send `VOL` after a calibration just to
get hardware into a sane state — `STATUS` already reports the real value.

## Stale calibrations are refused (hardware / calibration revision)

Every saved calibration is stamped with `CAL_HW_REV` (the hardware the duty→resistance
relationship depends on) and `CAL_ALGO_REV` (the meaning of the saved numbers). Calibration revision 2 introduced the loaded-divider solve, so every calibration taken before it is refused once and recalibrated on the next boot. A file whose
stamps differ from the running firmware's is **refused at boot**, exactly like a missing one:
the board runs the first-boot calibration (audio path disconnected, amp muted, `STATUS`
reports `BUSY` / `CAL=PROCESSING`, about 75–100 s) instead of playing through a LUT taken on
different hardware. From a master's point of view nothing new appears on the wire — it is the
same first-boot calibration it already has to wait out. Hardware revisions: 1 = original
driver (100k base bleeds), 2 = 1M base bleeds. Bump `CAL_HW_REV` in `Config.hpp` in the same
change as any hardware modification that alters the duty→resistance relationship, and flash
it with BOOTSEL after the board is modified (see the comment in `Config.hpp`).

## Boot hold (`MUTE_BOOT_HOLD_MS`) blocks EVERY command

During the boot-mute hold window the daughter refuses **every** strict-protocol
command — `VOL`, `MUTE`, `AMP_MUTE`, `CAL …`, `STATUS` — so nothing can disturb the
controlled startup sequence while it settles. The refusal is **not** an `ERR` line: it
is the regular status line, with **`STATUS=BUSY`** and
**`ERR=boot mute hold active, <n>ms remaining`**, for whichever command was sent:

```
OK STATUS=BUSY TEMP=27.7 UP=2 VOL=0 DB=-54.0 MUTE=0 AMP_MUTE=1 CAL=OK VOLMIN=0 VOLMAX=31 RANGE=54.0 R=95000 RMIN=10000 RMAX=127000 RAUTO=1 LASTCAL=FULL PROTO=7 ERR=boot mute hold active, 3200ms remaining
```

So a master treats it as any other "busy": wait `n` ms (or just poll `STATUS`) and
retry the command. The other fields are the real values — only the commands are held.
`CAL` is the true calibration state, not `PROCESSING` (nothing is being processed);
`PROCESSING` means a calibration is running. `DEBUG` is the one exception to the hold
(a technician must be able to reach the bench console at any time). A calibration
that is already running answers `STATUS=BUSY … CAL=PROCESSING` / `ERR busy` as above.

## `DEBUG` / `EXIT` — a human-technician escape hatch, not part of the protocol

Sending the exact line `DEBUG` switches the link into the bench console
(`daughter-board-debug-console.md`: `CAL` with verbose progress, `DIAG`, `DIAG ALL`, raw
`SET`/`RELAY`/`READ`, …) for field service, without needing USB. The daughter answers with the single line
`OK -- entering DEBUG mode. Send EXIT to return to the strict protocol.` — **no
banner**; type `HELP` for the command list. `EXIT` returns to the strict
protocol. **A real master's firmware should never send either word.** While in this
mode replies are verbose human-readable text — don't parse them as the strict
protocol. The console uses the same names as this protocol where they overlap
(`CAL`, `VOL`, `MUTE`, `AMP_MUTE`, and the same 0-based volume numbers).

## Master-side sketch

```cpp
struct Status { bool busy; int proto, vol, volMin, volMax, mute, ampMute, up, r;
                float tempC, db, range; String cal, error; };

// line = "OK STATUS=IDLE TEMP=28.0 UP=3725 VOL=0 ..." ; false if it isn't a status line
bool parseStatus(const String &line, Status &out) {
  if (!line.startsWith("OK STATUS=")) return false;
  int i = 3;                                   // skip "OK "
  while (i < (int)line.length()) {
    int sp = line.indexOf(' ', i); if (sp < 0) sp = line.length();
    int eq = line.indexOf('=', i);
    if (eq > i && eq < sp) {
      String k = line.substring(i, eq), v = line.substring(eq + 1, sp);
      if      (k == "STATUS")   out.busy    = (v == "BUSY");
      else if (k == "TEMP")     out.tempC   = v.toFloat();
      else if (k == "UP")       out.up      = v.toInt();
      else if (k == "VOL")      out.vol     = v.toInt();
      else if (k == "DB")       out.db      = v.toFloat();
      else if (k == "MUTE")     out.mute    = v.toInt();
      else if (k == "AMP_MUTE") out.ampMute = v.toInt();
      else if (k == "CAL")      out.cal     = v;        // "PROCESSING" | "OK" | "NONE" | "ERR"
      else if (k == "VOLMIN")   out.volMin  = v.toInt();
      else if (k == "VOLMAX")   out.volMax  = v.toInt();
      else if (k == "RANGE")    out.range   = v.toFloat();
      else if (k == "R")        out.r       = v.toInt();
      else if (k == "PROTO")    out.proto   = v.toInt();
      else if (k == "ERR") { out.error = line.substring(eq + 1); break; }  // free text to end of line
      // unknown keys: ignore
    }
    i = sp + 1;
  }
  return true;
}

// busy == true with out.error starting "boot mute hold active": the boot hold -- wait and
// retry. busy == true with out.cal == "PROCESSING": a calibration is running.
//
// start: send "CAL" (the daughter decides), "CAL R=<ohms>" within RMIN..RMAX, or "CAL R=AUTO"; the reply is a status
// line. busy == true: started. busy == false with cal == "ERR": it did NOT start, and
// out.error says why. Then, every few seconds: send "STATUS"; when busy == false the
// calibration is over and out.cal (and out.error) say whether it worked.
```

Intended boot flow: send `STATUS`. If `STATUS=BUSY` and `ERR=` starts with `boot mute
hold active`, wait the `n` ms it gives and ask again; if `STATUS=BUSY` and `CAL=PROCESSING`,
show "calibrating" and poll; if `IDLE`, adopt `MUTE`/`VOL`/`DB`/`TEMP` as the displayed state instead of
assuming defaults, clamp the volume UI to `VOLMIN`..`VOLMAX`, and if `CAL` is `ERR`
show `ERR=` to the user.

## Settings persistence

`R`, the measured range, the dark-end profiles, the cell fingerprints and the calibration
history are saved to flash with the calibration, so a power cycle does not revert them; at
boot the daughter checks the cells against the saved fingerprint and either uses the saved
calibration (same cells) or re-characterizes (changed cells) with no command needed. The
master sends `CAL` when it wants the cells resynced (for example after warm-up, or from a
"Recalibrate" menu item); the daughter decides how much work that takes.

## Accepted behaviour: levels settle over a few seconds

The LDR cells take a few seconds to settle after any volume change, and a volume change
always ramps, so the level slides to its target. A master should expect the attenuation
of a new step to be reached a few seconds after `VOL`, not instantly, and need not wait or
poll for it. The calibration limits the depth (`RMAX`, AUTO) to where the cells, once
settled, hold their level after loud listening and where the series curve is shallow
enough that the drivers' slow temperature drift moves the quiet-end level by about a dB at
most. With the same cells, a firmware whose limits put Rtotal elsewhere re-solves from the
stored measurements at the next `CAL` or boot (`LASTCAL=RESOLVE`), without a full scan.

## Changes in revision 7 (what the master must change)

- One calibration command: `CAL`, `CAL R=<ohms>` or `CAL R=AUTO`. `CAL FAST` and `CAL FULL`
  no longer exist; they get the ordinary `CAL args` error.
- `STATUS` adds `LASTCAL=FULL|RESYNC|RESOLVE|NONE` and reports `PROTO=7`.
- A full characterization takes about 4–5 minutes, a resync about 2 — poll `STATUS` until
  `IDLE`, as before.

## Known asymmetry

The master's own motor+relay volume/mute path is completely independent of this UART
link, per the original doc — nothing here changes that.
