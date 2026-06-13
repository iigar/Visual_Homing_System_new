# DECISIONS — архітектурні рішення і їх причини

> Append-only. Кожен запис: дата, рішення, чому, альтернативи.

## 2026-06-13 — S0 bootstrap

### D-001: Desktop-білди через WSL Ubuntu 24.04
**Рішення:** всі desktop build/test у WSL (`/mnt/d/...`), не нативний Windows toolchain.
**Чому:** на Windows-машині немає компілятора взагалі; WSL GCC 13 ≈ Pi Trixie GCC 14; bash-скрипти ідентичні на desktop і Pi; нуль CRLF-проблем виконання.
**Альтернативи:** MSVC (інший компілятор ніж Pi, зайва матриця), MinGW (нестандартний libstdc++).

### D-002: Pi OS Trixie (Debian 13)
**Рішення:** ціль — Pi OS Trixie, GCC 14, новий libcamera стек.
**Чому:** користувач підтвердив версію OS на Pi Zero 2W.
**Наслідок:** перед M11 перевірити актуальний libcamera API саме для Trixie (відрізняється від Bullseye/Bookworm).

### D-003: Власний мінімальний test helper замість GoogleTest
**Рішення:** `core/tests/vh_test.hpp` (~40 рядків), кожен тест = окремий CTest executable.
**Чому:** GoogleTest — важка залежність для нативних білдів на Pi Zero 2W (512MB RAM); промпт вимагає мінімум залежностей.
**Альтернативи:** doctest (single-header, прийнятний fallback якщо helper стане тісним).

### D-004: CMake safety options з FATAL_ERROR на часткові конфігурації
**Рішення:** `VH_ENABLE_WRITER_ATTACH` вимагає `VH_ENABLE_LIVE_OUTPUT` + `VH_ENABLE_BENCH_PROPS_OFF`; live без bench-props-off — відмова конфігурації.
**Чому:** промпт: "stale CMake cache must not silently enable live output"; часткова конфігурація = помилка оператора → fail при configure, не при runtime.

### D-005: interfaces.hpp повністю у S0
**Рішення:** всі 9 інтерфейсів стадій pipeline оголошені до першої реалізації.
**Чому:** фіксує архітектуру до розповзання коду; майбутні датчики (thermal, rangefinder, VIO) підключаються за цими контрактами; кожна стадія мокається в тестах незалежно.

### D-010: FNV-1a 64-bit для VHRS integrity diagnostics (S2)
**Рішення:** header digest = FNV-1a low 32 bits над першими 12 байтами header'а; file digest = FNV-1a 64-bit над усім файлом, репортується у inspection report (не зберігається в файлі — це зовнішня діагностика).
**Чому:** не криптографічний, але дає достатню роздільну здатність для виявлення випадкового пошкодження і простого локального tampering (1 byte flip → інший digest з ймовірністю ≈1). Промпт явно дозволяє це: "integrity diagnostics protect against accidental or local tampering but are not a complete cryptographic trust model unless signed metadata is later added".
**Альтернатива:** SHA-256 (200 рядків, важче, повільніше; немає захисту без підпису ключем). Залишено на майбутнє hardening разом з підписом.

### D-011: VHRS v1 layout (S2)
**Рішення:** 32-byte file header (magic+version+flags+entry_count+header_digest+16 reserved zero) + per-entry 40-byte header (frame_id u64, timestamp_ns i64, altitude_mm u32, heading_millirad i32, width u32, height u32, pixel_format u16, reserved u16, payload_length u32) + payload.
**Чому:** все integer-only (D-007), LE, фіксовані розміри для passable forwards-compat (reserved field), payload_length жорстко перевіряється через width×height×bytes_per_pixel — клієнт не може записати inconsistent entry.
**Hard caps:** 8192 px dim, 16MB payload, 1M entries — refuse до allocation.

### D-012: Integer altitude/heading метадані (S2)
**Рішення:** altitude — u32 mm (0xFFFFFFFF = unknown), heading — i32 milliradians (INT32_MIN = unknown).
**Чому:** integer-only (D-007), достатня точність для coarse route metadata (1mm altitude, ~0.057° heading), bit-exact між platforms, sentinel-friendly.
**Альтернатива:** f32 — потенційні denormal/NaN issues, не bit-exact між x86 і ARM з різними FP settings.

### D-013: Test helper — template instead of bool param (S2)
**Рішення:** `expect<T>(const T& value, ...)` замість `expect(bool, ...)` — використовує `static_cast<bool>()` всередині.
**Чому:** дозволяє `std::optional`, smart pointers, custom RAII guards без `.has_value()`/`.get()` boilerplate. Запобігає implicit narrowing помилкам коли тип має explicit operator bool.

### D-007: Integer-only math у hot paths (S1)
**Рішення:** block-average resize, MAD matcher (M5), digest math — без floating-point. Тільки `uint64_t` суми, integer division з half-block rounding.
**Чому:** GCC 13 на x86 і GCC 14 на ARM Cortex-A53 можуть мати різну FP rounding для denormals; integer arithmetic — bit-exact. Critical для reproducibility 3/3 evidence logs.
**Альтернатива:** -ffp-contract=off + strict-math — фрагілно, легко зламати випадково.

### D-008: HealthMonitor без власного годинника (S1)
**Рішення:** caller передає `now_ns` у `update()` і `on_frame_seen()/dropped()`. Сам монітор не викликає clock.
**Чому:** детерміновані тести без `std::this_thread::sleep_for`; пайплайн вже має один авторитетний час (timestamp кадру) — додавати другий породжує race; на Pi є один реальний CLOCK_MONOTONIC, на desktop тести — fake.

### D-009: Booting → Degraded на першому кадрі з поганими сигналами (S1)
**Рішення:** якщо frames_seen стає >0 і будь-який сигнал bad (subsystem off, stale, slow) — одразу Degraded, не залишатись у Booting.
**Чому:** Booting означає "ще не отримали даних"; як тільки потік пішов, ми вже не "завантажуємось". Інакше gate тримає Booting нескінченно при stale camera і фейлсейф не спрацьовує.

### D-006: Власний MAVLink parser (заплановано на M8)
**Рішення:** не вендорити повні mavlink C headers; парсити тільки потрібні ~6 повідомлень (HEARTBEAT, ATTITUDE, GLOBAL_POSITION_INT, VFR_HUD, SYS_STATUS, +SET_POSITION_TARGET_LOCAL_NED encode на M17).
**Чому:** повний mavlink = 100k+ рядків generated коду; нам потрібен маленький, тестований, auditable parser з явною обробкою malformed input (untrusted serial).
