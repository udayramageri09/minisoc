#!/usr/bin/env bash
# Everything CI runs, in one command: unit tests, build, full regression.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 -m pytest -q tools/tests
scripts/build.sh Debug
python3 tools/regress.py --bin build/minisoc --out out
