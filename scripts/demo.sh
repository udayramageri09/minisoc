#!/usr/bin/env bash
# One-command MiniSoC demo. Usage: scripts/demo.sh            (pauses between steps)
#                                  scripts/demo.sh --no-pause (runs straight through)
set -u
cd "$(dirname "$0")/.."

PAUSE=1; [ "${1:-}" = "--no-pause" ] && PAUSE=0
step()  { [ "$PAUSE" = 1 ] && clear; echo "=== MiniSoC demo - Step $1: $2 ==="; }
run()   { echo; echo "\$ $*"; echo; "$@"; }
pause() { if [ "$PAUSE" = 1 ]; then echo; read -rp "[Enter] next step " _; fi; }
HINT="On Ubuntu: sudo apt-get install -y libsystemc-dev tcl-dev cmake g++ python3-yaml"

for tool in cmake g++ python3; do
  command -v "$tool" >/dev/null 2>&1 || { echo "Missing: $tool. $HINT"; exit 1; }
done
if [ ! -x build/minisoc ]; then
  echo "Building MiniSoC (first run only)..."
  scripts/build.sh || { echo "Build failed. $HINT"; exit 1; }
fi
mkdir -p out

step 1 "Firmware running on the virtual chip"
run build/minisoc --fw timer_irq
pause

step 2 "A Tcl test checking the timer, with a bus trace"
run build/minisoc --script tests/test_timer.tcl --trace out/timer.csv
pause

step 3 "The bus trace: every transaction the bus handled"
run head -n 10 out/timer.csv
pause

step 4 "Address decoding in the bus (binary search)"
run grep -n -A15 "std::upper_bound" src/bus.h
pause

step 5 "Full regression: all tests, run in parallel"
run python3 tools/regress.py
echo; echo "=== End of MiniSoC demo ==="
