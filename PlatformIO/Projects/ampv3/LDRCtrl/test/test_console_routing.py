#!/usr/bin/env python3
"""Host test: debug routing. The debug console's command table (src/ConsoleCommands.cpp, kConsole)
must dispatch every line exactly as the old if/else chain did: by the first word,
case-insensitive, aliases included; SET checks its three forms in the original order."""
import os, re, sys
src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "ConsoleCommands.cpp")
text = open(src, encoding="utf-8").read()
body = text[text.index("const ConsoleCommands::ConsoleCommand ConsoleCommands::kConsole[] = {"):]
body = body[:body.index("};")]
rows = re.findall(r'\{"([^"]+)",\s*&ConsoleCommands::(\w+),\s*(nullptr|")', body)
table = {name: fn for name, fn, _ in rows}
assert table, "table not found"

def route(line):
    words = line.split()
    if not words:
        return "(nothing)"
    return table.get(words[0].upper())        # None = "ERR unknown command -- HELP"

expect = {
  "help": "cmdHelp", "?": "cmdHelp", "HELP": "cmdHelp",
  "cal": "cmdCal", "CAL R=AUTO": "cmdCal", "forget cal": "cmdForget", "vol 5": "cmdVol", "Vol info": "cmdVol",
  "comp off": "cmdComp", "learn": "cmdLearn", "temp": "cmdTemp",
  "mute on": "cmdMutes", "amp_mute off": "cmdMutes", "relay on": "cmdMutes",
  "set src 47": "cmdSet", "set auto deep": "cmdSet", "set ser_l 100": "cmdSet", "set": "cmdSet",
  "bal 3": "cmdBal", "bal": "cmdBal", "model": "cmdModel", "ver": "cmdVer", "fw": "cmdVer",
  "read": "cmdRead", "verify": "cmdVerify", "report": "cmdReport", "dump": "cmdDump",
  "diag": None, "diag all": None,          # renamed: VERIFY / REPORT / DUMP
  "xyz": None, "status": None, "env": None, "   ": "(nothing)",
}
bad = 0
for line, want in expect.items():
    got = route(line)
    if got != want:
        print(f"  {line!r} -> {got}, expected {want}"); bad += 1
# SET: the audio model, then the AUTO preference, then a raw duty -- the original order
fn = text[text.index("void ConsoleCommands::cmdSet("):]
fn = fn[:fn.index("\n}\n")]
i_src, i_auto, i_duty = fn.find('equalsIgnoreCase("SRC")'), fn.find('equalsIgnoreCase("AUTO")'), fn.find("DriverChannels::find")
if not (0 <= i_src < i_auto < i_duty):
    print("  SET forms out of order"); bad += 1
# every command is documented in HELP, or is an alias of a documented one
documented = {fn for name, fn, h in rows if h == '"'}
for name, fn, h in rows:
    if h == "nullptr" and fn not in documented and fn != "cmdHelp":
        print(f"  {name}: no HELP lines and no documented alias"); bad += 1
print(f"  {len(table)} table entries, {len(expect)} lines routed, {bad} differences")
print("console routing host test: " + ("PASS" if bad == 0 else "FAIL"))
sys.exit(1 if bad else 0)
