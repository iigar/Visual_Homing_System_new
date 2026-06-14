# PROJECT_MEMORY — стиснутий стан

> Перезаписується наприкінці кожної сесії. Ліміт 150 рядків. Журнал — у SESSION_LOG.md.

## Стан: S5/M8 завершено (2026-06-14) — read-only MAVLink telemetry

**Реалізовано (3 шари):**
- `vh/mavlink.{hpp,cpp}` — byte-streaming parser v1(0xFE)/v2(0xFD). CRC-16/MCRF4XX (`mavlink_crc_accumulate`) + per-msg CRC_EXTRA (HEARTBEAT 50, ATTITUDE 39, GLOBAL_POSITION_INT 104). State machine; counters (bytes_seen, frames_ok, frames_crc_error, frames_unknown_msgid, bytes_discarded). Unknown msgid → не емітиться (немає CRC_EXTRA). v2 signed → 13B підпис споживається не перевіряючись; deferred emit через `pending_` буфер. v2 empty-byte truncation → payload zero-extended.
- `vh/telemetry.{hpp,cpp}` — декодери HEARTBEAT/ATTITUDE/GLOBAL_POSITION_INT, TelemetrySnapshot, copter_mode_label, armed = base_mode & 0x80. MavlinkTelemetry (ITelemetrySource): ingest(bytes, now) → apply → update. expected_sysid фільтр відкидає чужий трафік. freshness→mavlink_ok через update(now): heartbeat_seen && age ∈ [0, max_heartbeat_age_ns=2с].
- `tools/vh_mavlink_inspect` (stdin→key=value) + `scripts/mavlink-capture.sh` (/dev/serial0 @115200 READ-ONLY) + `scripts/mavlink-inspect.sh`.

**Тести:** 16 CTest, 100% pass. test_mavlink (10 cases), test_telemetry (9 cases). Крос-валідовано незалежною Python-CRC реалізацією (notebook був down) — CRC_EXTRA+offsets підтверджено для всіх 3 повідомлень, end-to-end demo працює.

**⚠ Перед hardware (M11/M15):** звірити CRC_EXTRA/offsets проти pymavlink на реальному ArduPilot-потоці (D-020).

**Наступна сесія: M9 (dry-run MAVLink boundary)**
- DryRunCommandSink (ICommandSink) — лог команд, нічого не шле
- dry-run MAVLink bridge: scripted heartbeat + telemetry snapshots
- stale-telemetry blocking (TelemetrySnapshot.mavlink_ok=false → no command)
- compact log
- Жодного реального MAVLink output

## Архітектурні константи (не міняти без DECISIONS.md запису)

- Парадигма: route-following command-assist (yaw-rate-only)
- Single-threaded deterministic pipeline; clock injection скрізь (D-008, D-021)
- Integer-only math у hot paths; float — display projection (D-007, D-014, D-017)
- Navigator fail-closed: провал гейта → zero invalid + reset slew (D-018)
- MAVLink: власний parser, untrusted input, обовʼязкова CRC-валідація, parser НЕ продукує команд (D-019)
- mavlink_ok = свіжий heartbeat; stale/future → fail-closed (D-021)
- VHRS LE через explicit store_le/load_le; digest = FNV-1a 64-bit (non-crypto)
- Each test = окремий CTest executable
- Direction shift: positive = live displaced RIGHT vs reference (D-015)
- vx_mps/vy_mps структурно є, але hard 0.0 до live-output ревью

## Середовище

- Desktop: WSL Ubuntu 24.04, GCC 13.3, CMake 3.28, `./scripts/build-test-desktop.sh`
- Pi: Zero 2W, Pi OS Trixie (GCC 14), ще не задіяний
- Repo: github.com/iigar/Visual_Homing_System_new, гілка main
- Tools: `build/tools/{vh_route_inspect, vh_route_record, vh_route_quality, vh_mavlink_inspect}`
- NotebookLM: notebook `851a3eee`. `./scripts/notebook-ask.sh "..."` через Git Bash (НЕ WSL). Google-сесія протухає ~10хв → `python -m notebooklm login` (потребує Playwright chromium). notebook-add-doc.sh тепер idempotent (dedup by ID).

## План сесій

S0✅ → S1✅(M1+M2) → S2✅(M3+M4) → S3✅(M5+M6) → S4✅(M7) → S5✅(M8) → **S5+/S6(M9)** → S6(M10) → S7(M11,Pi) → S8(M12,Pi) → S9(M13+M14) → S10(M15,Pi+FC) → S11+(M16–M18)

## Вивчені пастки (всі сесії)

- C++ most vexing parse → braces; ambiguous `{}` overload → named var
- Health Booting→Degraded на першому "поганому" кадрі
- `std::optional` не → bool implicit (test helper templated)
- Забутий `<limits>`/`<cmath>`/`<bit>`/`<string>` include — кожен test/tool явно інклюдить що вживає
- Sign convention direction shift — consistent між helper і kernel
- Малі синтетичні patterns мають ambiguity — реальні 64×48 кадри variance більший
- WSL/NTFS "Clock skew"/"modification time in the future" warning — нешкідливе
- Integer bounding: int64 проміжне щоб int32 не переповнював
- MAVLink deferred emit: signed-frame треба буферити (pending_), бо `out` не персистить між parse_byte викликами
- MAVLink v2 truncation: payload буфер zero-fill, декодер читає фіксовані offsets безпечно
