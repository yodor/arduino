#!/usr/bin/env python3
"""Capture the attenuator's strict-protocol replies, and compare two captures.

For refactoring: capture once before flashing a new firmware and once after, then compare.
Only commands that change NOTHING are sent: the read-only queries, and a few malformed
commands that must be rejected. Needs pyserial (openSUSE: python3-pyserial; Gentoo:
dev-python/pyserial; or pip install pyserial).

  python3 tools/protocol_capture.py capture /dev/ttyUSB0 before.txt
  python3 tools/protocol_capture.py capture /dev/ttyUSB0 after.txt
  python3 tools/protocol_capture.py compare before.txt after.txt

The port is whichever serial port reaches the attenuator's protocol (the UART the controller uses,
115200 baud). The board must be in the strict protocol (send EXIT first if in DEBUG).

compare checks the STRUCTURE: the same lines, the same keys in the same order, every text
value identical. Numbers are compared as placeholders, since temperatures, uptime, rail and
offsets differ between any two runs (VER's FW= and BUILD= are expected to change too).
"""
import re, sys, time

COMMANDS = [
    "STATUS", "ENV", "PROGRESS", "LEARN", "MODEL", "VER", "FW", "BAL",
    # malformed or out of range: each must be rejected with its error reply, changing nothing
    "XYZ", "VOL X", "VOL 99", "BAL X", "BAL 99", "SET AUTO X", "SET SRC X", "SET LOAD X",
    "FORGET", "FORGET X", "AMP_MUTE 2", "CAL R=1",
]

def capture(port, out_path, baud=115200):
    import serial
    with serial.Serial(port, baud, timeout=0.2) as s, open(out_path, "w") as out:
        s.reset_input_buffer()
        for cmd in COMMANDS:
            s.write((cmd + "\n").encode())
            s.flush()
            lines, t0 = [], time.time()
            while time.time() - t0 < 1.0:                       # the protocol answers within ~10 ms
                ln = s.readline().decode(errors="replace").rstrip("\r\n")
                if ln:
                    lines.append(ln)
                    if ln.startswith(("OK", "ERR")):
                        break
            out.write(f">>> {cmd}\n")
            for ln in lines:
                out.write(ln + "\n")
            print(f"{cmd:12s} {lines[-1] if lines else '(no reply)'}")
    print(f"saved {out_path}")

NUM = re.compile(r"[-+]?\d+(\.\d+)?")
VOLATILE_KEYS = ("FW=", "BUILD=", "NOTE=")             # change with every release by design

def normalize(line):
    parts = []
    for tok in line.split(" "):
        if tok.startswith(VOLATILE_KEYS):
            parts.append(tok.split("=")[0] + "=*")
        else:
            parts.append(NUM.sub("#", tok))
    return " ".join(parts)

def compare(a_path, b_path):
    a = [l.rstrip("\n") for l in open(a_path)]
    b = [l.rstrip("\n") for l in open(b_path)]
    # NOTE= runs to the end of the line: cut it before normalizing
    cut = lambda l: l.split(" NOTE=")[0] if " NOTE=" in l else l
    diffs = 0
    for i in range(max(len(a), len(b))):
        la = a[i] if i < len(a) else "(missing)"
        lb = b[i] if i < len(b) else "(missing)"
        if normalize(cut(la)) != normalize(cut(lb)):
            print(f"line {i + 1}:\n  before: {la}\n  after:  {lb}")
            diffs += 1
    print("identical structure" if diffs == 0 else f"{diffs} differing line(s)")
    return diffs == 0

if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "capture":
        capture(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == "compare":
        sys.exit(0 if compare(sys.argv[2], sys.argv[3]) else 1)
    else:
        print(__doc__)
        sys.exit(2)
