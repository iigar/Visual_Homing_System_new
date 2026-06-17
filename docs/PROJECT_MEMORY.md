# PROJECT_MEMORY — стиснутий стан

> Перезаписується наприкінці кожної сесії. Ліміт 150 рядків. Журнал — у SESSION_LOG.md.

## Стан: M14 завершено (2026-06-17) — readiness checkers + evidence

**Реалізовано:**
- Гармонізація логу: `format_compact_log` bool → `true/false`, block reasons → `reason:count` (map-sorted)
- Інтеграція M13 gate у DryRunMatchSession: `set_live_output_gate(gate, LiveOutputContext{single_writer_owned, audit_ready})` — per-frame SafetyGateInputs (health+bridge.telemetry+match+command+now) → real allowed/blocked + reason counts; без gate fallback `live_output_disabled`
- `live_output`: AuditRecord +command_valid+vx_mps; `LiveMavlinkOutputAuditLog::format_log()` (one event/line)
- CLI: `vh_match_session --readiness` (gate operator-confirmed + disarmed fresh heartbeat → vehicle_not_armed:N); НОВИЙ `vh_live_session` (драйвить LiveMavlinkOutputSession → audit log; endpoint→mark_endpoint)
- `scripts/check-live-readiness-log.sh` + `check-live-session-audit-log.sh` (параметризовані expected count, дефолт 150); `check-route-quality-log.sh` вже з M6
- `docs/LIVE_OUTPUT_READINESS_RECORD.md` — evidence ledger (3/3 Pi слоти для M15 + desktop reference)

**Тести:** 23 CTest desktop, 100% pass (test_match_session 11, test_live_output 12). **E2E 150-кадровий self-replay:** readiness passed=true frames=150/150 … vehicle_not_armed:150; audit 1 start+150 decisions+1 endpoint stop; обидва checkers pass.

**Наступна сесія: M15 (3/3 readiness state) — ПОТРЕБУЄ Pi**
- Передумова: M11 Pi bring-up (build-test-pi.sh на Pi, fix compile, реальний захват)
- Зібрати 3 чисті Pi evidence logs у RECORD.md: route quality_pass + 150/150 dry-run + endpoint/progress gate + telemetry health + 150 blocked vehicle_not_armed; CTest на Pi
- Після 3/3: оновити roadmap/safety plan/decisions/session log/memory. Live output ЛИШАЄТЬСЯ blocked

**⚠ Хвіст M11:** Pi-секція (`#if VH_ENABLE_LIBCAMERA`) НЕ компільована (немає Pi) — Pi bring-up = окрема сесія ПЕРЕД M15

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
- M14 evidence: лог-формат true/false + reason:count; vehicle_not_armed ЧЕСНИЙ через gate-інтеграцію (не штамп); endpoint gate=1000 у evidence → endpoint на фінальному кадрі (150 decisions, без post-endpoint tail); evidence ≠ дозвіл на live output (D-028)
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
