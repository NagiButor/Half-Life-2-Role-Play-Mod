## Что уже есть в коде (важно для дизайна)
- Лут сейчас привязан к **CRagdollProp**: у игрока хранится `s_LootTargets[player]` и все команды `loot_take/put/...` работают строго с ragdoll ([loot_system.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/loot_system.cpp)).
- `loot_items` уже существует как KeyValue и у NPC, и у ragdoll:
  - ragdoll: `DEFINE_KEYFIELD( m_strLootItems, FIELD_STRING, "loot_items" )` ([physics_prop_ragdoll.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/physics_prop_ragdoll.cpp#L91-L105))
  - NPC: `DEFINE_KEYFIELD( m_iszLootItems, FIELD_STRING, "loot_items" )` (см. агент-отчёт)

## Цель 1: Лутание ящиков/сундуков через новую brush-энтити
### 1) Новая энтити
- Добавлю новую **brush-энтити** (под Tie to entity в Hammer), например `func_loot_container`.
- Базой возьму `CFuncBrush` (он уже поддерживает +use через `FCAP_IMPULSE_USE`) ([modelentities.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/modelentities.h)).
- На `Use()` у этой энтити будет открываться окно лута так же, как у ragdoll.

### 2) Настраиваемые KeyValues (как ты описал)
У `func_loot_container` будут KV:
- `loot_displayname` (Displayable name)
- `loot_sound_open`
- `loot_sound_close`
- `loot_items`

### 3) Обобщение loot_system (чтобы работало и для ragdoll, и для контейнеров)
- Заменю `static CHandle<CRagdollProp> s_LootTargets[]` на **универсальный** `EHANDLE`/`CHandle<CBaseEntity>`.
- Введу маленький “адаптер” (на уровне функций), чтобы `loot_system` мог:
  - получить/задать `loot_items` у текущей цели (ragdoll или контейнер)
  - получить displayname/open/close sounds
- Все существующие ConCommand’ы (`loot_take_slot`, `loot_put_slot`, `loot_take_all`, ...) продолжат работать, но будут работать с **любой текущей loot-целью**.

### 4) Передача displayname + звуков на клиент
- Сейчас сервер отправляет только строку `left|right` в `loot_open_payload_local`.
- Добавлю новую клиентскую команду, например: `loot_open_payload2_local "<payload>" "<displayname>" "<sound_open>" "<sound_close>"`.
- На клиенте (vgui_lootpanel.cpp) обработчик:
  - устанавливает `m_pRightLabel` = Displayable name (если пусто — оставляет дефолт)
  - сохраняет звуки открытия/закрытия в поля панели
  - открывает UI как сейчас.
- Для обратной совместимости можно оставить старую `loot_open_payload_local` и просто вызывать новый путь с дефолтами.

### 5) Звуки для контейнеров
- В LootPanel звуки открытия/закрытия будут **персональные**:
  - если сервер прислал `loot_sound_open/close`, панель играет их
  - иначе использует текущие дефолты (npc\\combine_soldier\\zipline_hitground2/1.wav).

## Цель 2: FGD — добавить displayable name и loot items для ragdoll
### 1) Код на сервере
- Добавлю в `CRagdollProp` новые поля и KeyValues:
  - `loot_displayname`
  - (опционально) `loot_sound_open`, `loot_sound_close` — если захочешь ещё и это настраивать для трупов
- Чтобы трупы из NPC тоже получали displayname, добавлю аналогичный KV в `CAI_BaseNPC` и скопирую в ragdoll при создании (там где сейчас копируется `loot_items`).

### 2) Правки FGD
- В твоих FGD (папка `e:\XBLAH's modding tool\tools\FGDs\Mapbase`) сделаю:
  - Добавлю **новую энтити** `func_loot_container` как `@SolidClass` с нужными KV и описаниями.
  - В определениях ragdoll (`prop_ragdoll` / `physics_prop_ragdoll`) добавлю поля:
    - `loot_displayname` (Displayable name)
    - `loot_items` (если вдруг отсутствует/не выведено в твоей версии FGD).

## Проверка поведения (что именно будет работать)
- В Hammer:
  - делаешь brush (trigger-текстура на крышке сундука/ящика)
  - Tie to entity → `func_loot_container`
  - задаёшь: Displayable name, Sound open/close, Loot items
- В игре:
  - по E на ящике открывается Loot UI
  - справа показывается твой Displayable name
  - звуки открытия/закрытия берутся из KV
  - предметы реально хранятся в энтити и меняются при take/put/put all/take all
- Для ragdoll:
  - можно настраивать displayable name и loot_items через FGD

Если всё ок — после подтверждения сразу внесу изменения в: server/loot_system.cpp (+новый класс контейнера), server/physics_prop_ragdoll.* и твои FGD-файлы в указанной папке.