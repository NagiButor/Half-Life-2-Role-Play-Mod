## Диагноз (почему сейчас получается returning_line → 2 → 3)
- Сейчас повторное открытие диалога делается так: сервер всегда открывает `start_node`, но если NPC уже “spoken”, то подменяет только текст первой реплики на `returning_line` ([dialog_definitions.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/dialog_definitions.cpp#L349-L366)).
- `auto_next` живёт на самом стартовом ноде (как в [test_npc_2.txt](file:///e:/Steam/steamapps/sourcemods/hl2rpm/scripts/dialogs/test_npc_2.txt#L12-L21)), поэтому после проигрывания “возвратной” фразы UI автоматически шлёт `dialog_continue`, и сервер продолжает ровно по `node->autoNextId`/`node->autoClose` ([dialogsystem_server.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/dialogsystem_server.cpp#L122-L157)).
- Важно: значения `autoNextId/autoClose`, которые сервер отправляет в payload, сейчас **не используются на сервере при `dialog_continue`** — сервер снова читает настройки из нода. Поэтому “переопределить авто-переходы только для returning” нельзя без небольшой правки протокола.

## Цель
- Сделать `returning_line`/`returning_choreo` настраиваемыми так, чтобы при повторном разговоре можно было:
  - закрывать диалог после returning-фразы,
  - либо прыгать на конкретный нод (например сразу на нод с опциями),
  - и не получать странный эффект “returning_line + середина цепочки autonext”.

## Изменения в скриптах (новые ключи)
Добавлю опциональные ключи на уровне `Dialog { ... }`:
- `returning_auto_next` (int): какой нод авто-открывать после returning-фразы (`-1` = не переходить).
- `returning_auto_close` (bool 0/1): закрывать ли диалог после returning-фразы.
- (опционально) `returning_no_options` (bool 0/1): скрыть кнопки на returning-фразе, даже если они есть.

## Сервер (C++)
1) **Парсинг новых ключей**
- Расширю структуру `DSDialog` в [dialog_definitions.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/dialog_definitions.h) и загрузку KV в [dialog_definitions.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/dialog_definitions.cpp), чтобы хранить `returning_auto_next/returning_auto_close/returning_no_options`.

2) **Отправка первого нода при returning**
- В `SendDialogToPlayer()` вместо обычного `SendDialogNodeToPlayer(...)` при returning буду вызывать `SendDialogNodeToPlayerEx(...)` и передавать `forcedAutoNextId/forcedAutoClose/forceNoOptions`, чтобы клиент знал нужные авто-настройки именно для returning-открытия.

3) **Чтобы это реально влияло на поведение (важный пункт)**
- Изменю `dialog_continue` протокол: клиент будет отправлять `dialog_continue <nodeId> "<entity>" <autoNextId> <autoClose>`.
- Серверный обработчик в [dialogsystem_server.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/dialogsystem_server.cpp) будет:
  - если пришли 4-й и 5-й аргументы — использовать их как override,
  - иначе (старый формат) — как сейчас, читать `pNode->autoNextId/autoClose`.

## Клиент (VGUI)
- В [vgui_dialogpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_dialogpanel.cpp#L646-L692) при авто-авансинге буду отправлять `dialog_continue` с текущими `m_autoNextId/m_autoClose`, чтобы сервер мог применить именно те авто-правила, которые прислал в payload.

## Обновление тестовых диалогов
- В [test_npc_2.txt](file:///e:/Steam/steamapps/sourcemods/hl2rpm/scripts/dialogs/test_npc_2.txt) добавлю, например:
  - `returning_auto_next "-1"`
  - `returning_auto_close "1"`
  Тогда повторный разговор будет: returning_line/returning_choreo → авто-закрытие, без продолжения в нод 10.
- В [test_npc.txt](file:///e:/Steam/steamapps/sourcemods/hl2rpm/scripts/dialogs/test_npc.txt) оставлю текущий режим (там нет `auto_next` на старте), либо тоже покажу пример (на твой вкус) — но менять поведение без необходимости не буду.

## Проверка
- Быстрый smoke-тест через `dialog_force test_npc_2`:
  - 1-й раз: 0 → auto_next 10 как раньше.
  - 2-й раз: показывается returning_line + returning_choreo и затем закрывается/прыгает как задано.
- Проверю, что старые диалоги без новых ключей работают как раньше (обратная совместимость команды `dialog_continue`).