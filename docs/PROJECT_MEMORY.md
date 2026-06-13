# PROJECT_MEMORY — стиснутий стан

> Перезаписується наприкінці кожної сесії. Ліміт 150 рядків. Журнал — у SESSION_LOG.md.

## Стан: S4 завершено (2026-06-14) — M7 готовий

**Реалізовано:**
- `vh/navigation_command.hpp` — NavigationCommand: timestamp, vx_mps/vy_mps (hard 0.0), yaw_rate_microradps (int32, authoritative) + yaw_rate_radps (float projection), confidence_mille+float, valid. kMicroPerRad=1e6
- `vh/navigator.{hpp,cpp}` — BoundedNavigator (INavigator):
  - 6 гейтів (всі AND): health.state==Ready; camera_ok&&mavlink_ok&&navigation_ok; match.valid; finite float-полів (NaN/Inf reject); confidence_mille≥min; age = now_ns-match.ts ∈ [0, max_match_age_ns]
  - провал будь-якого → zero invalid command + reset slew-памʼяті (last_yaw_rate=0)
  - yaw-rate-only: vx=vy=0; yaw_rate_microradps = direction_error_millirad × gain_milli; clamp до ±max_yaw_rate; slew-limit Δ за крок
  - публічний reset(), last_yaw_rate_microradps() accessor
  - kernels exposed: yaw_rate_from_error (int64, overflow-safe), clamp_symmetric, slew_limit
- NavigatorConfig defaults: min_conf 600, max_age 300ms, gain_milli 500, max_yaw 500000 µrad/s (~0.5 rad/s), max_slew 100000 µrad/s

**Тести:** 14 CTest executables, 100% pass у WSL. test_navigator = 11 cases (kernels, happy, zero-fwd policy, low conf, stale+future+edge, invalid, degraded ×5, non-finite ×3, clamp, slew ramp, reset-after-invalid).

**Наступна сесія: S5 = M8 (read-only MAVLink telemetry) + M9 (dry-run MAVLink boundary)**
- M8: власний MAVLink v1/v2 parser (framing, CRC, heartbeat/armed/mode/attitude/altitude), untrusted input robustness, freshness→mavlink_ok, Pi serial inspector scripts. НЕ слати команди.
- M9: DryRunCommandSink + dry-run bridge (scripted heartbeat + telemetry), stale-telemetry blocking, compact log
- ⚠ M8 = момент запитувати NotebookLM про MAVLink specs (CRC, message layouts) замість читання у власний контекст

## Архітектурні константи (не міняти без DECISIONS.md запису)

- Парадигма: route-following command-assist (yaw-rate-only)
- Single-threaded deterministic pipeline
- Integer-only math у hot paths; float — лише як display projection в публічних API (D-007, D-014, D-017)
- Yaw rate authoritative = microrad/s; gain_milli дає точний integer multiply (D-017)
- Navigator fail-closed: будь-який провал гейта → zero invalid + reset slew (D-018)
- Власний MAVLink parser (M8), digest = FNV-1a 64-bit (non-crypto, документовано)
- VHRS LE, всі multi-byte через explicit `store_le/load_le`
- Each test = окремий CTest executable
- Direction shift convention: positive = live image displaced RIGHT vs reference (D-015)
- vx_mps/vy_mps структурно присутні, але hard 0.0 до live-output ревью

## Середовище

- Desktop: WSL Ubuntu 24.04, GCC 13.3, CMake 3.28, `./scripts/build-test-desktop.sh`
- Pi: Zero 2W, Pi OS Trixie (GCC 14), ще не задіяний
- Repo: github.com/iigar/Visual_Homing_System_new, гілка main
- Tools: `build/tools/{vh_route_inspect, vh_route_record, vh_route_quality}` (M7 без CLI — навигаційний вивід у M9)
- NotebookLM: notebook `851a3eee` має промпт + project docs + MAVLink/ArduPilot/libcamera specs. `./scripts/notebook-ask.sh "..."` для запитів.

## План сесій

S0✅ → S1✅(M1+M2) → S2✅(M3+M4) → S3✅(M5+M6) → S4✅(M7) → **S5(M8+M9)** → S6(M10) → S7(M11,Pi) → S8(M12,Pi) → S9(M13+M14) → S10(M15,Pi+FC) → S11+(M16–M18)

## Вивчені пастки (всі сесії)

- C++ most vexing parse: `Path p(std::string(x))` → braces
- Ambiguous overload `{}`: іменована змінна замість brace-init
- Health Booting→Degraded на першому "поганому" кадрі
- `std::optional` не конвертується implicit в `bool` — test helper тепер templated
- Headers `<limits>`/`<cmath>` забутий: GCC не дасть `numeric_limits<T>::min()`/`isfinite` без явного include
- Test файли мають явно інклюдити кожен заголовок який використовують
- Sign convention для direction shift: тримати один напрямок consistent між helper-тестом і kernel-кодом
- Малі (8×8) синтетичні patterns мають природню ambiguity — реальні 64×48 кадри мають набагато більше variance
- WSL/NTFS "Clock skew detected" warning при білді — нешкідливе (timestamp розбіжність mount), білд завершується
- Integer bounding: int64 проміжне для yaw_rate_from_error/slew, щоб int32 millirad×gain не переповнював
