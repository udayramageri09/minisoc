#!/usr/bin/env bash
# Everything CI runs, in one command: unit tests, build, full regression.
#   scripts/check.sh            normal build
#   scripts/check.sh --asan     AddressSanitizer + UBSan build
set -euo pipefail
cd "$(dirname "$0")/.."
python3 -m pytest -q tools/tests
if [[ "${1:-}" == "--asan" ]]; then
  scripts/build.sh Debug --asan
  BIN=build-asan/minisoc
else
  scripts/build.sh Debug
  BIN=build/minisoc
fi
python3 tools/regress.py --bin "$BIN" --out out
