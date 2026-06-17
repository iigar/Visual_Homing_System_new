# ROADMAP — 18 milestones

> Чекбокс = milestone завершений: тести зелені, коміт зроблений, docs оновлені.

- [x] **S0** Bootstrap: repo skeleton, CMake safety options, interfaces, перший тест
- [x] **M1** Replay input: Frame, CSV manifest, PGM P5 Gray8, тести
- [x] **M2** Preprocessing + health: block-average resize, HealthSnapshot, states, harness
- [x] **M3** VHRS v1 route artifact: binary format, little-endian, integrity diagnostics, inspection CLI
- [x] **M4** Route recording: RouteSignatureRecorder, CLI з replay
- [x] **M5** Route matching: Gray8 MAD matcher, direction error, illumination diagnostics
- [x] **M6** Route validation/quality: self-match, perturbation checks, distinctiveness, quality policy, checker script
- [x] **M7** Navigation command model: RouteMatch, NavigationCommand, BoundedNavigator, yaw-rate-only
- [x] **M8** Read-only MAVLink telemetry: v1/v2 framing, heartbeat/attitude/altitude, untrusted input
- [x] **M9** Dry-run MAVLink boundary: DryRunCommandSink, bridge, stale telemetry blocking
- [x] **M10** Camera profiles: FOV, ground footprint, resolution/altitude docs, IMX219 profile
- [x] **M11** Pi hardware capture: libcamera (Trixie), build strategy, скрипти — ⚠ Pi-секція не скомпільована (немає Pi); desktop fail-closed зелений
- [x] **M12** Live route matching dry-run: speed mismatch validation, endpoint action, compact log — DryRunMatchSession + vh_match_session, E2E self-replay passed=1
- [x] **M13** Safety scaffolding: SafetyGate, AuditLog, Session, watchdog stale-data, blocked bridge stub — 15 gate reasons, fail-closed, нуль transmission
- [x] **M14** Readiness checkers: 3 shell checkers, evidence collection — log true/false + gate-derived vehicle_not_armed:N, E2E 150/150 both checkers pass
- [ ] **M15** 3/3 readiness state: повний pre-live evidence
- [ ] **M16** Live-output PLAN (документ, не код) — після ревью
- [ ] **M17** Bench props-off writer library + fail-closed wrapper — після ревью
- [ ] **M18** Visual brake / station-keeping (dry-run-only, окрема фіча)
