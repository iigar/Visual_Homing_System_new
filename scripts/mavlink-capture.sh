#!/usr/bin/env bash
# Capture raw MAVLink bytes from a Pi serial device to a file (prompt M8).
# READ-ONLY: configures the port and reads bytes; never writes to the link.
#
# Usage:
#   ./scripts/mavlink-capture.sh [device] [seconds] [outfile]
# Defaults:
#   device=/dev/serial0  seconds=5  outfile=mavlink-capture.bin
#
# Example (Pi):
#   ./scripts/mavlink-capture.sh /dev/serial0 5 /tmp/cap.bin
set -euo pipefail

DEV="${1:-/dev/serial0}"
SECS="${2:-5}"
OUT="${3:-mavlink-capture.bin}"
BAUD="${MAVLINK_BAUD:-115200}"

if [[ ! -e "$DEV" ]]; then
  echo "capture_error=device_missing dev=$DEV" >&2
  exit 2
fi

# Raw mode, no echo, fixed baud. -F selects the device on Linux.
stty -F "$DEV" raw -echo "$BAUD" 2>/dev/null || {
  echo "capture_error=stty_failed dev=$DEV baud=$BAUD" >&2
  exit 2
}

echo "capturing dev=$DEV baud=$BAUD seconds=$SECS -> $OUT" >&2
# timeout ends the read cleanly; cat never writes back to the port.
timeout "$SECS" cat "$DEV" > "$OUT" || true

BYTES=$(wc -c < "$OUT" | tr -d ' ')
echo "captured_bytes=$BYTES file=$OUT" >&2
