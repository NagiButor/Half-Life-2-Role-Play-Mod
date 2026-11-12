## Демонстрация: returning_line → сразу к опциям
Есть два рабочих варианта.

### Вариант A (без перестройки нодов)
Если у стартового нода уже есть `options` (как в текущем `test_npc`), то чтобы после returning-фразы **сразу показывались кнопки-ответы**:
- не ставь `returning_no_options=1`
- не ставь `returning_auto_close=1`
- при желании поставь на стартовом ноде `option_delay "0"`, чтобы кнопки появлялись сразу.

### Вариант B (правильный для autonext-цепочек)
Если стартовый нод “приветствие” должен быть без опций (как в `test_npc_2`), а опции — в отдельном ноде (например `10`), то делай так:
- `returning_auto_next "10"`
- `returning_auto_close "0"`
- (опционально) `returning_no_options "1"` чтобы на returning-фразе кнопки не появлялись раньше времени.

## Требования к квестам (как реализую)
- Один UI (TAB) с переключением `Inventory`/`Quests`.
- Квест = кнопка с названием; клик раскрывает описание + кнопку `Show on map` (пока заглушка).
- Данные квестов (id/название/описание/условия) лежат в `scripts/dialogs/*.txt` рядом с диалогами NPC.
- Диалоги могут выдавать/продвигать/завершать/проваливать квесты.
- Фаллаут-логика: kill→talk→complete, принести→complete, сказать→complete; убийство важного NPC снижает репутацию и проваливает квест; низкая репутация блокирует старт.
- Уведомления (как FO3/NV) всегда появляются в одном месте (вместо текущего центра), со звуками:
  - start quest: `friends\friend_join.wav`
  - complete quest: `friends\friend_online.wav`
  - inventory add/remove: `friends\message.wav`

## Сервер: QuestSystem
- Добавлю новый модуль `quest_system.h/.cpp` (server) с состоянием квестов на игрока:
  - активные/завершённые/проваленные, текущая стадия/шаг
  - репутация (минимум: одна числовая переменная на игрока, позже можно расширить до по-фракциям)
- Поддержка типов целей (MVP):
  - `kill_target` (targetname или classname)
  - `talk_to` (entity targetname)
  - `have_item` / `turn_in_item` (инвентарь: classname + count)
  - `say_flag` (выставляется диалогом при выборе опции)
  - `fail_on_kill` (важный NPC) + `rep_penalty`
- Хуки:
  - на убийства: перехват `Event_Killed`/game event, проверка `fail_on_kill` и/или `kill_target`
  - на диалог: при выборе опции/отправке нода — триггеры `grant_quest/advance/complete/fail` и `say_flag`
  - на инвентарь: проверка `have_item/turn_in_item`.

## Скрипты dialogs: формат данных
- В `Dialog { ... }` добавлю блок `quests`:
  - `"quests" { "quest_id" { "title" "..." "desc" "..." "min_rep" "..." ... } }`
- В `options` и/или `nodes` добавлю ключи действий:
  - `grant_quest "quest_id"`
  - `complete_quest "quest_id"`
  - `fail_quest "quest_id"`
  - `set_flag "flag_id"` (для say/реплик)
  - `require_rep "X"` (если репутация ниже — опция скрыта/недоступна и выдаётся причина в UI)
- Парсинг расширю в `dialog_definitions.cpp` (в `ParseOptionKV`/`ParseNodeKV`) и прокину в `dialogsystem_server.cpp` при обработке выбора.

## Клиент: вкладка Quests на основе Inventory UI
- В [vgui_inventorypanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.cpp) добавлю 2 “вкладки” сверху (как кнопки): `Inventory` и `Quests`.
- Реализую `Quests` страницу:
  - список кнопок-квестов
  - раскрытие выбранного квеста: блок описания + кнопка `Show on map` (пока делает только уведомление)
- Добавлю клиентский приёмник данных квестов (по аналогии с `Inventory_Update`).

## Сеть: обновление квестов
- Добавлю новый usermessage `Quests_Update` и зарегистрирую его в [mapbase_usermessages.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/shared/mapbase/mapbase_usermessages.cpp#L43-L59).
- Сервер будет слать `Quests_Update` пакетами Begin/Add/End (как инвентарь) и клиент будет собирать список и обновлять UI.

## Уведомления (FO3/NV стиль) и звуки
- Заменю текущий `inventory_pickup_notify`-попап (который сейчас по центру) на общий “toast”-панель в правом верхнем углу.
- Введу единый клиентский путь показа:
  - тип события (quest start/complete, item add/remove)
  - текст
  - звук (один из трёх путей)
- `InventorySystem::NotifyPlayerPickup` и будущие квест-события будут использовать один и тот же механизм.

## Верификация
- Диагностика проекта.
- Smoke-тест: открыть TAB → переключить Quests → получить квест из диалога → увидеть toast + звук → завершить условия (kill/talk/bring) → завершить квест → toast + звук.
- Совместимость: инвентарь и диалоги должны работать без квестов, если в scripts нет блока `quests`. 