#!/usr/bin/env bash
# Configure + build MiniSoC.  Usage: scripts/build.sh [Debug|Release]
set -euo pipefail
cd "$(dirname "$0")/.."
TYPE="${1:-Debug}"
cmake -S . -B build -DCMAKE_BUILD_TYPE="$TYPE"
cmake --build build -j"$(nproc)"
echo "built build/minisoc"
