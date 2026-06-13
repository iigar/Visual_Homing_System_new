# SESSION_LOG — журнал сесій

> Append-only. Новий запис зверху.

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
