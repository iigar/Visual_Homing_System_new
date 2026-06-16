# PROJECT_MEMORY — стиснутий стан

> Перезаписується наприкінці кожної сесії. Ліміт 150 рядків. Журнал — у SESSION_LOG.md.

## Стан: M11 завершено (2026-06-16) — Pi hardware capture (libcamera Trixie)

**Реалізовано:**
- `vh/pi_camera.{hpp,cpp}` — `PiCameraSource : ICameraSource` (pimpl ховає libcamera з public API)
  - PiCameraConfig (capture w/h, buffer_count, frame_timeout_ms, camera_index), PiCameraError (11), validate_pi_camera_config (dims∈(0,8192], buffers≥1, timeout>0) — передує compile-гейту
  - Двійний гейт: (1) compile `VH_ENABLE_LIBCAMERA` (PUBLIC define — умовний pimpl member) — desktop fail-closed stub, `open()`→NotCompiledIn; (2) runtime `open()` acquire+configure R8@capture dims інакше closed, `next_frame()`→nullopt
  - Pi backend (`#if VH_ENABLE_LIBCAMERA`): CameraManager→acquire→R8 stream→FrameBufferAllocator→Request; async `requestCompleted` → sync `next_frame()` через mutex+cv+черга; mmap-per-fd + row-copy зі `StreamConfiguration::stride`; validate() adjusted ≠ запит → ConfigureFailed
- CMake: ON → pkg-config libcamera link; OFF → define=0, нуль залежності
- `scripts/build-test-pi.sh` (build-pi/, libcamera ON, live-output OFF), `scripts/pi-camera-probe.sh` (READ-ONLY camera list)

**Тести:** 20 CTest desktop, 100% pass. test_pi_camera (config validation, fail-closed, NotStarted, NotCompiledIn).

**⚠ Хвіст:** Pi-секція НЕ компілювалась (немає Pi) — обовʼязково `build-test-pi.sh` на реальному Pi перед M12. libcamera 0.3+ Trixie API підтверджено NotebookLM (formats::R8, plane.fd().get(), stride).

**Наступна сесія: M12 (live route matching dry-run)**
- Speed mismatch validation, endpoint action, compact log
- Бере кадри з PiCameraSource (capture dims з активного CameraProfile M10) → preprocess → matcher

## Архітектурні константи (не міняти без DECISIONS.md запису)

- Парадигма: route-following command-assist (yaw-rate-only)
- Single-threaded deterministic pipeline; clock injection скрізь (D-008, D-021)
- Integer-only math у hot paths; float — display projection (D-007, D-014, D-017)
- Navigator fail-closed: провал гейта → zero invalid + reset slew (D-018)
- MAVLink: власний parser, untrusted input, обовʼязкова CRC-валідація, parser НЕ продукує команд (D-019). CRC_EXTRA 50/39/104 + offsets підтверджено Python+NotebookLM (D-020)
- mavlink_ok = свіжий heartbeat; stale/future → fail-closed (D-021)
- Command boundary fail-closed: DryRunCommandSink stopped-by-default, single-writer (D-022). Bridge накладає telemetry freshness на validity (D-023)
- Camera profile FOV → rad-per-pixel для matcher; ground footprint/scale mismatch = DIAGNOSTIC ONLY, не впливають на live команди (D-024)
- Pi capture: двійний гейт (compile VH_ENABLE_LIBCAMERA + runtime open), pimpl, fail-closed; libcamera async→sync ізольовано в backend; VH_ENABLE_LIBCAMERA ≠ live output (D-025)
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
- Pi capture pimpl: libcamera headers ТІЛЬКИ в .cpp під `#if`; compile-define PUBLIC (інакше pimpl member layout розходиться між TU); validate config ПЕРЕД compile-гейтом (детермінізм desktop/Pi); код під `#if VH_ENABLE_LIBCAMERA` desktop НЕ компілює → перевіряти на Pi окремо
