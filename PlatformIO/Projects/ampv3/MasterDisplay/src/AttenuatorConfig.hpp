#pragma once
#include <cstddef>
#include <cstdint>

// Settings for the link to the LDR attenuator board (AttenuatorBoardLink):
// the protocol revision this master is written for, the UART speed, poll
// intervals, timeouts and buffer sizes. Its UART pins are in Pins.hpp with
// the rest of the pin map. Included via AttenuatorBoardLink.hpp.

// UART attenuator board link (pins in Pins.hpp: ATTENUATOR_UART_TX_PIN / _RX_PIN).
constexpr uint32_t ATTENUATOR_UART_BAUD = 115200; // confirm against the attenuator board's actual firmware

// Protocol revision 7: the volume range is no longer a master-side
// constant -- it comes from the attenuator's STATUS line (VOLMIN/VOLMAX,
// 0-based, normally 0..31), so nothing here hard-codes it.

// Protocol revision this master is written for. An attenuator reporting a
// different PROTO= is logged loudly (the protocol doc says a master
// written for another revision will misbehave), but still parsed.
constexpr uint8_t ATTENUATOR_PROTO_EXPECTED = 20;

// How often to re-fire VOL UP/DOWN while a volume button is held -- each
// command is a single discrete step on the attenuator board.
constexpr uint32_t ATTENUATOR_VOL_REPEAT_MS = 250;

// How long to wait for a reply to any command before giving up and
// reporting a timeout. Revision 7 answers EVERY command within this,
// including STATUS during a calibration (~10ms).
constexpr uint32_t ATTENUATOR_RESPONSE_TIMEOUT_MS = 500;

// STATUS is the only getter in revision 7, so the master polls it: every
// IDLE_MS normally, every BUSY_MS while the attenuator reports BUSY
// (calibrating, or its boot hold -- the hold poll is shortened further to
// the remaining time it reports). Also sent right after every successful
// VOL/MUTE/AMP_MUTE command, so the on-screen volume reflects the
// attenuator's real VOL/DB rather than a local guess.
constexpr uint32_t ATTENUATOR_STATUS_POLL_IDLE_MS = 5000;
constexpr uint32_t ATTENUATOR_STATUS_POLL_BUSY_MS = 2000;

// Revision 8 read-only queries: PROGRESS polled this often while a
// calibration runs (for the progress page), ENV this often while a page
// that shows it is open (LEARN, rev 9, likewise).
constexpr uint32_t ATTENUATOR_PROGRESS_POLL_MS = 1000;
constexpr uint32_t ATTENUATOR_ENV_POLL_MS      = 5000;
// ...and, in the background, this often anyway while the attenuator is idle --
// for revision 14's resync hint and revision 13's NTC state (both based on a
// 1-minute average, so a slow poll misses nothing).
constexpr uint32_t ATTENUATOR_ENV_BACKGROUND_POLL_MS = 10000; // COMP / resync hint on the Info screen follow it

// The longest line the attenuator sends, with its terminator. VER is the
// longest (~230 characters before its free-text NOTE), so this leaves room
// for long release notes; a longer line is dropped and logged. The UART
// receive FIFO holds a whole reply plus the start of the next, so a reply
// arriving while the main loop is busy is never cut.
constexpr size_t ATTENUATOR_LINE_MAX     = 512;
constexpr size_t ATTENUATOR_RX_FIFO_SIZE = 2 * ATTENUATOR_LINE_MAX;

// Notices (resync advised, NTC fault): how long the popup stays up.
constexpr uint32_t NOTICE_POPUP_MS = 5000;

// A STATUS older than this is treated as "no link" (the attenuator stopped
// answering, or isn't fitted on this unit) -- comfortably more than two
// idle polls, so a single lost reply doesn't flip the display.
constexpr uint32_t ATTENUATOR_STATUS_STALE_MS = 12000;

// After a local mute toggle, ignore the attenuator's reported MUTE for this
// long before adopting it -- a STATUS that was already in flight when the
// user pressed mute would otherwise "undo" the press.
constexpr uint32_t ATTENUATOR_MUTE_ADOPT_GRACE_MS = 1000;

// Set true to log every UART line in both directions ([UART TX]/[UART RX])
// -- bring-up/debug only; with STATUS polled every few seconds it's noisy.
constexpr bool ATTENUATOR_LINK_TRACE = false;
