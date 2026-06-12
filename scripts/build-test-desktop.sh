#!/usr/bin/env bash
# Ordinary desktop build + test. Live-output CMake options are passed
# explicitly OFF so a stale cache can never silently enable live output.
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="build"

cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DVH_ENABLE_LIVE_OUTPUT=OFF \
  -DVH_ENABLE_BENCH_PROPS_OFF=OFF \
  -DVH_ENABLE_WRITER_ATTACH=OFF \
  -DVH_ENABLE_LIBCAMERA=OFF \
  -DVH_ENABLE_SERIAL_TELEMETRY=OFF

cmake --build "$BUILD_DIR" --parallel

ctest --test-dir "$BUILD_DIR" --output-on-failure
