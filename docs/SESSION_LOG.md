# SESSION_LOG — журнал сесій

> Append-only. Новий запис зверху.

## 2026-06-16 — M11 (Pi hardware capture — libcamera Trixie)

**Зроблено (1 модуль pimpl, 1 тест, 2 Pi-скрипти; 20/20 CTest desktop зелені):**

| Компонент | Що |
|-----------|-----|
| `pi_camera.{hpp,cpp}` | `PiCameraSource : ICameraSource`. PiCameraConfig (capture w/h, buffer_count, frame_timeout_ms, camera_index), PiCameraError (11 станів), validate_pi_camera_config. Pimpl ховає libcamera headers від public API. Desktop: fail-closed stub (`open()` → NotCompiledIn, `next_frame()` без open → NotStarted). Pi (`#if VH_ENABLE_LIBCAMERA`): CameraManager→acquire→R8 stream@capture dims→FrameBufferAllocator→Request, async `requestCompleted` → sync `next_frame()` через mutex+cv+черга, mmap-per-fd + row-copy зі stride. validate() Adjusted format/dims ≠ запит → ConfigureFailed (fail-closed) |
| CMake | `VH_ENABLE_LIBCAMERA` PUBLIC define (ABI: умовний pimpl member). ON → pkg-config libcamera link; OFF → define=0, нуль залежності |
| `scripts/build-test-pi.sh` | Pi build (build-pi/, Release), libcamera ON, всі live-output опції явно OFF |
| `scripts/pi-camera-probe.sh` | READ-ONLY: список камер + R8 формат (rpicam-hello/cam) |

**Тести (test_pi_camera):** config validation (6 reject paths), open invalid-config → InvalidConfig, next_frame before open → NotStarted, close idempotent, error strings, desktop fail-closed (open → NotCompiledIn).

**Рішення:** D-025 (двійний гейт compile+runtime, async→sync міст ізольовано в backend, fail-closed; VH_ENABLE_LIBCAMERA НЕ передбачає live output).

**Крос-валідація:** libcamera 0.3+ Trixie API через NotebookLM (relogin знадобився) — formats::R8, plane.fd().get() SharedFD, stride row-copy.

**⚠ Хвіст:** Pi-секція (`#if VH_ENABLE_LIBCAMERA`) НЕ компілювалась цю сесію (немає Pi). Обовʼязково `build-test-pi.sh` на реальному Pi перед M12/польовим використанням — можливі дрібні API-правки.

**Наступне:** M12 (live route matching dry-run — speed mismatch validation, endpoint action, compact log).

## 2026-06-15 — M10 (camera profiles)

**Зроблено (1 модуль, 1 тест +11 cases, 1 CLI, 1 doc; 19/19 CTest зелені):**

| Компонент | Що |
|-----------|-----|
| `camera_profile.{hpp,cpp}` | CameraProfile (id, sensor visible/thermal/other, capture+target dims, pixel format, h/v FOV, matcher+quality thresholds, mean_normalise). validate_profile, key=value parse/format, profile_to_json. FOV→rad-per-pixel (capture/target) + matcher_microrad_per_pixel. compute_ground_footprint (2·h·tan(fov/2), reject non-finite/non-pos). visual_scale_mismatch (diagnostic). to_matcher_config/to_quality_policy. imx219_profile built-in. ProfileRegistry (list/get/set_active) |
| `tools/vh_camera_profile` | validate / json / footprint commands |
| `docs/CAMERA_PROFILES.md` | resolution/altitude relationship + diagnostic-first safety |

**Тести:** validation (5 reject paths), text roundtrip, bad-sensor reject, json, rad-per-pixel, ground footprint + bad-altitude reject, visual scale mismatch, to_matcher_config, imx219 builtin, registry. E2E CLI demo: IMX219 @30m → ground 40.7×27.2m, 0.64 m/px target, microrad/px 18641.

**Рішення:** D-024 (FOV-derived rad-per-pixel; ground footprint/scale mismatch = DIAGNOSTIC ONLY, не впливають на live команди).

**Наступне:** M11 (Pi hardware capture — libcamera Trixie, compile/runtime gates, desktop fail-closed без live capture). Перший milestone що виходить на Pi.

## 2026-06-15 — M9 (dry-run MAVLink boundary)

**Зроблено (2 шари, 2 нові тести +11 cases; 18/18 CTest зелені):**

| Шар | Що |
|-----|-----|
| `command_sink.{hpp,cpp}` | DryRunCommandSink (ICommandSink): start/stop, stopped-by-default reject, single-writer (start вдруге → false), bounded ring history (дефолт 64) + counters (accepted/accepted_valid/rejected_stopped), all-time seq. НІЧОГО не передає |
| `dry_run_bridge.{hpp,cpp}` | DryRunBridge: MavlinkTelemetry + BoundedNavigator + sink. tick() накладає telemetry freshness/compat на health → stale/incompatible FC = navigator відмовляє valid command. Counters: ticks/blocked_stale/blocked_incompatible/commands_valid/invalid. telemetry() expose armed/mode/attitude/rel-alt |

**Тести:** test_command_sink (5: stopped-reject, single-writer, record+count, bounded-drop-oldest, stop-then-reject), test_dry_run_bridge (6: telemetry polling, fresh→valid, stale blocks, never-heartbeat blocks, disarmed+require_armed→incompatible, sink-stopped→rejects-record).

**Архітектурні рішення:** D-022 (fail-closed command boundary, stopped-by-default, single-writer), D-023 (bridge накладає telemetry freshness на command validity через існуючий navigator gate).

**Live MAVLink output лишається недоступним і fail-closed.** Жодних live-output опцій не торкався.

**Наступне:** M10 (camera profiles — FOV, ground footprint, rad-per-pixel для matcher direction error, IMX219 профіль, JSON для UI/API).

## 2026-06-14 — S5: M8 (read-only MAVLink telemetry)

**Зроблено (3 шари, 2 нові тести +19 cases, 1 CLI, 2 Pi-скрипти; 16/16 CTest зелені):**

| Шар | Що |
|-----|-----|
| `mavlink.{hpp,cpp}` | byte-streaming parser v1(0xFE)/v2(0xFD); CRC-16/MCRF4XX + per-msg CRC_EXTRA; state machine; counters (bytes_seen/frames_ok/crc_error/unknown_msgid/bytes_discarded); v2 signed-frame consume; deferred emit через pending_ буфер |
| `telemetry.{hpp,cpp}` | декодери HEARTBEAT/ATTITUDE/GLOBAL_POSITION_INT; TelemetrySnapshot; copter mode labels; armed з base_mode&0x80; MavlinkTelemetry (ITelemetrySource) з expected_sysid фільтром; freshness→mavlink_ok через update(now) |
| `tools/vh_mavlink_inspect` + `mavlink-capture.sh` + `mavlink-inspect.sh` | CLI читає stdin → stable key=value; Pi серійний capture (/dev/serial0 @115200, READ-ONLY) + inspect wrapper |

**Тести:** test_mavlink (10: v1/v2 roundtrip, truncation, CRC/payload corruption reject, unknown msgid, garbage resync, partial, signed consume, back-to-back), test_telemetry (9: armed/mode, disarmed/unknown, freshness stale, future fail-closed, attitude, position, wrong-sysid filter, CRC-fail no-update, mixed stream).

**Крос-валідація (NotebookLM був down):** незалежна Python-реалізація CRC будує кадри → C++ парсер приймає (crc_error=0) і декодує правильно. Підтверджено CRC_EXTRA 50/39/104 + offsets для всіх 3 повідомлень. End-to-end demo через vh_mavlink_inspect.

**Архітектурні рішення (D-019…D-021):** untrusted input + обовʼязковий CRC; крос-валідація замість notebook; freshness→mavlink_ok через clock injection.

**Інфра-нотатки:** dedup notebook-add-doc.sh (delete-by-ID), Stop-hook anti-loop (stop_hook_active), очистка диску C: (+10GB; видалення ms-playwright тимчасово зламало notebook login — відновлено `playwright install chromium`). NotebookLM Google-сесія протухає ~10хв.

**Наступне:** S5 продовження = M9 (DryRunCommandSink + dry-run MAVLink bridge, stale-telemetry blocking, compact log).

## 2026-06-14 — S4: M7 (BoundedNavigator — navigation command model)

**Зроблено (1 новий тест, 11 test cases; 14/14 CTest зелені):**

| Модуль | Тести / поведінка |
|--------|-------------------|
| `navigation_command.hpp` (M7) | новий тип: integer-authoritative `yaw_rate_microradps` + float `yaw_rate_radps` projection; `vx_mps`/`vy_mps` hard-wired 0.0 |
| `navigator` (M7) | bounding kernels (yaw_rate_from_error / clamp_symmetric / slew_limit) integer-exact + overflow-safe; happy path (err 100mrad × gain 500 → 50000 µrad/s, vx=vy=0); zero-forward-speed policy при великій помилці; gates: low confidence, stale match (+ future-stamp negative age + inclusive edge), invalid match, degraded health (non-Ready + кожен stage flag окремо), non-finite floats (NaN confidence / Inf progress / NaN health conf); clamp обмежує величезну помилку; slew ramps 30k→60k→90k→100k hold; reset-after-invalid обнуляє slew-памʼять, recovery стартує з 0 |

**Архітектурні рішення (D-017, D-018):**
- D-017: yaw rate integer-authoritative на microrad/s; `gain_milli` дає точний integer multiply `error_millirad × gain_milli`; float — display
- D-018: будь-який провал гейта → zero invalid + reset slew-памʼяті; future-stamped match (negative age) відхиляється

**Жодного CLI у M7** — навігаційний вивід прийде з DryRunCommandSink (M9). Жодних live-output опцій не торкався.

**Наступне:** S5 = M8 (read-only MAVLink telemetry v1/v2) + M9 (dry-run MAVLink boundary).

## 2026-06-14 — S3: M5 + M6 (route matching + quality)

**Зроблено (2 нових тести з 30 окремими test cases + 1 CLI tool + 1 checker script):**

| Модуль | Тести / поведінка |
|--------|-------------------|
| `route_matcher` (M5) | sum_abs_diff identity/known, mad_confidence_mille extremes/midpoint, mean_gray8 uniform/halves, aligned match → confidence 1000, brightness offset без normalisation → low confidence, з normalisation → recovered, left/right shift → coherent direction_error, low-confidence → no direction emitted, dimension mismatch, window restricts search, empty route |
| `direction_error` (M5) | aligned shift=0, find -2 offset, shift_px_to_millirad round half-up |
| `route_quality` (M6) | self-match clean = exact, duplicates lose exactness, empty route; perturbation brightness with normalisation passes, malformed always rejected, low-amp noise keeps match; distinctiveness uniform = low texture, exact duplicates detected, diverse route passes, edge_trim excludes boundary; quality verdict pass for clean, fail for duplicates, fail for low-texture |
| CLI/script | `vh_route_quality` stable key=value output; `check-route-quality-log.sh` readiness gate. End-to-end PASS: diverse route → quality_pass=true → checker pass. End-to-end FAIL: duplicate route → 4 explicit failures → checker fails correctly |

**Архітектурні рішення (D-014…D-016):**
- D-014: RouteMatch містить і integer mille і float repr — float тільки для display, всі gates на integer
- D-015: Direction shift sign convention зафіксована: positive shift_px = live displaced RIGHT vs reference (відповідає параметру `s` у `shift_horizontal(in, +s)`)
- D-016: Quality policy thresholds є per-deployment — у тестах з малими 8×8 patterns ambiguous threshold послаблюється; у виробництві (64×48) дефолти жорсткі

**Пастки знайдені і виправлені:**
- Sign mismatch між тестовим shift_horizontal і моїм direction kernel — узгодили через explicit negation
- Synthetic 8×8 patterns мають високу ambiguity — для test зробив generator з різнішими seeds + явний chequerboard для "garbage" frame

**Наступне:** S4 = M7 (BoundedNavigator: RouteMatch + HealthSnapshot → NavigationCommand yaw-rate-only).

## 2026-06-13 — S2: M3 + M4 (VHRS format + recording)

**Зроблено (4 нові тести + 2 CLI tools, всі зелені):**

| Модуль | Тести / поведінка |
|--------|-------------------|
| `endian` + `digest` | LE roundtrip u16/u32/u64/signed, FNV-1a відомі RFC значення ("", "a", "foobar"), streaming = oneshot, single-bit flip detection |
| `route_io` (VHRS v1) | round-trip 3 entries, reject invalid/oversized, bad magic, short file, wrong version (with valid digest), header digest tamper detection, post-finalize truncation, trailing bytes, file digest detects payload flip, unsupported pixel format, already-finalized writer, unknown metadata roundtrip, empty route |
| `route_inspect` | uniform route з 5 entries: dimensions, monotonicity, altitude/heading min/max ranges, stable key=value text output |
| `route_recorder` | 3-frame round-trip з PoseHint, invalid frame counted as rejected, record after finalize refused, unknown pose round-trip |
| CLI end-to-end | 5 PGM 8x8 → manifest → `vh_route_record --target 4x4` → `vh_route_inspect` (`record_ok=true`, всі поля коректно)|

**Архітектурні рішення:**
- D-010: FNV-1a 64-bit (NOT cryptographic) для VHRS integrity diagnostics. Sufficient для виявлення випадкового пошкодження і простого локального tampering. Криптографічна довіра потребує signed metadata (відкладено).
- D-011: VHRS = 32-byte file header + 40-byte entry header, всі multi-byte LE. Header містить magic+version+flags+entry_count+header_digest (12 bytes covered) + 16 reserved zero bytes.
- D-012: Integer-only metadata: altitude_mm (u32), heading_millirad (i32). Sentinel values для unknown.
- D-013: Test helper тепер templated — приймає будь-що contextually convertible to bool.

**Пастки знайдені і виправлені:**
- Забутий `#include <limits>` для `numeric_limits<T>::min()`
- `std::optional` як аргумент implicit-bool помічника
- Test файли не інклюдять headers які використовують напряму (через interfaces.hpp їх не вистачає)

**Наступне:** S3 = M5 (Gray8RouteMatcher + direction error) + M6 (route quality policy + checker script).

## 2026-06-13 — S1: M1 + M2 (replay input + preprocessing + health)

**Зроблено (7 нових тестів, всі зелені):**

| Модуль | Тести |
|--------|-------|
| `manifest` | basic, comments, no header, malformed row, non-numeric id, negative ts, non-monotonic, duplicate id, empty path, empty file, base_dir resolution, CRLF |
| `pgm` | basic 4x2, comments in header, bad magic, zero dim, unsupported maxval (16-bit), short payload, trailing bytes, missing whitespace |
| `replay_camera` | full replay, missing file → PgmFailed, empty source |
| `preprocess` | uniform 4x4→2x2, two-value split, id/ts propagation, non-divisible rejected, invalid input, wrong format, half-block rounding, identity |
| `health` | initial Booting, Booting→Ready з усіма сигналами, slow processing → Degraded одразу з Booting, Ready↔Degraded, stale frame, Failsafe sticky → Shutdown, dropped counter, route confidence clamp |
| `pipeline_harness` | end-to-end 3 frames, preprocess refusal → drop, long gap → Degraded |

**Архітектурні рішення додано:**
- D-007: integer-only math у hot paths (без FP) — bit-exact між WSL і Pi
- D-008: HealthMonitor не має власного годинника — caller передає `now_ns` (детермінізм у тестах)
- D-009: Booting → Degraded одразу при поганих сигналах на першому кадрі

**Пастки знайдені і виправлені під час білду:**
- Most vexing parse у `std::filesystem::path p(std::string(...))` → braces
- Ambiguous overload `ReplayCameraSource src({})` → named vector
- Health stays in Booting при slow processing першого кадру → виправлено

**Наступне:** S2 = M3 (VHRS v1 format + integrity) + M4 (RouteSignatureRecorder).

## 2026-06-13 — S0: Bootstrap

**Зроблено:**
- Скелет репо: core/ (include/vh, src, tests), scripts/, docs/, artifacts/ (ignored)
- Git init, remote `origin` → github.com/iigar/Visual_Homing_System_new
- `.gitattributes`: LF для .sh/.cpp/.md (критично для Pi), binary для .pgm/.vhrs
- Root CMakeLists: C++20, -Wall -Wextra -Wpedantic -Werror, 5 safety options OFF за замовчуванням, FATAL_ERROR на часткові live-конфігурації
- `vh/frame.hpp`: Frame + PixelFormat (Gray8, Thermal16 reserved), valid()
- `vh/interfaces.hpp`: 9 інтерфейсів (ICameraSource, IPreprocessor, IRouteWriter/Reader, IRouteMatcher, ITelemetrySource, INavigator, ICommandSink, IAuditLog, ISafetyGate)
- `vh_test.hpp`: мінімальний test helper, test_sanity зелений
- `scripts/build-test-desktop.sh`: explicit OFF для всіх live опцій
- Docs: PROJECT_MEMORY, ROADMAP (18 чекбоксів), DECISIONS (D-001…D-006), CLAUDE.md роутер
- NotebookLM notebook `851a3eee` створений, промпт залитий (зроблено до S0)

**Середовище перевірене:** WSL Ubuntu 24.04 / GCC 13.3 / CMake 3.28 / Ninja 1.11. Pi: OS Trixie (підтверджено користувачем).

**Незавершене:** ARCHITECTURE.md — заплановано на S1 (після перших реальних реалізацій діаграма буде точнішою).

**Наступне:** S1 = M1 (replay input) + M2 (preprocessing + health).
