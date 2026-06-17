#!/usr/bin/env bash
# Readiness checker for the vh_live_session audit log (prompt M14). The log is
# one `audit_event=...` line per record. Pass criteria: exactly one start, N
# decision events, every decision blocked for vehicle_not_armed with a valid
# zero-forward-speed command, and exactly one endpoint stop.
#
# Usage:
#   ./scripts/check-live-session-audit-log.sh <log_file> [expected_command_count]
# expected_command_count defaults to 150 (or $VISUAL_HOMING_EXPECTED_FRAMES).
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: $0 <log_file> [expected_command_count]" >&2
  exit 2
fi
LOG="$1"
EXP="${2:-${VISUAL_HOMING_EXPECTED_FRAMES:-150}}"
if [[ ! -f "$LOG" ]]; then
  echo "FAIL log_file_missing=$LOG" >&2
  exit 2
fi

count() { grep -cE "$1" "$LOG" || true; }

fails=0
require() {
  local label="$1" got="$2" want="$3"
  if [[ "$got" != "$want" ]]; then
    echo "FAIL $label expected=$want got=$got"
    fails=$((fails + 1))
  fi
}

starts="$(count '^audit_event=start ')"
decisions="$(count '^audit_event=decision ')"
allowed_false="$(count '^audit_event=decision .*allowed=false ')"
allowed_true="$(count '^audit_event=decision .*allowed=true ')"
reason_armed="$(count '^audit_event=decision .*reason=vehicle_not_armed ')"
valid_true="$(count '^audit_event=decision .*valid=true ')"
vx_zero="$(count '^audit_event=decision .*vx_mps=0 ')"
stop_endpoint="$(count '^audit_event=stop reason=(endpoint_progress_reached|match_live_route_complete)$')"

require start_events "$starts" 1
require decision_events "$decisions" "$EXP"
require allowed_false "$allowed_false" "$EXP"
require allowed_true "$allowed_true" 0
require reason_vehicle_not_armed "$reason_armed" "$EXP"
require valid_true "$valid_true" "$EXP"
require vx_mps_zero "$vx_zero" "$EXP"
require endpoint_stop_events "$stop_endpoint" 1

if [[ $fails -ne 0 ]]; then
  echo "check_live_session_audit_log=fail failures=$fails" >&2
  exit 1
fi
echo "check_live_session_audit_log=pass commands=$EXP"
exit 0
