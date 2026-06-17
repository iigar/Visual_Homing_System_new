#!/usr/bin/env bash
# Readiness checker for the vh_match_session compact log (prompt M14).
# The log is a single space-separated key=value line. Pass criteria mirror the
# prompt: a clean dry-run with all frames matched, endpoint reached, telemetry
# healthy, dry-run commands valid, and zero live-output allowed — every frame
# blocked for exactly vehicle_not_armed.
#
# Usage:
#   ./scripts/check-live-readiness-log.sh <log_file> [expected_frame_count]
# expected_frame_count defaults to 150 (or $VISUAL_HOMING_EXPECTED_FRAMES).
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: $0 <log_file> [expected_frame_count]" >&2
  exit 2
fi
LOG="$1"
EXP="${2:-${VISUAL_HOMING_EXPECTED_FRAMES:-150}}"
if [[ ! -f "$LOG" ]]; then
  echo "FAIL log_file_missing=$LOG" >&2
  exit 2
fi

# Extract a whole-word KEY=VALUE token from the single-line log.
tok() { grep -oE "(^| )$1=[^ ]+" "$LOG" | head -n1 | sed -E "s/^ ?$1=//"; }

fails=0
check_eq() {
  local key="$1" want="$2" got
  got="$(tok "$key")"
  if [[ "$got" != "$want" ]]; then
    echo "FAIL $key expected=$want got=${got:-<missing>}"
    fails=$((fails + 1))
  fi
}

check_eq passed true
check_eq frames "$EXP/$EXP"
check_eq valid_matches "$EXP"
check_eq endpoint_passed true
check_eq progress_gate_passed true
check_eq telemetry_health true
check_eq telemetry_dropped 0
check_eq dry_run_quality true
check_eq dry_run_valid "$EXP/$EXP"
check_eq live_output_gate_allowed 0
check_eq live_output_gate_blocked "$EXP"
check_eq live_output_gate_block_reasons "vehicle_not_armed:$EXP"

if [[ $fails -ne 0 ]]; then
  echo "check_live_readiness_log=fail failures=$fails" >&2
  exit 1
fi
echo "check_live_readiness_log=pass frames=$EXP"
exit 0
