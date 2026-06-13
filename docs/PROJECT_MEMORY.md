# PROJECT_MEMORY — стиснутий стан

> Перезаписується наприкінці кожної сесії. Ліміт 150 рядків. Журнал — у SESSION_LOG.md.

## Стан: S1 завершено (2026-06-13) — M1 + M2 готові

**Реалізовано:**
- `vh/manifest.{hpp,cpp}` — CSV manifest parser (id,timestamp_ns,path); comments, header, CRLF, strict monotonic timestamps, duplicate id detection
- `vh/pgm.{hpp,cpp}` — PGM P5 Gray8 reader (rejects P2/16-bit/short/trailing); header comments підтримані
- `vh/replay_camera.{hpp,cpp}` — ReplayCameraSource (ICameraSource impl): manifest + PGM → frame stream
- `vh/preprocess.{hpp,cpp}` — BlockAveragePreprocessor: integer-only block-average resize, fail-closed на non-divisible
- `vh/health.{hpp,cpp}` — HealthMonitor + HealthSnapshot + 5 states (Booting/Ready/Degraded/Failsafe/Shutdown). Failsafe/Shutdown — sticky. Deterministic: caller передає `now_ns`
- Інтеграційний harness тест: replay→preprocess→health

**Тести:** 7 CTest executables, 100% pass у WSL (Ubuntu 24.04, GCC 13.3). Самописний test helper, без GoogleTest.

**Наступна сесія: S2 = M3 (VHRS v1 route artifact) + M4 (route recording)**
- M3: бінарний формат VHRS (magic, version, LE, entry metadata, payload), integrity diagnostics (digest), inspection CLI, round-trip tests
- M4: RouteSignatureRecorder з replay → VHRS файл

## Архітектурні константи (не міняти без DECISIONS.md запису)

- Парадигма: route-following command-assist (yaw-rate-only). НЕ EKF position estimate
- Single-threaded deterministic pipeline
- Без OpenCV / heavy deps. Власний MAVLink parser (M8), власний SHA-256 при потребі
- VHRS little-endian, explicit `load_le/store_le` helpers (будуть у M3)
- Integer-only math у hot paths (без FP) — для bit-exact між desktop і Pi
- Each test = окремий CTest executable

## Середовище

- Desktop: WSL Ubuntu 24.04, GCC 13.3, CMake 3.28, `./scripts/build-test-desktop.sh`
- Pi: Zero 2W, Pi OS Trixie (GCC 14), ще не задіяний
- Repo: github.com/iigar/Visual_Homing_System_new, гілка main

## План сесій

S0✅ → S1✅(M1+M2) → **S2(M3+M4)** → S3(M5+M6) → S4(M7) → S5(M8+M9) → S6(M10) → S7(M11,Pi) → S8(M12,Pi) → S9(M13+M14) → S10(M15,Pi+FC) → S11+(M16–M18)

## Вивчені пастки (фікси по ходу S1)

- **C++ most vexing parse:** `Path p(std::string(x))` → парситься як декларація функції. Використовуй braces: `Path p{std::string(x)}`
- **ambiguous overload `{}`:** `ReplayCameraSource src({})` — компілятор не може вибрати між vector ctor і copy-ctor. Передавати іменований `std::vector<>` змінною
- **Health Booting→Degraded:** перший побачений кадр з поганими сигналами має одразу йти в Degraded, не "стирчати" в Booting
