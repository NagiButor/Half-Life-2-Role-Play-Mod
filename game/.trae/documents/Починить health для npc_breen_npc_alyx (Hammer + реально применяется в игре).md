## Диагноз
- В FGD `npc_breen` и `npc_alyx` наследуются от `TalkNPC` ([halflife2_vizzys.fgd](file:///e:/XBLAH's%20modding%20tool/tools/FGDs/Mapbase/halflife2_vizzys.fgd#L2209-L2256), [L2310-L2318](file:///e:/XBLAH's%20modding%20tool/tools/FGDs/Mapbase/halflife2_vizzys.fgd#L2310-L2318)). Явного `health(...)` в их блоках нет.
- В коде override здоровья по карте работает через `m_iMapHealthOverride`, который выставляется только если в энтити реально пришёл KV `health` в `CAI_BaseNPC::KeyValue()` ([ai_basenpc.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/ai_basenpc.cpp#L13488-L13496)), и применяется в `CAI_BaseNPC::NPCInit()` ([ai_basenpc.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/ai_basenpc.cpp#L8246-L8288)).
- То, что у тебя `npc_breen health 200` “не ставится”, наиболее похоже на ситуацию, когда в карте попадает не ключ `health` (например `Health`/другой кейс или вообще ключ не проставлен из-за отсутствия SmartEdit-поля).

## Что сделаю
### 1) Починить отображение в Hammer (чтобы ключ гарантированно появился)
- В `TalkNPC` (в начале [halflife2_vizzys.fgd](file:///e:/XBLAH's%20modding%20tool/tools/FGDs/Mapbase/halflife2_vizzys.fgd#L17-L29)) добавлю `health(integer)` с подсказкой, чтобы все talk-NPC (включая `npc_breen`, `npc_alyx`, и т.п.) точно имели поле в SmartEdit.
- Дополнительно (на всякий случай) добавлю `health(integer)` прямо в блоки `npc_breen` и `npc_alyx` с их дефолтом:
  - `npc_breen` дефолт 8 (хардкод в [npc_breen.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/hl2/npc_breen.cpp#L83-L115)).
  - `npc_alyx` дефолт 80 (или `sk_alyx_health`) в [npc_alyx.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/hl2/npc_alyx.cpp#L115-L144).

### 2) Сделать KeyValue здоровья более “пуленепробиваемым”
- В `CAI_BaseNPC::KeyValue()` расширю обработку health на case-insensitive (`health`/`Health`), чтобы даже при ручном вводе в Hammer или старых VMF override всегда срабатывал.

### 3) Верификация
- Сделаю .bak копии затронутых FGD.
- Проверю, что `health(` встречается для `npc_breen`/`npc_alyx` в FGD.
- Прогоню диагностику.

## Ожидаемый результат
- В Hammer у `npc_breen` и `npc_alyx` появляется поле `Health` (SmartEdit).
- В игре `npc_breen` с `health 200` реально становится живучим (не умирает от 1 выстрела), потому что `m_iMapHealthOverride` начинает корректно выставляться и применяться в `NPCInit()`.