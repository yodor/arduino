# Master Board <-> Daughter Board UART Protocol — v2 (finalized)

Supersedes the original `daughter-board-uart-protocol.md`. That version was
written before the daughter board's actual architecture and calibration
design were settled; this one reflects what's been built and decided since,
including the parts the original doc explicitly flagged as unsettled.

**Status: implemented and exercised on real hardware, on both links.** The
UART0 handler (`MasterLink`) implementing this protocol exists and runs
the same code path as USB — both links default to this strict protocol,
with `DEBUG`/`EXIT` as the bench escape hatch on either (see below). `CAL
MODE`/`CAL RTOTAL`/`CAL FULL` have been run for real, in the final
mounted device, with successful attenuation and both-channel calibration.
**What hasn't happened yet is an end-to-end test with the actual master
board** sending these commands over its own UART0 — everything so far has
been a human typing the strict commands directly over a terminal.

## What changed from v1, and why

**One controller, two channels, not two independent daughter boards.**
The original doc assumed "the daughter board" meant one LDR board on one
link. The actual build is one RP2040 controller running **both** L and R
channels (separate relays, separate I2C buses, separate calibration data),
with a single UART0 link to the master. Consequently:

- `VOL`, `MUTE`, and their `GET` counterparts apply to **both channels
  together** — the master has one volume/mute concept (a single knob or
  menu value), and the daughter keeps L/R in lockstep internally. There is
  no per-channel addressing in this protocol.
- `AMP_MUTE` is a **third, independent mute path** — the amp's own 4N25
  opto mute circuit, unrelated to the LDR network's series/shunt-based
  `MUTE`. Either can be engaged regardless of the other's state; muting
  one does not imply or affect the other.
- `CAL` calibrates **both channels together** in one request — characterized
  *interleaved* (alternating between channels point-by-point, not fully
  finishing one before starting the other), which roughly halves the total
  time versus doing them back to back, since the dominant cost is each
  LDR's own physical settling time and the two channels' I2C buses don't
  contend with each other.

**Volume range is 1–32, not 1–64.** The daughter's LUT is sized at 32
steps (`NUM_VOLUME_STEPS_DEFAULT` in firmware). Update the master's
`DAUGHTER_VOL_MIN`/`MAX` accordingly.

**The `CAL` hold time is now a measured, not estimated, wait — still not
8 seconds, but shorter than earlier sequential estimates.** Real automatic
characterization sweeps on actual hardware have taken anywhere from ~15
seconds to over a minute per channel depending on how many duty points
require the full convergence timeout before succeeding or giving up. With
both channels now characterized interleaved rather than sequentially,
total time is roughly what a *single* channel's sequential sweep used to
take, not double that — but still budget generously (a couple of minutes)
as a safety margin, since convergence time is inherently variable. The
master must hold on `CAL DONE`/`CAL FAIL`, not a fixed timer — see below.

Two behaviors worth knowing, neither visible on the wire: (1) the sweep now
**refines adaptively across the steep knee**, adding roughly 15–20 extra
measured points per phase, so a `CAL` takes on the order of 20–30 s longer
than before, and `CAL FULL` now ends with an automatic trim pass (roughly another minute) because the sweep alone is systematically a few duty counts off at the knee — so budget **3–4 minutes** for `CAL FULL`; (2) the attenuation **range is always computed** from the measured cell floors
and Rtotal — there is no fixed-range setting and no command to set one — and
**both channels always use the smaller of the two channels' own ranges**, so
every `VOL` step has the same dB on L and R. (Previously each channel's range
came from its own cell floors and the pair could differ by several dB at the
quiet end.) `CAL RTOTAL` / `CAL MODE` / `CAL FIXEDSERIES` re-solve with the
same stereo-common range.

**Calibration is persisted to flash on the daughter board.** In normal
operation, the daughter auto-loads a saved calibration at boot and never
needs `CAL` sent to it at all. The master should treat `CAL` as a
deliberate, user-triggered action (e.g. a "Recalibrate" menu item), not
something to send routinely.

## Physical link (unchanged from v1)

UART0, pins/baud per each board's `Pins.hpp`/`Config.hpp` — **check current
values directly**, they've changed before. Plain ASCII, `\n`-terminated
(daughter tolerates a stray `\r` before it), one command in flight at a
time, 500ms response timeout — **except `CAL`, which uses the hold
mechanism below instead of the 500ms timeout.**

## Commands

| Command | Daughter replies | Notes |
|---|---|---|
| `MUTE ON` | `OK` / `ERR ...` | Both channels. Instant (series→0, shunt→max); works even with no calibration loaded. |
| `MUTE OFF` | `OK` / `ERR ...` | Restores whatever was active (or last requested, if a `VOL` came in while muted) on both channels. |
| `VOL UP` | `OK` / `ERR ...` | One LUT step louder, both channels, clamped at 32. |
| `VOL DOWN` | `OK` / `ERR ...` | One LUT step quieter, both channels, clamped at 1. |
| `VOL <n>` | `OK` / `ERR ...` | `<n>` is 1–32 (maps to internal 0-indexed step `n-1`). `ERR` if no calibration is loaded on either channel, or if that step is marked invalid on either channel's LUT (see below). **A jump of more than one step auto-ramps through every intermediate step** (confirmed on real hardware: applying a large jump in one shot produces an audible pop) -- a couple of ms per intermediate step, worst case ~60-250ms depending on firmware tuning for a full-range jump. Still comfortably inside the normal 500ms response timeout, but don't assume `VOL <n>`'s reply is instantaneous the way `VOL UP`/`VOL DOWN` are. |
| `AMP_MUTE ON` / `AMP_MUTE OFF` | `OK` / `ERR boot mute hold active, <n>ms remaining` | Engages/disengages the amp's own mute (4N25 opto, independent circuit from the LDR network) -- **regardless of either channel's own `MUTE` state**. Once the hold expires, **the amp auto-unmutes on its own, with no command needed** -- a real master generally doesn't need to send `AMP_MUTE OFF` at all unless it deliberately re-muted for its own reasons after boot. (The boot-hold `ERR` behavior described here now applies to every command, not just this one -- see the new section below.) |
| `GET AMP_MUTE` | `AMP_MUTE=1` / `AMP_MUTE=0` | |
| `GET MUTE` | `MUTE=1` / `MUTE=0` | |
| `GET VOL` | `VOL=<n>` | Reports the shared step, 1–32. |
| `GET STATUS` (alias `STATUS`) | `OK STATUS=IDLE|BUSY ...` (one line, see **GET STATUS** below) | Everything a master needs after boot to sync its display in one round trip. Read-only. **Answered even while a calibration is running** (as `STATUS=BUSY`). When idle it is refused during the boot hold like every other strict command (`ERR boot mute hold active, <n>ms remaining` — wait that long and ask again). |
| `CAL` / `CAL FULL` | `OK` immediately, then async `CAL DONE` / `CAL FAIL <reason>` | Full characterization (coarse sweep + adaptive refinement across the knee) + solve (under whatever RTOTAL/mode is currently set) + **trim of every step against live measurements** + save, both channels. Bare `CAL` = `CAL FULL`. Use this for a from-scratch re-calibration once initial setup (below) is done. Expect on the order of 3-4 minutes. |
| `CAL FAST` | same as above | **Drift touch-up, not a smaller sweep.** Keeps the saved curves and LUT, re-measures and trims every step (no sweep), then returns each channel to its *current* volume. Roughly a minute or two. If either channel has no usable calibration it silently does a `CAL FULL` instead. Audio is disconnected for the duration (relay energized) like any `CAL`; the volume it returns to is the one in force before it started, and the mute state is preserved. Use this periodically (e.g. after warm-up) rather than `CAL FULL`. |
| `CAL INIT` | `OK`, then async `CAL INIT SUGGEST RTOTAL=<n>:RANGE=<db> ...` then `CAL INIT DONE` / `CAL INIT FAIL <reason>` | **First-run / unknown-cells path.** Characterizes both channels (same cost as `CAL FULL`) and leaves a working calibration in place immediately (under whatever RTOTAL/mode was already set, so the unit isn't left unusable), then reports a fixed set of candidate `RTOTAL` values with each one's achievable dB range computed from the curves just measured — the smaller of the two channels' ranges, which is exactly the range the unit will use if that candidate is applied. Does not itself commit to a candidate -- follow with `CAL RTOTAL`/`CAL FIXEDSERIES`/`CAL MODE` to apply one (cheap -- reuses these same curves, no re-sweep). |
| `CAL RTOTAL <ohms>` | `OK` | Sets the Rs+Rsh target on both channels. If curves already exist, immediately re-solves and saves (instant, no re-characterization) -- otherwise takes effect on the next `CAL FULL`/`INIT`. **The instant re-solve is untrimmed** (a re-solve rebuilds the LUT from the curves); send `CAL FAST` afterwards to trim it against live measurements. |
| `CAL MODE RTOTAL` | `OK` | Switches both channels to the constant-impedance scheme (default). Same immediate-resolve-if-possible behavior as `CAL RTOTAL`. |
| `CAL MODE FIXEDSERIES` | `OK` | Switches both channels to the fixed-series scheme, keeping whatever fixed series duty is already set. **Only meaningful if that duty was already tuned** -- see `CAL FIXEDSERIES` below, or `DEBUG` mode's `CALSERIESDUTY` for manual tuning. |
| `CAL FIXEDSERIES <ohms>` | `OK` / `ERR <reason>` | Switches both channels to the fixed-series scheme, picking each channel's own fixed duty as whatever best achieves the given target resistance on its own characterized series curve (so the master only ever deals in ohms, never raw duty). `ERR` if the target falls outside a channel's characterized range -- run `CAL INIT`/`FULL` first. |


## GET STATUS

`GET STATUS` (or bare `STATUS`) replies with **one line that always starts with `OK`**,
followed by `KEY=value` pairs separated by single spaces. Every key is always present,
in this order, so a fixed-format scan works as well as a tokenizer:

```
OK STATUS=IDLE PROTO=2 MUTE=0 AMP_MUTE=0 VOL=1 VOLMIN=1 VOLMAX=31 DB=-54.0 RANGE=54.0 CAL=OK RTOTAL=50000 MODE=RTOTAL
```

| Key | Meaning |
|---|---|
| `STATUS` | `IDLE` — normal operation, every other field is current. `BUSY` — a calibration or trim is running (a `CAL`/`CAL FAST` from the master, a bench command, or the automatic calibration at power-up after a flash or PWM change). While `BUSY` all the other fields are **transient — ignore them** (the LUT may be mid-rebuild); just show "calibrating" and wait for `CAL DONE`/`CAL FAIL` (or ask again later). |
| `PROTO` | Protocol revision of this daughter firmware (currently `2`). Compare against what the master was written for. |
| `MUTE` | `1` if the LDR mute is engaged (what `MUTE ON` sets). Same value as `GET MUTE`. |
| `AMP_MUTE` | `1` if the amplifier mute is engaged (what `AMP_MUTE ON` sets, and what the boot hold / relay clicks assert). Same as `GET AMP_MUTE`. |
| `VOL` | Current volume, same 1-based numbering as `VOL <n>` and `GET VOL`. Valid regardless of `MUTE` (mute doesn't change it). |
| `VOLMIN`, `VOLMAX` | Lowest and highest `VOL` that is valid on **both** channels. The top LUT step is unreachable by design, so `VOLMAX` is normally one below the LUT size (31 of 32). Use these to clamp the master's own volume UI; `VOL UP` at `VOLMAX` returns `ERR step limit`. |
| `DB` | Attenuation at the current `VOL`, in dB (negative; 1 decimal). Handy for an on-screen readout. |
| `RANGE` | Total attenuation depth the LUT spans (dB from `VOLMIN` to the notional 0 dB top). Always computed from the measured floors; identical on both channels. |
| `CAL` | `OK` — both channels have a usable calibration. `PARTIAL` — only one does (a `VOL` command will fail). `NONE` — neither. |
| `RTOTAL` | The Rs+Rsh target in ohms (same on both channels). |
| `MODE` | `RTOTAL` (constant-impedance, default) or `FIXEDSERIES`. |

When idle, `VOL`, `VOLMIN`, `VOLMAX`, `DB` and `RANGE` read `0` if `CAL` is not `OK`; `PROTO`, `MUTE`, `AMP_MUTE`, `RTOTAL` and `MODE` are always meaningful.

### Commands while a calibration is running

A calibration (or trim) blocks the daughter's main loop for minutes, but it keeps the
master link alive: **any line the master sends meanwhile is answered within ~10 ms.**
`GET STATUS` gets `OK STATUS=BUSY ...`; every other command (`VOL`, `MUTE`, `CAL`,
`AMP_MUTE`, `GET VOL`, even `DEBUG`) gets `ERR busy` and is **not** queued or executed —
resend it after `CAL DONE`/`CAL FAIL`. The calibration itself is unaffected. This
replaces the earlier "may reply `ERR`, may reply nothing at all" behaviour. It applies to
the UART link only (the USB link, when used as a bench strict-protocol terminal, is left
alone so a keypress there can still abort a bench calibration).

The same holds at **power-up**: after a flash, a PWM-resolution change or a corrupt
calibration file the daughter calibrates both channels before it is usable (a few
minutes), and the UART is already listening, so a master that boots first and asks
`GET STATUS` hears `STATUS=BUSY` instead of silence.

**Intended use:** send `GET STATUS` once the master has booted. If the reply is
`STATUS=BUSY`, show "calibrating" and poll again every few seconds; if it is
`ERR boot mute hold active, <n>ms remaining`, wait `n` ms and ask again; if it is
`STATUS=IDLE`, adopt `MUTE`/`VOL`/`DB` as the displayed state instead of assuming
defaults. Re-send it after any `CAL` completes (`CAL DONE`) or after `CAL RTOTAL`/
`CAL MODE`, since those can change `VOLMAX`, `RANGE` and `CAL`.

Master-side parsing sketch (tokenize on spaces, split each token at the first `=`):

```cpp
// line = "OK STATUS=IDLE PROTO=2 MUTE=0 ..." ; false if it isn't a status line
bool parseStatus(const String &line, Status &out) {
  if (!line.startsWith("OK STATUS=")) return false;
  int i = 3;                                   // skip "OK "
  while (i < (int)line.length()) {
    int sp = line.indexOf(' ', i); if (sp < 0) sp = line.length();
    int eq = line.indexOf('=', i);
    if (eq > i && eq < sp) {
      String k = line.substring(i, eq), v = line.substring(eq + 1, sp);
      if      (k == "STATUS")   out.busy    = (v == "BUSY");
      else if (k == "PROTO")    out.proto   = v.toInt();
      else if (k == "MUTE")     out.mute    = v.toInt();
      else if (k == "AMP_MUTE") out.ampMute = v.toInt();
      else if (k == "VOL")      out.vol     = v.toInt();
      else if (k == "VOLMIN")   out.volMin  = v.toInt();
      else if (k == "VOLMAX")   out.volMax  = v.toInt();
      else if (k == "DB")       out.db      = v.toFloat();
      else if (k == "RANGE")    out.range   = v.toFloat();
      else if (k == "CAL")      out.cal     = v;        // "OK" | "PARTIAL" | "NONE"
      else if (k == "RTOTAL")   out.rtotal  = v.toInt();
      else if (k == "MODE")     out.mode    = v;        // "RTOTAL" | "FIXEDSERIES"
      // unknown keys: ignore, so newer daughters can add fields
    }
    i = sp + 1;
  }
  return true;
}
```

Unknown keys must be ignored, so fields can be appended in future revisions without breaking an older master.

## Boot hold (`MUTE_BOOT_HOLD_MS`) blocks EVERY command, not just `AMP_MUTE`

This is a real behavioral change worth being precise about: during the
boot-mute hold window, the daughter refuses **every** strict-protocol
command -- `VOL`, `MUTE`, `CAL` and all its variants, every `GET*` --
replying `ERR boot mute hold active, <n>ms remaining` to each one, not
just to `AMP_MUTE`. This is a deliberate blanket window so nothing can
disturb the controlled startup sequence while it's settling. `DEBUG` is
the one exception (a technician needs to reach the bench console
regardless of hold state) -- sending it during the hold still works
normally and is not itself subject to this gate.

A master that sends anything during this window should expect this
specific `ERR` text and simply retry after the reported remaining time,
rather than treating it as a protocol fault.

## What state exists right after `CAL DONE` (or at boot)

Both at boot and at the end of any successful `CAL`, the daughter
automatically lands on a real, defined volume state with no command
needed: the lowest LUT step that's actually valid, if one exists, or the
LDR's own hard mute (not `AMP_MUTE` -- the series/shunt network itself)
if a channel's calibration came back degenerate (zero usable steps). A
master doesn't need to send an explicit `VOL` after `CAL DONE` just to
get hardware into a sane state -- `GET VOL` will already report something
real and intentional, not leftover/undefined duty from the sweep itself.

## Calibration (`CAL`) — now fully settled

1. Master sends `CAL`. Daughter replies `OK` immediately (calibration
   *started*, not finished) and begins characterizing **both channels at
   once, interleaved** (see "What changed from v1" above).
2. Master holds off sending anything else until it sees `CAL DONE` or
   `CAL FAIL <reason>` — **not** a fixed timer. Use a generous safety
   timeout (a few minutes) purely as a crash/disconnection guard, not as
   the expected completion signal.
3. On success: `CAL DONE`. The daughter has solved a new LUT for each
   channel, verified/trimmed it against live measurement, and saved it to
   flash — it's immediately usable via `VOL`.
4. On failure: `CAL FAIL insufficient valid steps or no ADS1115 detected
   on one or both channels` -- this exact wording, now settled. **Either
   channel** coming back with zero genuinely usable LUT steps triggers
   `FAIL` for the whole request, even if the other channel solved fine --
   there's no partial-success reply. (Worth knowing why this check is
   trustworthy: a degenerate calibration can still produce a full-size,
   32-entry LUT with every single entry marked invalid -- confirmed on
   real hardware -- so this check specifically looks for at least one
   *valid* step per channel, not just that the LUT array exists.) Master
   parsing should still only rely on "did I get `DONE` or `FAIL`", not on
   matching this exact string, in case the wording is refined later.
5. **`VOL`/`MUTE` sent before a `CAL DONE` should be rejected or queued**,
   not silently misapplied — the daughter's relay is energized and its
   driver outputs are in calibration-sweep states throughout, not in a
   state where a volume change means anything.

## `DEBUG` / `EXIT` -- a human-technician escape hatch, not part of the protocol

Sending the exact line `DEBUG` on this same UART0 link switches it into
the full bench console (every command described throughout this project's
development -- `SET`, `PCT`, `CALSCAN`, `CALTRIM`, verbose `CALAUTO`
progress, etc.) for field service, without needing to physically connect
to USB. `EXIT` (or `PROD`) switches back to the strict protocol above.
**A real master's firmware should never send either word** -- this exists
purely so a technician with a terminal on the same wire can get full
diagnostic access. While in this mode, replies are verbose human-readable
text, not the strict wire format -- don't attempt to parse responses
during a `DEBUG` session as if they were the normal protocol.

## Settings persistence

`RTOTAL` and mode are saved to flash alongside the calibration data itself
-- a power cycle will not silently revert either one. `CAL RTOTAL`/`CAL
MODE` changes take effect (and are saved) immediately if a channel is
already characterized; otherwise they're remembered and applied on the
next `CAL FULL`/`FAST`.

## Known asymmetry (unchanged from v1)

The master's own motor+relay volume/mute path is completely independent of
this UART link, per the original doc — nothing here changes that.
