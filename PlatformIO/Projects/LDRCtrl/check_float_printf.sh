#!/bin/sh
# Fails if any printf-family format string in the firmware uses a float conversion
# (%f %e %g %a, with any flags/width/precision). The Pico core links newlib-nano, where
# those silently print NOTHING -- an empty field, no error. Format floats with
# Stream::print(value, decimals) or Capabilities::fmtFixed() instead.
cd "$(dirname "$0")/../src" || exit 2
hits=$(grep -n '%[-+ 0#]*[0-9*]*\(\.[0-9*]*\)\?[fFgGeEaA]' *.cpp *.hpp | grep -v '^[^:]*:[0-9]*:[[:space:]]*//')
if [ -n "$hits" ]; then
  echo "FLOAT printf conversions found (these print nothing on the Pico):"; echo "$hits"; exit 1
fi
echo "ok: no float conversions in format strings"
