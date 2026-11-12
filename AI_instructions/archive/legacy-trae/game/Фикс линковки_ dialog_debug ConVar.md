## Причина ошибки
- `dialog_debug` объявлен как `extern ConVar dialog_debug` в [dialog_debug.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/dialog_debug.h).
- Но определён сейчас **не как глобальный символ**, а внутри `namespace DialogDefinitions` в [dialog_definitions.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/dialog_definitions.cpp). Это другой mangled-символ, поэтому линковщик не находит `?dialog_debug@@3VConVar@@A`.

## План исправления
1) Перенести определение ConVar в глобальную область видимости
- Убрать `ConVar dialog_debug(...)` из `namespace DialogDefinitions`.
- Добавить `ConVar dialog_debug("dialog_debug", "0", FCVAR_GAMEDLL, "Enable dialog system debug prints");` **в глобальной области** (например, в `dialog_definitions.cpp` сразу после include’ов).

2) (Альтернатива, если предпочитаешь чище) сделать отдельный .cpp
- Создать `dialog_debug.cpp` в `game/server`, который включает `dialog_debug.h` и содержит единственное определение `ConVar dialog_debug(...)`.
- В `dialog_definitions.cpp` тогда удалить текущее определение.

3) Проверка
- Пересобрать Server (Episodic) → ошибки `LNK2001 dialog_debug` и `LNK1120` должны исчезнуть.
- `LNK4075` оставить как предупреждение (не блокирует сборку).
