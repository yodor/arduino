#pragma once
// The firmware's identity -- the ONE place to change for a release.
//   FW_VERSION_ID    date + letter of the delivered package (what VER reports as FW=)
//   FW_VERSION_NOTE  a few words on what changed (VER: NOTE=, last on the line)
// FW_VERSION (ID + note) is what the boot banner, HELP and DIAG print.
#define FW_VERSION_ID   "2026-10-28d"
#define FW_VERSION_NOTE "balance: BAL -16..16 in 0.25 dB steps"
#define FW_VERSION      FW_VERSION_ID " (" FW_VERSION_NOTE ")"
