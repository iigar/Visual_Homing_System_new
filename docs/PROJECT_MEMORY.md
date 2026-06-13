# PROJECT_MEMORY — стиснутий стан

> Перезаписується наприкінці кожної сесії. Ліміт 150 рядків. Журнал — у SESSION_LOG.md.

## Стан: S3 завершено (2026-06-14) — M5 + M6 готові

**Реалізовано:**
- `vh/route_match.hpp` — RouteMatch: route_index, progress (int mille + float), confidence (int mille + float), direction_shift_px, direction_error_millirad, valid
- `vh/route_matcher.{hpp,cpp}` — Gray8RouteMatcher: normalized MAD baseline (integer-only), optional window radius (memory of last best index), optional mean-normalisation (brightness robustness), confidence gate. Kernels exposed: sum_abs_diff, mean_gray8, mad_confidence_mille
- `vh/direction_error.{hpp,cpp}` — search_horizontal_shift у межах ±max_shift_px з нормалізацією по overlap. Знак: positive = live shifted RIGHT vs reference. shift_px_to_millirad через microrad_per_pixel
- `vh/route_quality.{hpp,cpp}` — три аналітики:
  - self_match: кожен entry проти всього route, перевірка exact_index + progress monotonicity
  - perturbation_check: brightness/noise/shift survival + malformed rejection
  - distinctiveness: low_texture, exact_duplicates, ambiguous_nearest, adjacent/nearest MAD aggregates, edge_trim
  - evaluate_quality: збирає три репорти проти QualityPolicy (low_texture_fraction ≤ 0.05, ambiguous ≤ 0.10, avg_nearest_mad ≥ 5, no duplicates, exact self-match, malformed rejected)
- `tools/vh_route_quality` — CLI з stable key=value виходом
- `scripts/check-route-quality-log.sh` — readiness gate: quality_pass=true + self-match exact + zero duplicates + monotonic + (опційно) entry_count

**Тести:** 13 CTest executables, 100% pass у WSL. End-to-end demos:
- Diverse route → quality_pass=true → checker pass
- Duplicate-heavy route → 4 явні failures → checker correctly fails

**Наступна сесія: S4 = M7 (BoundedNavigator)**
- RouteMatch + HealthSnapshot → NavigationCommand (yaw-rate-only, vx=vy=0)
- Gates: health Ready, camera_ok + mavlink_ok + nav_ok, valid match, min confidence, max match age
- Clamp + slew-limit yaw rate
- Тести: low confidence, stale match, degraded health, NaN/Inf, clamp/slew, reset state, zero forward speed policy

## Архітектурні константи (не міняти без DECISIONS.md запису)

- Парадигма: route-following command-assist (yaw-rate-only)
- Single-threaded deterministic pipeline
- Integer-only math у hot paths; float — лише як display projection в публічних API
- Власний MAVLink parser (M8), digest = FNV-1a 64-bit (non-crypto, документовано)
- VHRS LE, всі multi-byte через explicit `store_le/load_le`
- Each test = окремий CTest executable
- Direction shift convention: positive = live image displaced RIGHT vs reference

## Середовище

- Desktop: WSL Ubuntu 24.04, GCC 13.3, CMake 3.28, `./scripts/build-test-desktop.sh`
- Pi: Zero 2W, Pi OS Trixie (GCC 14), ще не задіяний
- Repo: github.com/iigar/Visual_Homing_System_new, гілка main
- Tools: `build/tools/{vh_route_inspect, vh_route_record, vh_route_quality}`
- NotebookLM: notebook `851a3eee` має promp + project docs + MAVLink/ArduPilot/libcamera specs. `./scripts/notebook-ask.sh "..."` для запитів.

## План сесій

S0✅ → S1✅(M1+M2) → S2✅(M3+M4) → S3✅(M5+M6) → **S4(M7)** → S5(M8+M9) → S6(M10) → S7(M11,Pi) → S8(M12,Pi) → S9(M13+M14) → S10(M15,Pi+FC) → S11+(M16–M18)

## Вивчені пастки (всі сесії)

- C++ most vexing parse: `Path p(std::string(x))` → braces
- Ambiguous overload `{}`: іменована змінна замість brace-init
- Health Booting→Degraded на першому "поганому" кадрі
- `std::optional` не конвертується implicit в `bool` — test helper тепер templated
- Headers `<limits>` забутий: GCC не дасть `numeric_limits<T>::min()` без явного include
- Test файли мають явно інклюдити кожен заголовок який використовують
- Sign convention для direction shift: важливо тримати один напрямок consistent між helper-тестом і kernel-кодом
- Малі (8×8) синтетичні patterns мають природню ambiguity — реальні 64×48 кадри мають набагато більше variance
