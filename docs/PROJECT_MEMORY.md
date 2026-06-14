# PROJECT_MEMORY — стиснутий стан

> Перезаписується наприкінці кожної сесії. Ліміт 150 рядків. Журнал — у SESSION_LOG.md.

## Стан: M9 завершено (2026-06-15) — dry-run MAVLink boundary

**Реалізовано:**
- `vh/command_sink.{hpp,cpp}` — DryRunCommandSink (ICommandSink): НІКОЛИ не передає, лише bounded ring history (дефолт 64) + counters. Stopped-by-default (send відхилено до start). Single-writer (start вдруге → false). all-time seq зберігається.
- `vh/dry_run_bridge.{hpp,cpp}` — DryRunBridge: MavlinkTelemetry + BoundedNavigator + sink. `tick(match, base_health, now)` накладає telemetry freshness/compat: `eff.mavlink_ok = ts.mavlink_ok && !incompatible` → stale heartbeat / disarmed(require_armed) → navigator відмовляє valid command. Counters: ticks/blocked_stale/blocked_incompatible/commands_valid/invalid. telemetry() expose armed/mode/attitude/rel-alt.

**Тести:** 18 CTest, 100% pass. test_command_sink (5), test_dry_run_bridge (6). Live MAVLink output недоступний, fail-closed.

**Наступна сесія: M10 (camera profiles)**
- Camera profile model + file format: id, sensor type (visible/thermal/other), capture+target w/h, pixel format, horizontal/vertical_fov_rad, matcher thresholds, route-quality thresholds, normalization hints
- Profile validation, list/get/set active, JSON output для UI/API
- FOV → rad-per-pixel для matcher direction error (зараз microrad_per_pixel передається вручну в MatcherConfig/DirectionConfig — M10 дає реальне джерело)
- FOV/altitude → ground footprint helpers (ground w/h + meters-per-pixel), IMX219 профіль

## Архітектурні константи (не міняти без DECISIONS.md запису)

- Парадигма: route-following command-assist (yaw-rate-only)
- Single-threaded deterministic pipeline; clock injection скрізь (D-008, D-021)
- Integer-only math у hot paths; float — display projection (D-007, D-014, D-017)
- Navigator fail-closed: провал гейта → zero invalid + reset slew (D-018)
- MAVLink: власний parser, untrusted input, обовʼязкова CRC-валідація, parser НЕ продукує команд (D-019). CRC_EXTRA 50/39/104 + offsets підтверджено Python+NotebookLM (D-020)
- mavlink_ok = свіжий heartbeat; stale/future → fail-closed (D-021)
- Command boundary fail-closed: DryRunCommandSink stopped-by-default, single-writer, нічого не передає (D-022). Bridge накладає telemetry freshness на validity (D-023)
- VHRS LE через explicit store_le/load_le; digest = FNV-1a 64-bit (non-crypto)
- Each test = окремий CTest executable
- Direction shift: positive = live displaced RIGHT vs reference (D-015)
- vx_mps/vy_mps структурно є, але hard 0.0 до live-output ревью

## Середовище

- Desktop: WSL Ubuntu 24.04, GCC 13.3, CMake 3.28, `./scripts/build-test-desktop.sh`
- Pi: Zero 2W, Pi OS Trixie (GCC 14), ще не задіяний
- Repo: github.com/iigar/Visual_Homing_System_new, гілка main
- Tools: `build/tools/{vh_route_inspect, vh_route_record, vh_route_quality, vh_mavlink_inspect}`
- NotebookLM: notebook `851a3eee`. `./scripts/notebook-ask.sh "..."` через Git Bash (НЕ WSL). Google-сесія протухає ~10хв → якщо падає auth, дати юзеру `! python -m notebooklm login` (він оновлює сам). login потребує Playwright chromium. notebook-add-doc.sh idempotent (dedup by ID).

## План сесій

S0✅ → M1-M9✅ → **M10** → M11(Pi) → M12(Pi) → M13+M14 → M15(Pi+FC) → M16-M18(live-output, після ревью)

## Вивчені пастки (всі сесії)

- C++ most vexing parse → braces; ambiguous `{}` overload → named var
- Health Booting→Degraded на першому "поганому" кадрі
- `std::optional` не → bool implicit (test helper templated)
- Забутий `<limits>`/`<cmath>`/`<bit>`/`<string>`/`<cstring>` include — кожен test/tool явно інклюдить що вживає
- Sign convention direction shift — consistent між helper і kernel
- WSL/NTFS "Clock skew"/"modification time in the future" warning — нешкідливе
- Integer bounding: int64 проміжне щоб int32 не переповнював
- MAVLink deferred emit: signed-frame треба буферити (pending_), бо `out` не персистить між parse_byte
- MAVLink v2 truncation: payload буфер zero-fill, декодер читає фіксовані offsets безпечно
- Ring buffer history: oldest = (head + cap - size) % cap; seq лічильник окремо від retention
