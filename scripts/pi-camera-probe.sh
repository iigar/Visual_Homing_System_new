#!/usr/bin/env bash
# Read-only Pi camera probe (M11). Lists cameras libcamera can see and the
# formats the primary camera advertises — used to confirm an IMX219 is wired up
# and that R8 (Gray8) is available before configuring capture. Captures nothing.
#
# Usage: ./scripts/pi-camera-probe.sh
set -euo pipefail

echo "== libcamera camera list =="
if command -v rpicam-hello >/dev/null 2>&1; then
  rpicam-hello --list-cameras || true
elif command -v libcamera-hello >/dev/null 2>&1; then
  libcamera-hello --list-cameras || true
elif command -v cam >/dev/null 2>&1; then
  cam --list || true
  echo
  echo "== camera 0 formats =="
  cam --camera 1 --list-properties || true
else
  echo "probe_error=no_libcamera_tool" >&2
  echo "install one of: rpicam-apps (rpicam-hello), libcamera-tools (cam)" >&2
  exit 2
fi
