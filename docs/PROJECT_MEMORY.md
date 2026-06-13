# PROJECT_MEMORY — стиснутий стан

> Перезаписується наприкінці кожної сесії. Ліміт 150 рядків. Журнал — у SESSION_LOG.md.

## Стан: S2 завершено (2026-06-13) — M3 + M4 готові

**Реалізовано:**
- `vh/route_entry.hpp` — RouteEntry (frame_id, ts, altitude_mm, heading_millirad, dims, format, payload). Unknown = sentinel значення (0xFFFFFFFF / INT32_MIN)
- `vh/endian.hpp` — explicit little-endian load/store (u16/u32/u64 + signed). Жодних залежностей від host endianness
- `vh/digest.hpp` — FNV-1a 64-bit, constexpr + streaming `Fnv1a64`. Перевірено на відомих RFC значеннях
- `vh/route_format.hpp` — VHRS v1 формат: 32-byte file header + 40-byte entry header + payload. Magic "VHRS", LE, integrity (header_digest = FNV-1a low 32 bits) + file_digest (FNV-1a 64-bit над усім файлом)
- `vh/route_io.{hpp,cpp}` — VhrsWriter (streaming append + finalize з header rewrite), VhrsReader (full-file load + 12 явних error categories). Hard caps: max 8192 px dim, 16MB payload, 1M entries
- `vh/route_inspect.{hpp,cpp}` — stateless InspectionReport + format_report() з stable key=value виходом (для checker scripts на M6/M14)
- `vh/route_recorder.{hpp,cpp}` — RouteSignatureRecorder: Frame + PoseHint → RouteEntry → VhrsWriter
- `tools/vh_route_inspect` — CLI: VHRS → key=value report
- `tools/vh_route_record` — CLI: manifest + preprocess → VHRS файл

**Тести:** 11 CTest executables (+4 нових), 100% pass у WSL. End-to-end CLI demo: 5 PGM → manifest → vh_route_record → vh_route_inspect, все коректно.

**Наступна сесія: S3 = M5 (route matching) + M6 (route validation/quality)**
- M5: Gray8RouteMatcher (normalized MAD), bounded direction error через horizontal shift, ілюмінаційна діагностика (brightness, contrast normalization як опція)
- M6: stateless self-match, perturbation tests, distinctiveness diagnostics, quality policy, `check-route-quality-log.sh`

## Архітектурні константи (не міняти без DECISIONS.md запису)

- Парадигма: route-following command-assist (yaw-rate-only). НЕ EKF position estimate
- Single-threaded deterministic pipeline
- Integer-only math у hot paths (bit-exact між desktop і Pi)
- Власний MAVLink parser (M8), digest = FNV-1a 64-bit (не криптографічний — задокументовано в DECISIONS)
- VHRS little-endian, всі multi-byte через explicit `store_le/load_le`
- Each test = окремий CTest executable
- VhrsReader = full-file load (для 150 entries × 64×48 ≈ 450KB це безпечно)

## Середовище

- Desktop: WSL Ubuntu 24.04, GCC 13.3, CMake 3.28, `./scripts/build-test-desktop.sh`
- Pi: Zero 2W, Pi OS Trixie (GCC 14), ще не задіяний
- Repo: github.com/iigar/Visual_Homing_System_new, гілка main
- Tools: `build/tools/vh_route_inspect`, `build/tools/vh_route_record`

## План сесій

S0✅ → S1✅(M1+M2) → S2✅(M3+M4) → **S3(M5+M6)** → S4(M7) → S5(M8+M9) → S6(M10) → S7(M11,Pi) → S8(M12,Pi) → S9(M13+M14) → S10(M15,Pi+FC) → S11+(M16–M18)

## Вивчені пастки (всі сесії)

- C++ most vexing parse: `Path p(std::string(x))` → braces
- Ambiguous overload `{}`: іменована змінна замість brace-init
- Health Booting→Degraded на першому "поганому" кадрі
- `std::optional` не конвертується implicit в `bool` — test helper тепер templated
- Headers `<limits>` забутий: GCC не дасть `numeric_limits<T>::min()` без явного include
- Test файли мають **явно** інклюдити кожен заголовок який використовують (transitivity не гарантується)
