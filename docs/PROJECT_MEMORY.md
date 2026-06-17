# PROJECT_MEMORY — стиснутий стан

> Перезаписується наприкінці кожної сесії. Ліміт 150 рядків. Журнал — у SESSION_LOG.md.

## Стан: M12 завершено (2026-06-17) — live route matching dry-run

**Реалізовано:**
- `vh/match_session.{hpp,cpp}` — `DryRunMatchSession` над DryRunBridge (M9)
  - `step_match(RouteMatch)` (тести) / `step(Frame)` (матчить через matcher M5) → акумуляція evidence
  - ExpectedProgress any/forward/reverse; progress first/last/min/max; regressions + rollback_total_mille + index_jumps = speed mismatch як явна validation-змінна
  - Endpoint gate (fwd: progress≥gate; rev: ≤1000−gate) → STOP команд + stop_reason=endpoint_reached, наступні кадри без команд (fail-closed)
  - live-output межа hard-closed: allowed=0, blocked=N, reason `live_output_disabled`; dry_run_quality (M6) + telemetry health окремі gates
  - `format_compact_log` (всі поля промпту M12); MatchSessionResult.passed = AND усіх gates
- `tools/vh_match_session` — route.vhrs + frames.csv → compact log; --expected/--endpoint-gate/--min-confidence/--fps/--quality-pass/--synthetic-heartbeat; exit 0 iff passed

**Тести:** 21 CTest desktop, 100% pass. test_match_session (10 cases). E2E self-replay: passed=1, frames=5/5, progress 0..1000, endpoint_passed=1, live_output_gate_allowed=0/blocked=5.

**Наступна сесія: M13 (non-live live-output safety scaffolding)**
- `LiveMavlinkOutputSafetyGate`: runtime enable + operator confirm + single-writer + audit ready + dry-run quality + fresh telemetry + valid/fresh/high-conf match + finite bounded command + exact zero forward speed; explicit block reasons
- Усе ще НЕ live output — це лише gate-логіка (AuditLog/SafetyGate/Session)

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
