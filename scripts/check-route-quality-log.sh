#!/usr/bin/env bash
# Readiness checker for `vh_route_quality` output (prompt M6).
# Pass-criteria: self-match pass, perturbation pass, malformed rejection,
# zero exact duplicates, quality_pass=true.
#
# Usage:
#   ./scripts/check-route-quality-log.sh <log_file>
#
# Optional environment variables:
#   VISUAL_HOMING_EXPECTED_ROUTE_QUALITY_ENTRIES   strict entry-count check
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <log_file>" >&2
  exit 2
fi
LOG="$1"
if [[ ! -f "$LOG" ]]; then
  echo "FAIL log_file_missing=$LOG" >&2
  exit 2
fi

extract() {
  grep -E "^$1=" "$LOG" | tail -n1 | cut -d= -f2- || true
}

require_eq() {
  local key="$1" expected="$2"
  local got
  got="$(extract "$key")"
  if [[ "$got" != "$expected" ]]; then
    echo "FAIL $key expected=$expected got=${got:-<missing>}"
    return 1
  fi
}

require_match() {
  local k1="$1" k2="$2"
  local v1 v2
  v1="$(extract "$k1")"
  v2="$(extract "$k2")"
  if [[ -z "$v1" || -z "$v2" || "$v1" != "$v2" ]]; then
    echo "FAIL $k1=$v1 vs $k2=$v2"
    return 1
  fi
}

require_zero() {
  local key="$1" got
  got="$(extract "$key")"
  if [[ "$got" != "0" ]]; then
    echo "FAIL $key expected=0 got=${got:-<missing>}"
    return 1
  fi
}

fails=0
require_eq quality_pass true                            || fails=$((fails + 1))
require_match self_match_checked self_match_exact        || fails=$((fails + 1))
require_match perturbation_checked perturbation_malformed_rejected \
                                                         || fails=$((fails + 1))
require_zero distinctiveness_exact_duplicates           || fails=$((fails + 1))
require_eq self_match_progress_monotonic true           || fails=$((fails + 1))

if [[ -n "${VISUAL_HOMING_EXPECTED_ROUTE_QUALITY_ENTRIES:-}" ]]; then
  require_eq entry_count "$VISUAL_HOMING_EXPECTED_ROUTE_QUALITY_ENTRIES" \
    || fails=$((fails + 1))
fi

if [[ $fails -ne 0 ]]; then
  echo "check_route_quality_log=fail failures=$fails" >&2
  exit 1
fi
echo "check_route_quality_log=pass"
exit 0
