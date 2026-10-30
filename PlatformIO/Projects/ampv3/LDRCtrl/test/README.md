# Host tests

Each `test_*.cpp` checks one piece of the firmware's logic on a PC, most of them on real
measurements from this attenuator (step tables, curves, learning logs). They compile the
firmware's own headers from `src/`, so they test the code that runs on the board.

Run them all:

    python3 test/run_tests.py

or only some (by name):

    python3 test/run_tests.py balance

Needs a C++17 `g++` on the PATH (Linux, macOS; on Windows e.g. MSYS2/MinGW).

- Before the tests, a source check makes sure no format string uses a float conversion
  (`%f`, `%e`, `%g`, `%a`): on the Pico those print nothing. Floats go through
  `Console::num(value, decimals)`.
- `support/Arduino.h` is a minimal stand-in, enough for the headers that include Arduino.
- `data/` holds data extracted from calibration logs.

Every change to the calibration, the compensation or the protocol logic should come with a
test here, and all of them should pass before a release.
