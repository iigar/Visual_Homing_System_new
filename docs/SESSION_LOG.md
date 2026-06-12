# SESSION_LOG — журнал сесій

> Append-only. Новий запис зверху.

## 2026-06-13 — S0: Bootstrap

**Зроблено:**
- Скелет репо: core/ (include/vh, src, tests), scripts/, docs/, artifacts/ (ignored)
- Git init, remote `origin` → github.com/iigar/Visual_Homing_System_new
- `.gitattributes`: LF для .sh/.cpp/.md (критично для Pi), binary для .pgm/.vhrs
- Root CMakeLists: C++20, -Wall -Wextra -Wpedantic -Werror, 5 safety options OFF за замовчуванням, FATAL_ERROR на часткові live-конфігурації
- `vh/frame.hpp`: Frame + PixelFormat (Gray8, Thermal16 reserved), valid()
- `vh/interfaces.hpp`: 9 інтерфейсів (ICameraSource, IPreprocessor, IRouteWriter/Reader, IRouteMatcher, ITelemetrySource, INavigator, ICommandSink, IAuditLog, ISafetyGate)
- `vh_test.hpp`: мінімальний test helper, test_sanity зелений
- `scripts/build-test-desktop.sh`: explicit OFF для всіх live опцій
- Docs: PROJECT_MEMORY, ROADMAP (18 чекбоксів), DECISIONS (D-001…D-006), CLAUDE.md роутер
- NotebookLM notebook `851a3eee` створений, промпт залитий (зроблено до S0)

**Середовище перевірене:** WSL Ubuntu 24.04 / GCC 13.3 / CMake 3.28 / Ninja 1.11. Pi: OS Trixie (підтверджено користувачем).

**Незавершене:** ARCHITECTURE.md — заплановано на S1 (після перших реальних реалізацій діаграма буде точнішою).

**Наступне:** S1 = M1 (replay input) + M2 (preprocessing + health).
