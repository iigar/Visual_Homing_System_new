# PROJECT_MEMORY — стиснутий стан

> Перезаписується наприкінці кожної сесії. Ліміт 150 рядків. Журнал — у SESSION_LOG.md.

## Стан: M10 завершено (2026-06-15) — camera profiles

**Реалізовано:**
- `vh/camera_profile.{hpp,cpp}` — CameraProfile: id, sensor (visible/thermal/other), capture+target w/h, pixel_format, h/v FOV, matcher thresholds (min_confidence_mille, window_radius), quality thresholds (low_texture/ambiguous/avg_nearest_mad), mean_normalise hint
  - validate_profile (id non-empty, dims>0, target≤capture, FOV∈(0,π) finite)
  - key=value parse/format (без JSON-залежності) + profile_to_json (для UI/API)
  - rad_per_pixel_h/v (capture/target), matcher_microrad_per_pixel = horizontal_fov/target_width·1e6
  - compute_ground_footprint = 2·h·tan(fov/2) + meters_per_pixel; reject non-finite/non-pos altitude
  - visual_scale_mismatch (route vs current altitude) — DIAGNOSTIC ONLY
  - to_matcher_config / to_quality_policy (профіль живить M5/M6)
  - imx219_profile built-in (nominal FOV — виміряти для реальної лінзи)
  - ProfileRegistry: add/list/get/set_active/active
- `tools/vh_camera_profile` — validate/json/footprint CLI
- `docs/CAMERA_PROFILES.md` — resolution/altitude relationship + diagnostic-first safety

**Тести:** 19 CTest, 100% pass. test_camera_profile (11 cases). E2E: IMX219 @30m → ground 40.7×27.2m, 0.64 m/px target.

**Наступна сесія: M11 (Pi hardware capture) — ПЕРШИЙ на Pi**
- Pi camera backend (libcamera) за compile-time + runtime gates
- Desktop builds fail-closed без live capture
- Pi build strategy (native CTest повільний на Zero 2W; стабільні incremental build dirs, ignored)
- ⚠ Trixie libcamera API відрізняється від Bullseye/Bookworm — запитати NotebookLM (libcamera C++ API вже в notebook)
- Capture у capture dims → preprocess → target dims; matcher бере microrad/px з camera profile (M10)

## Архітектурні константи (не міняти без DECISIONS.md запису)

- Парадигма: route-following command-assist (yaw-rate-only)
- Single-threaded deterministic pipeline; clock injection скрізь (D-008, D-021)
- Integer-only math у hot paths; float — display projection (D-007, D-014, D-017)
- Navigator fail-closed: провал гейта → zero invalid + reset slew (D-018)
- MAVLink: власний parser, untrusted input, обовʼязкова CRC-валідація, parser НЕ продукує команд (D-019). CRC_EXTRA 50/39/104 + offsets підтверджено Python+NotebookLM (D-020)
- mavlink_ok = свіжий heartbeat; stale/future → fail-closed (D-021)
- Command boundary fail-closed: DryRunCommandSink stopped-by-default, single-writer (D-022). Bridge накладає telemetry freshness на validity (D-023)
- Camera profile FOV → rad-per-pixel для matcher; ground footprint/scale mismatch = DIAGNOSTIC ONLY, не впливають на live команди (D-024)
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
