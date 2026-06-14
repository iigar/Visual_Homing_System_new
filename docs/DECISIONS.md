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

### D-014: RouteMatch має integer і float representations (S3)
**Рішення:** progress і confidence зберігаються і в integer mille (0..1000) і у float (0.0..1.0). Всі gates у matcher і навигаторі — на integer mille; float — лише display.
**Чому:** D-007 (integer-only у hot paths) для bit-exact behaviour, але downstream (UI/web/Python tooling) очікують float. Дублювання — дешеве, читачі обирають свою репрезентацію.

### D-015: Direction shift sign convention (S3)
**Рішення:** `RouteMatch.direction_shift_px > 0` означає що live frame має контент який знаходиться ПРАВІШЕ у візуальному полі ніж у reference. Це той самий знак що параметр `s` у helper-функції `shift_horizontal(in, +s)`.
**Чому:** один з двох тестів спочатку видавав протилежний знак — переплутана геометрія сигналу. Зафіксовано через explicit negation у `search_horizontal_shift` + детальний коментар у header.
**How to apply:** при додаванні нових matcher backends (M5 fallback descriptors) — переконатись що вони використовують ту саму convention.

### D-016: Quality policy thresholds per-deployment (S3)
**Рішення:** дефолти QualityPolicy (low_texture_fraction ≤ 0.05, ambiguous_nearest ≤ 0.10, avg_nearest_mad ≥ 5, no duplicates) розраховані на 64×48 real-world frames. Синтетичні 8×8 тести можуть мати relaxed thresholds — це OK, бо це тести алгоритму, не виробничого порогу.

## 2026-06-14 — S4 (M7)

### D-017: NavigationCommand yaw-rate integer-authoritative + bounding на microrad/s (S4)
**Рішення:** `NavigationCommand.yaw_rate_microradps` (int32) — джерело істини; `yaw_rate_radps` (float) = microradps/1e6 як display projection. Уся bounding-математика (gain, clamp, slew) — на integer microrad/s. Gain виражений як `gain_milli` (gain×1000), тому `yaw_rate_microradps = direction_error_millirad × gain_milli` — точний integer multiply (фактори 1e-3·1e6·1e3 скорочуються).
**Чому:** D-007 (integer-only hot paths) для bit-exact між desktop і Pi; D-014 патерн (integer + float dual). `vx_mps`/`vy_mps` структурно присутні, але hard-wired 0.0 — yaw-rate-only scope до ревью.
**How to apply:** M9 (DryRunCommandSink) і M17 (MAVLink encoder) читають `yaw_rate_microradps` для bit-exact логування; float — лише для людино-читабельного виводу.

### D-018: BoundedNavigator скидає slew-памʼять на будь-якому провалі гейта (S4)
**Рішення:** будь-який провал з 6 гейтів (health Ready, stage flags, valid match, finite floats, min confidence, match age ∈ [0, max]) → zero invalid command + `last_yaw_rate=0`. Відновлення завжди стартує з нуля, не відновлює застарілу швидкість.
**Чому:** fail-closed posture з промпту ("invalid input → zero command"); відновлення з накопиченого rate після провалу health/match — небезпечно. Future-stamped match (negative age) теж відхиляється як nonsensical.
**How to apply:** будь-який майбутній navigator backend має дотримуватись цього reset-on-fail контракту; тести покривають reset-after-invalid явно.

## 2026-06-14 — S5 (M8)

### D-019: Власний read-only MAVLink parser, CRC-валідація обовʼязкова, untrusted input (S5)
**Рішення:** byte-streaming state machine для v1(0xFE)/v2(0xFD). Кадр НІКОЛИ не довіряється без перевірки CRC-16/MCRF4XX з per-message CRC_EXTRA. Unknown msgid (немає CRC_EXTRA) → не емітиться, рахується окремо (`frames_unknown_msgid`), payload споживається структурно щоб не розсинхронити стрім. v2 signed: 13-байтовий підпис споживається але НЕ перевіряється (read-only telemetry). Опційний `expected_sysid` фільтр відкидає чужий трафік. Парсер НЕ продукує команд.
**Чому:** промпт — UART telemetry = untrusted input; malformed/injected/wrong-sysid/stale не мають створювати command permission. CRC+CRC_EXTRA — єдиний надійний gate проти сміття.
**How to apply:** M9 dry-run bridge і M13 SafetyGate читають `TelemetrySnapshot.mavlink_ok`; ніколи не давати permission на основі некрос-перевіреного кадру.

### D-020: MAVLink CRC_EXTRA/offsets крос-валідовано Python-реалізацією (notebook був down) (S5)
**Рішення:** CRC_EXTRA HEARTBEAT=50, ATTITUDE=39, GLOBAL_POSITION_INT=104; offsets per documented dialect. NotebookLM auth протухав під час M8, тож замість notebook-звірки зроблено незалежну крос-перевірку: окрема Python-реалізація CRC будує кадри, C++ парсер їх приймає (frames_crc_error=0) і декодує правильні значення (heartbeat LOITER/armed, attitude roll 0.5, position lat/lon/rel_alt).
**Чому:** дві незалежні імплементації того самого документованого алгоритму, що збігаються — сильний доказ коректності framing+offsets+CRC_EXTRA без notebook.
**How to apply:** ПЕРЕД hardware bring-up (M11/M15) все одно звірити проти pymavlink на реальному ArduPilot-потоці; крос-перевірка Python не ловить помилку якщо обидві імплементації мають однаковий хибний CRC_EXTRA (хоча значення — стандартні).
**Update (пізніше тієї ж сесії):** після re-login NotebookLM підтвердив усі константи authoritative — CRC_EXTRA 50/39/104 + offsets (HEARTBEAT custom_mode@0/type@4/autopilot@5/base_mode@6; GLOBAL_POSITION_INT lat@4/lon@8/alt@12/relative_alt@16/hdg@26). Тепер дві незалежні перевірки (Python + notebook) збігаються з реалізацією. Hardware-звірка з pymavlink лишається доброю практикою але ризик хибних констант знято.

### D-021: telemetry freshness → mavlink_ok через clock injection (S5)
**Рішення:** `MavlinkTelemetry.update(now_ns)` рахує age кожного повідомлення; `mavlink_ok = heartbeat_seen && age ∈ [0, max_heartbeat_age_ns]` (дефолт 2с). Negative age (future timestamp) → not ok (fail-closed). Той самий clock-injection патерн що HealthMonitor (D-008). HealthMonitor.set_mavlink_ok споживає цей bool на рівні orchestrator.
**Чому:** stale/missing heartbeat має одразу ронити link health; детермінований годинник для відтворюваних тестів.
**Чому:** малі patterns мають велику ambiguity природньо. Жорсткі production thresholds зробили б тести brittle на synthetic data, не давши користі.
**How to apply:** перед M11/M12 (live capture на Pi) перевірити що default thresholds passуються на реальних 64×48 IMX219 кадрах; якщо ні — або thresholds слабші, або матчер потребує fallback descriptor (M5 future work).

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
