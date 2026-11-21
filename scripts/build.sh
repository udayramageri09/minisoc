#!/usr/bin/env bash
# Configure + build MiniSoC.  Usage: scripts/build.sh [Debug|Release] [--asan]
set -euo pipefail
cd "$(dirname "$0")/.."
TYPE="${1:-Debug}"
EXTRA=()
[[ "${2:-}" == "--asan" ]] && EXTRA+=(-DMINISOC_SANITIZE=ON)
BUILD_DIR="build"
[[ "${2:-}" == "--asan" ]] && BUILD_DIR="build-asan"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$TYPE" "${EXTRA[@]}"
cmake --build "$BUILD_DIR" -j"$(nproc)"
echo "built $BUILD_DIR/minisoc"
