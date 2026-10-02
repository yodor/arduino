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
| `CAL` / `CAL FULL` | `OK` immediately, then async `CAL DONE` / `CAL FAIL <reason>` | Full characterization + solve (under whatever RTOTAL/mode is currently set) + save, both channels. Bare `CAL` = `CAL FULL`. Use this for routine re-calibration once initial setup (below) is done. |
| `CAL FAST` | same as above | Faster, coarser re-characterization (for periodic drift touch-up) -- same async pattern. |
| `CAL INIT` | `OK`, then async `CAL INIT SUGGEST RTOTAL=<n>:RANGE=<db> ...` then `CAL INIT DONE` / `CAL INIT FAIL <reason>` | **First-run / unknown-cells path.** Characterizes both channels (same cost as `CAL FULL`) and leaves a working calibration in place immediately (under whatever RTOTAL/mode was already set, so the unit isn't left unusable), then reports a fixed set of candidate `RTOTAL` values with each one's achievable dB range (worst of the two channels) computed from the curves just measured. Does not itself commit to a candidate -- follow with `CAL RTOTAL`/`CAL FIXEDSERIES`/`CAL MODE` to apply one (cheap -- reuses these same curves, no re-sweep). |
| `CAL RTOTAL <ohms>` | `OK` | Sets the Rs+Rsh target on both channels. If curves already exist, immediately re-solves and saves (fast, no re-characterization) -- otherwise takes effect on the next `CAL FULL`/`FAST`/`INIT`. |
| `CAL MODE RTOTAL` | `OK` | Switches both channels to the constant-impedance scheme (default). Same immediate-resolve-if-possible behavior as `CAL RTOTAL`. |
| `CAL MODE FIXEDSERIES` | `OK` | Switches both channels to the fixed-series scheme, keeping whatever fixed series duty is already set. **Only meaningful if that duty was already tuned** -- see `CAL FIXEDSERIES` below, or `DEBUG` mode's `CALSERIESDUTY` for manual tuning. |
| `CAL FIXEDSERIES <ohms>` | `OK` / `ERR <reason>` | Switches both channels to the fixed-series scheme, picking each channel's own fixed duty as whatever best achieves the given target resistance on its own characterized series curve (so the master only ever deals in ohms, never raw duty). `ERR` if the target falls outside a channel's characterized range -- run `CAL INIT`/`FULL` first. |

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
