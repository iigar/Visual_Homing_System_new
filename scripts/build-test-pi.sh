#!/usr/bin/env bash
# Pi build + test with the libcamera capture backend enabled (M11).
# Run ON the Raspberry Pi (Pi OS Trixie, libcamera dev packages installed).
#
# Live-output CMake options are still passed explicitly OFF — enabling the
# hardware capture backend must never imply any live MAVLink output. Only
# VH_ENABLE_LIBCAMERA flips ON here.
#
# A dedicated build dir keeps the Pi (native, slow on a Zero 2W) incremental
# build separate from the desktop/WSL build; it is gitignored.
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="build-pi"

cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DVH_ENABLE_LIVE_OUTPUT=OFF \
  -DVH_ENABLE_BENCH_PROPS_OFF=OFF \
  -DVH_ENABLE_WRITER_ATTACH=OFF \
  -DVH_ENABLE_LIBCAMERA=ON \
  -DVH_ENABLE_SERIAL_TELEMETRY=OFF

cmake --build "$BUILD_DIR" --parallel

ctest --test-dir "$BUILD_DIR" --output-on-failure
