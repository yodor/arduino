#!/usr/bin/env python3
"""Host test: strict routing. The serial link's command table (src/SerialLink.cpp, kProtocol)
must route every protocol line exactly as the old if/else chain did -- same handler, same
order, same set answered during a calibration. Reads the real table from the source."""
import os, re, sys
src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "SerialLink.cpp")
text = open(src, encoding="utf-8").read()
body = text[text.index("const SerialLink::ProtocolCommand SerialLink::kProtocol[] = {"):]
body = body[:body.index("};")]
rows = re.findall(r'\{"([^"]*)",\s*(EXACT|PREFIX),\s*(ANYTIME|0),\s*&SerialLink::(\w+),', body)
assert rows, "table not found"

def route(line, busy=False):
    for pat, match, flags, fn in rows:
        if (line == pat) if match == "EXACT" else line.startswith(pat):
            if busy and flags != "ANYTIME":
                return "ERR busy"
            return fn
    return "ERR busy" if busy else None

# expected: what the old if/else chain did (None = "ERR unrecognized command")
expect = {
  "ENV": "cmdEnv", "PROGRESS": "cmdProgress", "LEARN": "cmdLearn", "MODEL": "cmdModel",
  "VER": "cmdVersion", "FW": "cmdVersion", "BAL": "cmdBalanceQuery", "STATUS": "cmdStatus",
  "BAL 3": "cmdBalanceSet", "BAL -16": "cmdBalanceSet", "BAL X": "cmdBalanceSet",
  "AMP_MUTE ON": "cmdAmpMute", "AMP_MUTE 1": "cmdAmpMute", "AMP_MUTE OFF": "cmdAmpMute", "AMP_MUTE 0": "cmdAmpMute",
  "AMP_MUTE 2": None, "AMP_MUTE": None,
  "MUTE ON": "cmdMuteOn", "MUTE OFF": "cmdMuteOff", "MUTE": None, "MUTE 1": None,
  "VOL UP": "cmdVolUp", "VOL DOWN": "cmdVolDown", "VOL 12": "cmdVolSet", "VOL 0": "cmdVolSet",
  "VOL X": "cmdVolSet", "VOL UPX": "cmdVolSet", "VOL": None,
  "FORGET CAL": "cmdForget", "FORGET COMP": "cmdForget", "FORGET ALL": "cmdForget",
  "FORGET": "cmdForgetUsage", "FORGET X": "cmdForgetUsage", "FORGETCAL": "cmdForgetUsage",
  "SET SRC 47": "cmdSetModel", "SET LOAD 0": "cmdSetModel", "SET SRC": None, "SET AUTO DEEP": "cmdSetAuto",
  "SET X 1": None, "COMP ON": "cmdComp", "COMP OFF": "cmdComp", "COMP": None,
  "CAL": "cmdCal", "CAL R=AUTO": "cmdCal", "CAL R=500000": "cmdCal", "CALX": None,
  "CAL VERIFY": "cmdCal", "CAL FULL": "cmdCal", "VERIFY RESULT": "cmdVerifyResult",
  "VERIFY": None, "VERIFY X": None, "VERIFYRESULT": None,
  "DEBUG": None,            # handled before the table
  "XYZ": None, "": None,
}
bad = 0
for line, want in expect.items():
    got = route(line)
    if got != want:
        print(f"  routing: {line!r} -> {got}, expected {want}"); bad += 1
# during a calibration: only the read-only queries (old busy handler), everything else ERR busy
anytime = {"ENV", "PROGRESS", "LEARN", "MODEL", "VER", "FW", "BAL", "STATUS", "VERIFY RESULT"}
for line in list(expect) + ["BAL 3", "VOL 5", "CAL", "DEBUG"]:
    got = route(line, busy=True)
    want = expect.get(line) if line in anytime else "ERR busy"
    if got != want:
        print(f"  busy: {line!r} -> {got}, expected {want}"); bad += 1
print(f"  {len(rows)} table entries, {len(expect)} lines routed, {bad} differences")
print("protocol routing host test: " + ("PASS" if bad == 0 else "FAIL"))
sys.exit(1 if bad else 0)
