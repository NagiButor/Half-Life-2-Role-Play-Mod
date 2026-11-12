## Что уже есть (важно)
- **NPC может начать диалог через триггер уже сейчас** без нового кода: в Hammer на триггере `OnStartTouch -> <NPC targetname> -> Use`. В [baseentity.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/baseentity.cpp#L2828-L2862) есть хук, который при `Use` от игрока вызывает `StartDialogForEntity`.
- Клиент уже умеет ждать окончания реплики/сцены перед показом вариантов: `option_delay` + ожидание конца `C_SceneEntity` (см. [vgui_dialogpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_dialogpanel.cpp#L487-L535)).

## 1) «NPC первый начал диалог» (триггер/сценарии)
- Оставить текущий рабочий путь через `Use` (минимум кода, гибко для маппера).
- Дополнительно (для удобства): добавить серверную сущность `trigger_dialog` или `logic_dialog_start` с полями `npc_name`/`dialog_entity`, которая на `StartTouch` вызывает `DialogSystemServer::StartDialogForEntity(...)`. Это позволит запускать без промежуточного `Use`.

## 2) Несколько реплик NPC подряд (Fallout‑стиль)
- Добавить в скрипт ноды новые поля:
  - `auto_next` (int) — ID следующей ноды, на которую надо перейти автоматически.
  - `end`/`close` (bool) — завершить диалог после этой реплики.
- Правило: если у ноды **нет options**, но задан `auto_next` — после окончания речи (по `option_delay=auto` или числу секунд) автоматически перейти на следующую ноду. Так строится цепочка из нескольких реплик.

## 3) NPC завершает диалог без кнопок
- Если у ноды **нет options** и стоит `end=1` (или `auto_next=-1` + `end=1`) — по завершению речи диалог закрывается.
- Сервер при закрытии помечает `MarkEntitySpoken` (и при необходимости spoil‑логику), затем отправляет `dialog_open_end`.

## 4) Скип реплик кликом мыши
- Расширить клиентскую панель:
  - Если сейчас идёт ожидание (`m_waitForSceneEnd` или `m_optionAvailableAt` в будущем) — **клик ЛКМ**:
    - останавливает текущую сцену (уже есть в `SetChoreo()` при смене ноды),
    - и либо мгновенно показывает варианты (если options есть),
    - либо инициирует переход на `auto_next`/закрытие (если options нет).

## Техническая реализация (как именно)
- **Payload**: расширить формат `dialog_open_payload` и добавить 2 поля после delay:
  - `autoNextId` и `autoClose` (или `end`). Клиент парсит при `parts.Count() >= 12`, старый формат остаётся рабочим.
- **Сервер**:
  - В `dialog_definitions.h/.cpp` добавить в `DSDialogNode` поля `autoNextId`, `autoClose` и распарсить их из KV.
  - В `SendDialogNodeToPlayer()` дописывать эти поля в payload.
  - Добавить серверную concommand `dialog_continue <nodeId> <entityName>` (разрешённую клиенту, как `dialog_choose`), которая:
    - валидирует, что нода существует,
    - если `autoNextId != -1` — отправляет следующую ноду,
    - если `autoClose` — закрывает диалог и отмечает spoken.
- **Клиент**:
  - В `vgui_dialogpanel` хранить `m_autoNextId`, `m_autoClose`.
  - В обработчике payload выставлять эти поля.
  - В `OnMousePressed` и/или `OnTick` реализовать:
    - «показать options сейчас» (снять ожидание),
    - либо отправить `engine->ClientCmd("dialog_continue ...")`.

## Проверка (минимальные кейсы)
- Trigger->Use запускает диалог без нажатия E.
- Цепочка из 2–3 нод без options с `auto_next` проходит автоматически.
- Финальная нода с `end=1` закрывает окно без кнопок.
- ЛКМ во время реплики: мгновенно показывает кнопки или перескакивает на следующую реплику.

Если ок — приступаю к правкам в `dialog_definitions.*`, `dialogsystem_server.*`, `vgui_dialogpanel.*` и добавлю пример в `scripts/dialogs/test_npc.txt` с цепочкой реплик.