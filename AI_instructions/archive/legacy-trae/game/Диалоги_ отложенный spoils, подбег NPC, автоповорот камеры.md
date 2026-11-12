## 1) Spoils после реплики (не мгновенно)
- Сейчас `spoils` обрабатывается сразу в `HandleDialogChoice` (сервер закрывает и помечает spoiled немедленно).
- Сделаю так: `spoils` превращается в «закрыть после окончания реплики». То есть игрок выбирает вариант → показывается реплика NPC → после окончания (delay/сцена) окно закрывается → только тогда NPC считается spoiled.

**Реализация**
- Добавить в payload ещё 1 поле: `pendingSpoil` (0/1).
- На сервере:
  - Если `node->spoils == 1` — отправлять ноду с `pendingSpoil=1`, принудительно `autoClose=1`, `autoNext=-1`, и **без options**.
  - Если `option.spoils == 1`:
    - если `nextId != -1` — отправлять следующую ноду, но также принудительно `pendingSpoil=1`, `autoClose=1`, `autoNext=-1`, **без options** (чтобы это была финальная реплика).
    - если `nextId == -1` — отправлять пустую финальную ноду с delay=0, `pendingSpoil=1`, `autoClose=1`.
- Добавить серверную команду `dialog_finish_spoil <nodeId> <entityName>` (clientcmd_can_execute):
  - `MarkEntitySpoiled(entityName)` + `MarkEntitySpoken(entityName)`
  - `dialog_open_end` игроку.
- На клиенте:
  - хранить `m_pendingSpoil` в `CVGuiDialogPanel`
  - когда наступает авто-закрытие (логика autoClose/autoNext без options) или при скипе ЛКМ — слать `dialog_finish_spoil ...` вместо `dialog_continue ...`.

## 2) trigger_dialog_start: опция «подбежать перед диалогом»
- Расширить `trigger_dialog_start` ключами:
  - `npc_approach` (0/1)
  - `approach_distance` (float, например 96)
  - `approach_run` (0/1)
- Логика:
  - при касании игроком: если `npc_approach=0` — как сейчас, сразу `StartDialogForEntity`.
  - если `npc_approach=1` и NPC — `CAI_BaseNPC`:
    - вызвать `ScheduledMoveToGoalEntity(SCHED_FORCED_GO_RUN/GO, player, ACT_RUN/ACT_WALK)`
    - включить Think у триггера и ждать пока NPC приблизится на `approach_distance`, потом стартовать диалог.
    - добавить таймаут (например 5 сек), чтобы не зависнуть.
- Обновить FGD `hl2rpm.fgd`, чтобы новые keyvalues появились в Hammer.

## 3) Скриптовая настройка автоповорота камеры на NPC
- Добавить в корень Dialog-скрипта: `auto_face_npc "0/1"`.
- Передавать в payload ещё 1 поле: `autoFace` (0/1) (лучше вместе с pendingSpoil).
- На клиенте:
  - если `autoFace=1` при старте диалога — запустить плавный поворот viewangles к NPC.
  - цель: точка между грудью и шеей = `pNPC->WorldSpaceCenter() + Vector(0,0,16)`.
  - обновление очень частое: включить TickSignal на 1мс только пока идёт автоповорот; остановить через ~0.4–0.6 сек или когда дельта углов маленькая.
  - ЛКМ-скип не должен ломать поворот (поворот может продолжиться параллельно, либо сразу завершаться — выберу вариант «продолжить коротко»).

## Затрагиваемые файлы
- Сервер: [dialog_definitions.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/dialog_definitions.h), [dialog_definitions.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/dialog_definitions.cpp), [dialogsystem_server.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/dialogsystem_server.cpp), [triggers.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/triggers.cpp)
- Клиент: [vgui_dialogpanel.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_dialogpanel.h), [vgui_dialogpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_dialogpanel.cpp)
- Hammer: [hl2rpm.fgd](file:///e:/XBLAH%27s%20modding%20tool/tools/FGDs/Mapbase/hl2rpm.fgd)

## Проверка
- `spoils 1`: после выбора показывается финальная реплика, затем закрытие и только после этого NPC становится «нельзя говорить».
- `trigger_dialog_start` с `npc_approach=1`: NPC бежит к игроку и только затем стартует.
- `auto_face_npc=1`: камера плавно доворачивается на NPC при начале диалога.