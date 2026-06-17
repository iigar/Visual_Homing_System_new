# LIVE_OUTPUT_READINESS_RECORD

> Dry-run evidence ledger (prompt M14). Collects clean route-following dry-run
> logs before any live-output blocker change. **Evidence completion is NOT
> permission to enable live output.** Live output stays blocked until the M16
> reviewed bench-props-off plan and the explicit CMake attach chain.

## What counts as one clean evidence run

Three artifacts per run, all green:

1. **Route quality** — `vh_route_quality <route.vhrs>` → `check-route-quality-log.sh` → `quality_pass=true`.
2. **Live match dry-run readiness** — `vh_match_session <route> <frames.csv> --readiness --endpoint-gate 1000 --min-confidence 800 --fps <fps>` → `check-live-readiness-log.sh <log> <N>`.
3. **Live-output session audit** — `vh_live_session <route> <frames.csv> --min-confidence 800 --endpoint-gate 1000` → `check-live-session-audit-log.sh <log> <N>`.

Readiness pass criteria (N frames): `passed=true`, `frames=N/N`, `valid_matches=N`,
`endpoint_passed=true`, `progress_gate_passed=true`, `telemetry_health=true`,
`telemetry_dropped=0`, `dry_run_quality=true`, `dry_run_valid=N/N`,
`live_output_gate_allowed=0`, `live_output_gate_blocked=N`,
`live_output_gate_block_reasons=vehicle_not_armed:N`.

Audit pass criteria: 1 start, N decision events (every `allowed=false`,
`reason=vehicle_not_armed`, `valid=true`, `vx_mps=0`), 1 stop
`reason=endpoint_progress_reached`.

## Required for the M15 3/3 state

- [ ] Pi evidence run 1 (on Raspberry Pi, real capture or replay) — date, route, logs
- [ ] Pi evidence run 2
- [ ] Pi evidence run 3

> The 3/3 evidence MUST be collected on the Pi (after the M11 libcamera bring-up
> — see PROJECT_MEMORY Pi-хвіст). The desktop reference below proves the
> tooling and checkers, not the 3/3 Pi state.

## Desktop reference run (tooling proof, NOT a 3/3 Pi log)

- **Date:** 2026-06-17
- **Platform:** WSL Ubuntu 24.04 (desktop), self-replay of a 150-frame 16×16 synthetic route
- **Scenario:** bench-readiness — operator authority granted, single writer owned,
  audit ready, dry-run quality passed, telemetry fresh, vehicle **disarmed** (props off).

Readiness log (`check-live-readiness-log.sh` → pass):

```
passed=true frames=150/150 effective_fps=15.0 configured_fps=15 elapsed_ms=9933 valid_matches=150 progress=0..1000 progress_first=0 progress_last=1000 regressions=0 rollback_total=0 index_jumps=0 endpoint_passed=true progress_gate_passed=true confidence_min_avg=1000/1000 telemetry_health=true telemetry_dropped=0 dry_run_quality=true dry_run_valid=150/150 live_output_gate_allowed=0 live_output_gate_blocked=150 live_output_gate_block_reasons=vehicle_not_armed:150 stop_reason=endpoint_reached
```

Audit log (`check-live-session-audit-log.sh` → pass; 152 lines, head/tail shown):

```
audit_event=start reason=live_match_dry_run
audit_event=decision allowed=false reason=vehicle_not_armed valid=true vx_mps=0 yaw_rate_microradps=0 confidence_mille=1000
... (150 decision events total) ...
audit_event=stop reason=endpoint_progress_reached
```

Both checkers returned `pass` (exit 0). Live output remained blocked on every
frame (`live_output_gate_allowed=0`); nothing was transmitted.
