# Visual Homing Codex — контекст для AI агента

> C++20 flight-safety route-following система. НЕ порт Python VO — інша парадигма (command-assist, не EKF position estimate).

## Старт кожної сесії (обовʼязково, в цьому порядку)

1. `docs/PROJECT_MEMORY.md` — стиснутий стан проєкту
2. `docs/ROADMAP.md` — статус 18 milestone (чекбокси)
3. `git log -5`
4. При потребі: `docs/DECISIONS.md` (чому), `docs/SESSION_LOG.md` (останній запис)

Повний промпт-специфікація: `D:\LLM\ChatGPT\Codex\Visual-Homing\Visual_Homing_Codex\docs\CLAUDE_CODE_PROMPT_NEW.md`

## Кінець кожної сесії

1. Оновити `docs/PROJECT_MEMORY.md` (перезаписати, НЕ дописувати — ліміт 150 рядків)
2. Додати запис у `docs/SESSION_LOG.md`
3. Оновити чекбокси `docs/ROADMAP.md`
4. Коміт + push
5. Оновити Obsidian: `D:\Obsidian\CloudCode\_claude\memory\visual_homing_session.md`

## Збірка і тести

```bash
# Desktop (WSL Ubuntu 24.04, GCC 13):
wsl -e bash -c "cd /mnt/d/Agents_ClaudeCode/Visual_Homing_System_new && ./scripts/build-test-desktop.sh"
```

- Білди ТІЛЬКИ через WSL (на Windows немає toolchain)
- Усі live-output CMake опції завжди явно OFF у звичайних скриптах
- Кожен тест = окремий CTest executable, helper: `core/tests/vh_test.hpp`

## Жорсткі правила безпеки

- НІКОЛИ не вмикати `VH_ENABLE_LIVE_OUTPUT` / `VH_ENABLE_WRITER_ATTACH` без явного запиту користувача з ревью
- Fail-closed скрізь: invalid input → zero command
- Кожна зміна safety boundary → запис у `docs/LIVE_OUTPUT_SAFETY_PLAN.md` + `DECISIONS.md`

## Інструменти

### NotebookLM (зовнішня пам'ять) — notebook `851a3eee`

**Коли ОБОВ'ЯЗКОВО запитати notebook замість читання у власний контекст:**
- MAVLink message specs (M8 parser, M17 encoder): "What is HEARTBEAT message layout?"
- MAVLink v1/v2 framing і CRC: "How is CRC computed for MAVLink v2?"
- ArduPilot GUIDED mode семантика і `SET_POSITION_TARGET_LOCAL_NED` type_mask
- libcamera C++ API (M11): "How do I configure a libcamera stream for Gray8?"
- PGM формат при сумнівах
- Свій же стан проєкту: "What are the VHRS hard caps?" замість читання `route_format.hpp`

**Команди:**
```bash
./scripts/notebook-ask.sh "<question>"             # запит
./scripts/notebook-add-doc.sh docs/X.md docs/Y.md  # синхронізувати docs після milestone
```

**Поточний вміст notebook (станом на S2):**
- `CLAUDE_CODE_PROMPT_NEW.md` (full spec)
- `PROJECT_MEMORY.md` / `DECISIONS.md` / `ROADMAP.md` / `SESSION_LOG.md`
- MAVLink Common Messages + Serialization
- ArduPilot Companion Computers / GUIDED / commands-in-GUIDED
- libcamera C++ API
- PGM netpbm spec

**Після кожного milestone:** запусти `notebook-add-doc.sh` на оновлені docs щоб notebook знав поточний стан.

### Інше

- **Codex** (`/codex:rescue`) — друга думка при складних багах
- **Pi target:** Raspberry Pi Zero 2W, Pi OS **Trixie** (Debian 13, GCC 14), libcamera

## Hardware (ціль)

| Компонент | Деталь |
|-----------|--------|
| Companion | RPi Zero 2W (4× A53 @ 1GHz, 512MB), Pi OS Trixie |
| FC | Matek H743-Slim V3 + ArduPilot |
| Камера | IMX219 (libcamera); Caddx Thermal 256 — окремий milestone |
| UART | /dev/serial0, MAVLink v1/v2, 115200 |
