# PROJECT_MEMORY — стиснутий стан

> Перезаписується наприкінці кожної сесії. Ліміт 150 рядків. Журнал — у SESSION_LOG.md.

## Стан: S0 bootstrap (2026-06-13)

**Завершено:** скелет репо, CMake (C++20, safety options OFF за замовчуванням з FATAL_ERROR на часткові конфігурації), interfaces.hpp (9 інтерфейсів усіх стадій pipeline), frame.hpp, мінімальний тест-helper (vh_test.hpp, без GoogleTest), test_sanity зелений, docs скелет.

**Наступна сесія: S1 = M1 (replay input) + M2 (preprocessing + health)**
- M1: CSV manifest парсер (`id,timestamp_ns,path`), PGM P5 reader, тести: monotonic timestamps, malformed input
- M2: block-average Gray8 resize, HealthSnapshot + states (Booting/Ready/Degraded/Failsafe/Shutdown), pipeline harness

## Архітектурні константи (не міняти без DECISIONS.md запису)

- Парадигма: route-following command-assist (yaw-rate-only). НЕ EKF position estimate
- Single-threaded deterministic pipeline (поки виміри не доведуть потребу в threading)
- Без OpenCV / важких залежностей у core
- Власний MAVLink parser (~6 повідомлень), власний test helper, власний SHA-256 при потребі
- VHRS little-endian, explicit load_le/store_le helpers
- Кожен milestone = тести + коміт + docs update

## Середовище

- Desktop build: WSL Ubuntu 24.04, GCC 13.3, CMake 3.28, `./scripts/build-test-desktop.sh`
- Pi: Zero 2W, Pi OS **Trixie** (GCC 14), ще не задіяний (до S7/M11)
- Repo: `D:\Agents_ClaudeCode\Visual_Homing_System_new` → github.com/iigar/Visual_Homing_System_new

## План сесій

S0✅ → S1(M1+M2) → S2(M3+M4) → S3(M5+M6) → S4(M7) → S5(M8+M9) → S6(M10) → S7(M11,Pi) → S8(M12,Pi) → S9(M13+M14) → S10(M15,Pi+FC) → S11+(M16-M18, після ревью)
