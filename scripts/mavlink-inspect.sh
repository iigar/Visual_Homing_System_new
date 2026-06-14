#!/usr/bin/env bash
# Inspect/validate MAVLink telemetry from a Pi serial device or a saved
# capture, emitting the stable key=value summary from vh_mavlink_inspect
# (prompt M8). READ-ONLY end to end — no bytes are ever sent to the link.
#
# Usage:
#   ./scripts/mavlink-inspect.sh <device-or-file> [seconds]
#
# If the argument is a character device, it is captured for `seconds` (default
# 5) and then inspected; if it is a regular file, it is inspected directly.
#
# Optional environment variables:
#   VH_MAVLINK_INSPECT   path to the vh_mavlink_inspect binary
#                        (default: build/tools/vh_mavlink_inspect)
#   MAVLINK_EXPECTED_SYSID   pass --sysid <n> to filter foreign traffic
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <device-or-file> [seconds]" >&2
  exit 2
fi

SRC="$1"
SECS="${2:-5}"
BIN="${VH_MAVLINK_INSPECT:-build/tools/vh_mavlink_inspect}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ ! -x "$BIN" && -x "$HERE/$BIN" ]]; then
  BIN="$HERE/$BIN"
fi
if [[ ! -x "$BIN" ]]; then
  echo "inspect_error=binary_missing path=$BIN" >&2
  exit 2
fi

ARGS=()
if [[ -n "${MAVLINK_EXPECTED_SYSID:-}" ]]; then
  ARGS=(--sysid "$MAVLINK_EXPECTED_SYSID")
fi

if [[ -c "$SRC" ]]; then
  # Character device: capture to a temp file first (read-only), then inspect.
  TMP="$(mktemp)"
  trap 'rm -f "$TMP"' EXIT
  "$HERE/scripts/mavlink-capture.sh" "$SRC" "$SECS" "$TMP" >&2
  "$BIN" "${ARGS[@]}" < "$TMP"
elif [[ -f "$SRC" ]]; then
  "$BIN" "${ARGS[@]}" < "$SRC"
else
  echo "inspect_error=source_not_found src=$SRC" >&2
  exit 2
fi
