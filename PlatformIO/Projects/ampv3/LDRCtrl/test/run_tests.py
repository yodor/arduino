#!/usr/bin/env python3
"""Compile and run every host test (test/test_*.cpp) against the firmware sources.

Needs a C++17 g++ on the PATH (Linux, macOS; on Windows e.g. MSYS2/MinGW).
Usage:  python3 test/run_tests.py            all tests
        python3 test/run_tests.py balance    only tests whose name contains "balance"
Exit code 0 when every test passes.
"""
import os, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
src = os.path.join(root, "src")
pattern = sys.argv[1] if len(sys.argv) > 1 else ""

import re

def check_float_formats():
    """No float conversions (%f %e %g %a, any flags/width/precision) in the firmware's format
    strings: the Pico core links newlib-nano, where they silently print NOTHING. Floats go
    through Console::num(value, decimals) instead. Comment lines are ignored."""
    rx = re.compile(r'%[-+ 0#]*[0-9*]*(\.[0-9*]*)?[fFgGeEaA]')
    hits = []
    for name in sorted(os.listdir(src)):
        if not name.endswith((".cpp", ".hpp")):
            continue
        with open(os.path.join(src, name), encoding="utf-8", errors="replace") as fh:
            for no, line in enumerate(fh, 1):
                if line.lstrip().startswith("//"):
                    continue
                if rx.search(line):
                    hits.append(f"{name}:{no}: {line.strip()}")
    return hits

hits = check_float_formats()
if hits:
    print("FAIL  float conversions in format strings (they print nothing on the Pico; use Console::num):")
    for h in hits:
        print("      " + h)
else:
    print("PASS  source check: no float conversions in format strings")

tests = sorted(f for f in os.listdir(here) if f.startswith("test_") and f.endswith((".cpp", ".py")) and pattern in f)
if not tests:
    print("no tests found")
    sys.exit(1)

failed = []
with tempfile.TemporaryDirectory() as tmp:
    for t in tests:
        if t.endswith(".py"):                       # Python tests run as they are
            r = subprocess.run([sys.executable, os.path.join(here, t)], capture_output=True, text=True)
            last = (r.stdout.strip().splitlines() or ["(no output)"])[-1]
            ok = r.returncode == 0 and last.endswith("PASS")
            print(f"{'PASS' if ok else 'FAIL'}  {t[:-3]:28s} {last if ok else ''}")
            if not ok:
                print(r.stdout[-2000:] + r.stderr[-2000:])
                failed.append(t)
            continue
        exe = os.path.join(tmp, t[:-4] + (".exe" if os.name == "nt" else ""))
        cmd = ["g++", "-std=gnu++17", "-O1", "-I", src, "-I", os.path.join(here, "support"),
               "-I", os.path.join(here, "data"), os.path.join(here, t), "-o", exe]
        c = subprocess.run(cmd, capture_output=True, text=True)
        if c.returncode != 0:
            print(f"BUILD FAIL  {t}\n{c.stderr.strip()[:2000]}")
            failed.append(t)
            continue
        r = subprocess.run([exe], capture_output=True, text=True)
        last = (r.stdout.strip().splitlines() or ["(no output)"])[-1]
        ok = r.returncode == 0 and last.endswith("PASS")
        print(f"{'PASS' if ok else 'FAIL'}  {t[:-4]:28s} {last if ok else ''}")
        if not ok:
            print(r.stdout[-2000:])
            failed.append(t)

print(f"\n{len(tests) - len(failed)} of {len(tests)} tests passed" + ("" if not hits else ", source check FAILED"))
sys.exit(1 if (failed or hits) else 0)
