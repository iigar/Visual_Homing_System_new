# PROJECT_MEMORY — стиснутий стан

> Перезаписується наприкінці кожної сесії. Ліміт 150 рядків. Журнал — у SESSION_LOG.md.

## Стан: M13 завершено (2026-06-17) — live-output safety scaffolding (нуль transmission)

**Реалізовано (жодного live output):**
- `vh/safety_gate.{hpp,cpp}` — `GateDecision{allowed, block_reasons[]}` (тип з interfaces.hpp визначено). `LiveMavlinkOutputSafetyGate.evaluate(SafetyGateInputs)` → 15 явних reasons (детермінований порядок): runtime_not_enabled, operator_not_confirmed, writer_not_owned, audit_not_ready, dry_run_quality_not_passed, camera_frame_timeout, telemetry_stale, vehicle_not_armed, match_invalid, match_stale, match_low_confidence, command_invalid, command_not_finite, command_out_of_bounds, forward_speed_nonzero. Default config → blocked
- `vh/live_output.{hpp,cpp}`:
  - `LiveMavlinkOutputAuditLog : IAuditLog` — start/decision/stop, can_write=ready&&!write_fail (fail-closed)
  - `LiveMavlinkBridge : ICommandSink` — stub, available/start/send/started усі false (real writer = M17)
  - `LiveMavlinkOutputSession` — start fail-closed; tick→evaluate→audit→dry_sink (history); audit fail→+audit_write_failed+stop; allowed→live_bridge рефузить (live_rejected++, нуль TX); block_reason_counts (reason→count); mark_endpoint→endpoint_progress_reached

**Тести:** 23 CTest desktop, 100% pass. test_safety_gate (17), test_live_output (11). CMake chain fail-closed підтверджено (partial live config → FATAL_ERROR).

**Наступна сесія: M14 (readiness checkers + evidence)**
- 3 shell checkers: check-route-quality-log.sh (вже є з M6 — звірити), check-live-readiness-log.sh, check-live-session-audit-log.sh
- check-live-readiness вимагає: passed=true, frames=150/150, valid_matches=150, endpoint_passed, progress_gate_passed, telemetry_health, telemetry_dropped=0, dry_run_quality, dry_run_valid=150/150, live_output_gate_allowed=0, blocked=150, block reason `vehicle_not_armed:150`
- ⚠ block reason `vehicle_not_armed:N` — M12 match session ЗАРАЗ емітить `live_output_disabled`; для M14 readiness log треба прогнати через M13 gate (або адаптувати) щоб reason став vehicle_not_armed

**⚠ Хвіст M11:** Pi-секція (`#if VH_ENABLE_LIBCAMERA`) НЕ компільована (немає Pi) — `build-test-pi.sh` на Pi перед польовим використанням

## Архітектурні константи (не міняти без DECISIONS.md запису)

- Парадигма: route-following command-assist (yaw-rate-only)
- Single-threaded deterministic pipeline; clock injection скрізь (D-008, D-021)
- Integer-only math у hot paths; float — display projection (D-007, D-014, D-017)
- Navigator fail-closed: провал гейта → zero invalid + reset slew (D-018)
- MAVLink: власний parser, untrusted input, обовʼязкова CRC-валідація, parser НЕ продукує команд (D-019). CRC_EXTRA 50/39/104 + offsets підтверджено Python+NotebookLM (D-020)
- mavlink_ok = свіжий heartbeat; stale/future → fail-closed (D-021)
- Command boundary fail-closed: DryRunCommandSink stopped-by-default, single-writer (D-022). Bridge накладає telemetry freshness на validity (D-023)
- Camera profile FOV → rad-per-pixel для matcher; ground footprint/scale mismatch = DIAGNOSTIC ONLY, не впливають на live команди (D-024)
- Pi capture: двійний гейт (compile VH_ENABLE_LIBCAMERA + runtime open), pimpl, fail-closed; libcamera async→sync ізольовано в backend; VH_ENABLE_LIBCAMERA ≠ live output (D-025)
- M12 match session: speed mismatch (regressions/rollback/index_jumps) — явна validation-змінна, не діє мовчки; endpoint → STOP команд fail-closed; live-output hard-blocked (allowed=0); dry_run_quality+telemetry окремі gates; матчить кадри ВЖЕ у route dims (preprocess вище) (D-026)
- M13 safety scaffolding: gate default-blocked, 15 явних reasons; нуль transmission (allowed впирається в рефузячий LiveMavlinkBridge); audit write fail → block+stop; SafetyGateInputs багатший за ISafetyGate; AuditLog реалізує IAuditLog (D-027)
- VHRS LE через explicit store_le/load_le; digest = FNV-1a 64-bit (non-crypto)
- Each test = окремий CTest executable
- Direction shift: positive = live displaced RIGHT vs reference (D-015)
- vx_mps/vy_mps структурно є, але hard 0.0 до live-output ревью

## Середовище

- Desktop: WSL Ubuntu 24.04, GCC 13.3, CMake 3.28, `./scripts/build-test-desktop.sh`
- Pi: Zero 2W, Pi OS Trixie (GCC 14) — задіюється з M11
- Repo: github.com/iigar/Visual_Homing_System_new, гілка main
- Tools: `build/tools/{vh_route_inspect, vh_route_record, vh_route_quality, vh_mavlink_inspect, vh_camera_profile}`
- NotebookLM: notebook `851a3eee`. `./scripts/notebook-ask.sh "..."` через Git Bash (НЕ WSL). Auth протухає ~10хв → юзеру `! python -m notebooklm login`. notebook-add-doc.sh idempotent (dedup by ID).

## План сесій

S0✅ → M1-M10✅ → **M11(Pi)** → M12(Pi) → M13+M14 → M15(Pi+FC) → M16-M18(live-output, після ревью)

## Вивчені пастки (всі сесії)

- C++ most vexing parse → braces; ambiguous `{}` overload → named var
- Health Booting→Degraded на першому "поганому" кадрі
- `std::optional` не → bool implicit (test helper templated)
- Забутий `<limits>`/`<cmath>`/`<bit>`/`<string>`/`<cstring>`/`<vector>` include — кожен test/tool явно інклюдить що вживає
- Sign convention direction shift — consistent між helper і kernel
- WSL/NTFS "Clock skew"/"modification time in the future" warning — нешкідливе
- Integer bounding: int64 проміжне щоб int32 не переповнював
- MAVLink deferred emit: signed-frame треба буферити (pending_), бо `out` не персистить між parse_byte
- MAVLink v2 truncation: payload буфер zero-fill, декодер читає фіксовані offsets безпечно
- Ring buffer history: oldest = (head + cap - size) % cap; seq лічильник окремо від retention
- Camera profile: зберігання key=value (нуль залежностей), JSON лише на вихід; FOV nominal ≠ measured (реальна лінза/crop)
- Pi capture pimpl: libcamera headers ТІЛЬКИ в .cpp під `#if`; compile-define PUBLIC (інакше pimpl member layout розходиться між TU); validate config ПЕРЕД compile-гейтом (детермінізм desktop/Pi); код під `#if VH_ENABLE_LIBCAMERA` desktop НЕ компілює → перевіряти на Pi окремо
