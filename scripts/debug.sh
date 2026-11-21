#!/usr/bin/env bash
# Re-run one test under gdb, stopping on SystemC errors.
#   scripts/debug.sh --script tests/test_timer.tcl
#   scripts/debug.sh --fw timer_irq
set -euo pipefail
cd "$(dirname "$0")/.."
exec gdb -q -ex "break sc_core::sc_report_handler::report" -ex run --args build/minisoc "$@"
