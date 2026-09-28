# Master Board <-> Daughter Board UART Protocol Reference

Extracted from the RP2350 audio visualizer/volume-control master board firmware
(`DaughterBoardLink.hpp/.cpp`, `VolumeControl.hpp/.cpp`, `Config.hpp`), for the
daughter board (LDR/vactrol volume control) firmware project to implement
against. The master board is the existing, working side of this link; the
daughter board firmware needs to speak this protocol to interoperate with it
correctly.

## Physical link

- **Peripheral:** RP2350 UART0, exposed to application code as Arduino-Pico's
  `Serial1`.
- **Pins (master board side):** `DAUGHTER_UART_TX_PIN` / `DAUGHTER_UART_RX_PIN`
  in `Pins.hpp` -- currently GPIO0 (TX) / GPIO1 (RX), UART0's default pin
  location. **Check the current values in `Pins.hpp` directly** before wiring
  anything, since board pin assignments have changed at least once already
  during this project's development (breadboard -> PCB) and may again.
- **Baud rate:** `DAUGHTER_UART_BAUD` in `Config.hpp` -- currently 115200.
  The master board's own comment flags this as unconfirmed against real
  daughter board firmware ("confirm against the daughter board's actual
  firmware") -- treat 115200 as the master's assumption, not a settled spec,
  until both sides are actually tested together.
- **Framing:** plain ASCII text, one command or reply per line, newline
  (`\n`) terminated. The master board's receive parser tolerates a stray `\r`
  before the `\n` (so CRLF from a manual terminal works too), but only ever
  *sends* bare `\n`.
- **Transaction model:** strictly one command in flight at a time. The master
  board will not send a new command until it has received a reply to the
  previous one (or that command has timed out) -- the daughter board never
  needs to handle pipelined/overlapping commands.

## Commands the master board sends

| Command | Daughter board should reply | Notes |
|---|---|---|
| `MUTE ON` | `OK` or `ERR ...` | |
| `MUTE OFF` | `OK` or `ERR ...` | |
| `VOL UP` | `OK` or `ERR ...` | One discrete step per command -- see "Repeat behavior" below, this is not a continuous drive signal. |
| `VOL DOWN` | `OK` or `ERR ...` | Same, one discrete step per command. |
| `VOL <n>` | `OK` or `ERR ...` | `<n>` is decimal, always sent within `DAUGHTER_VOL_MIN`-`DAUGHTER_VOL_MAX` (currently **1-64** inclusive) -- the master board validates and refuses to send anything outside this range itself, so the daughter board should never receive an out-of-range value from this master, but shouldn't assume that leniency if other senders exist. |
| `GET MUTE` | `MUTE=1` or `MUTE=0` | `1` = muted. |
| `GET VOL` | `VOL=<n>` | `<n>` decimal, same 1-64 range. |
| `CAL` | `OK` (see "Calibration" below) | |

Master-side sender functions (`DaughterBoardLink::sendMuteOn()` etc.) refuse
to transmit anything (returning `false`, no bytes sent) if a previous command
is still awaiting a reply, or during the post-`CAL` hold window -- so from the
daughter board's perspective, commands always arrive one at a time with
reasonable spacing, never in a burst.

## Replies the daughter board should send

- **`OK`** -- command succeeded. For `CAL` specifically, this means
  calibration *started*, not that it finished (see below).
- **`ERR <reason>`** -- command failed; `<reason>` is free text, logged by the
  master board but not parsed/matched against anything specific. Keep it
  short and one line.
- **`MUTE=1`** / **`MUTE=0`** -- reply to `GET MUTE`.
- **`VOL=<n>`** -- reply to `GET VOL`.
- **`CAL DONE`** -- unsolicited (not a reply to any specific command),
  proposed but **not confirmed implemented**. See "Calibration" below.

Any line that doesn't match one of the patterns above is logged by the master
board (`[Daughter] Unrecognized reply: ...`) but does not cause a hard error
-- safe to emit debug/log chatter on the same line-based channel while the
protocol is still being finalized, as long as it doesn't collide with the
exact patterns above.

## Timing behavior to match

- **`DAUGHTER_RESPONSE_TIMEOUT_MS` = 500ms.** If the daughter board takes
  longer than this to reply to an ordinary (non-`CAL`) command, the master
  board gives up and marks that command as timed out. Reply faster than this
  under normal conditions.
- **`DAUGHTER_VOL_REPEAT_MS` = 250ms.** While a volume button is physically
  held on the master board, it re-sends `VOL UP`/`VOL DOWN` every 250ms
  (each one a fresh, independent single-step command+reply transaction) --
  this is how "holding the button" becomes continuous volume movement on
  this protocol, rather than the master board sending one long-held
  "start moving" / "stop moving" pair of commands. Design the daughter
  board's step size accordingly (a 250ms cadence at whatever step-per-VOL-UP
  size you implement determines the perceived ramp speed).

## Calibration (`CAL`) -- explicitly unsettled, read carefully

This is the one part of the protocol the master board's own comments flag as
a placeholder, not a finished spec:

- The master board sends `CAL`, expects an immediate `OK` acknowledging that
  calibration has *started* (not finished).
- Real completion time is unknown to the master board. Rather than assume
  anything, it just holds off sending any further commands for
  `DAUGHTER_CAL_HOLD_MS` = **8000ms** after that initial `OK`, then resumes
  normal traffic regardless of whether calibration has actually finished by
  then.
- **Proposed addition, not confirmed:** an unsolicited `CAL DONE` line sent
  the moment calibration genuinely completes, which would let the master
  board end its hold window early instead of waiting out the full 8 seconds
  every time. The master board's parser already handles this message if it
  arrives, but nothing has confirmed the daughter board actually needs to
  (or will) send it.
- **What the daughter board project should decide:** whether to implement
  `CAL DONE`, and if so, whether 8000ms is actually long enough for however
  calibration ends up working on the LDR/vactrol hardware. Also worth
  deciding here: what should happen if the master board sends a command
  *during* calibration despite the hold window (e.g. a race, or the hold
  window turning out too short) -- the master board's comment notes this
  daughter-side behavior "isn't finalized... may reply ERR, may reply
  nothing at all," meaning this is genuinely open on both sides right now.

## Known asymmetry worth knowing about

The master board drives volume through **two entirely independent paths
unconditionally, always both**: this UART link, *and* a direct DRV8833 motor
driving a motorized potentiometer (see `VolumeMotor.hpp/.cpp`) plus a direct
`MUTE_PIN` GPIO to a 4N25 optocoupler mute circuit. Neither path knows or
cares whether the other is physically populated on a given unit -- whichever
hardware is actually present takes effect, the other is just an unconnected
pin toggling or a UART command nobody's listening to. This means the daughter
board firmware doesn't need to coordinate with or be aware of the motor/mute
GPIO path at all; it only ever needs to correctly implement this UART
protocol on its own terms.
