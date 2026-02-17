## Требования (как понял)
- У каждого предмета есть вес (кг); если предметы стакаются (в т.ч. патроны) — вес суммируется.
- Есть настраиваемый лимит веса игрока и прочие параметры (скорость при перегрузе, тексты, включение/выключение и т.д.).
- При превышении лимита игрок ходит очень медленно.
- В инвентаре показывается текущий вес и максимальный.
- Сверху слева появляется уведомление "You are overencumbered" в стиле текущих toast-уведомлений.

## Текущее состояние в коде (важное для реализации)
- Инвентарь синкается на клиент через usermessage `Inventory_Update` с типами begin/add/end (см. shared/inventory_netmessages.h).
- Toast-уведомления уже есть: команда `ui_toast` показывает произвольный текст сверху слева (см. client/vgui_inventorypanel.cpp).
- Скорость игрока в HL2 задаётся через `SetMaxSpeed(...)` (см. server/hl2/hl2_player.cpp), поэтому перегруз логично внедрить как «кламп максимальной скорости».

## План реализации
### 1) Конфиги и таблица весов
- Добавить ConVar’ы (серверные):
  - `sv_inventory_weight_enable` (0/1)
  - `sv_inventory_weight_max` (float, кг)
  - `sv_inventory_weight_over_speed` (float, например 60 или множитель)
  - `sv_inventory_weight_over_toast` (0/1)
  - `sv_inventory_weight_default_item`, `sv_inventory_weight_default_weapon`, `sv_inventory_weight_default_other`
  - (опционально) `sv_inventory_weight_include_equipped_weapons`, `sv_inventory_weight_include_equipped_suit`
- Сделать загрузку весов из KeyValues-файла (например `scripts/inventory_weights.txt`):
  - ключ = classname, значение = вес (кг)
  - в файле можно задавать веса для ammo/weapon/item/костюмов и редких классов.
  - если в файле не найдено — падать на дефолтные веса по префиксу (`weapon_`, `item_`, прочее).

### 2) Подсчёт веса
- В server/inventory_system.cpp добавить функцию:
  - `float InventorySystem::GetPlayerInventoryWeight(CBasePlayer*)` (суммирует веса всех `InvItem` в g_PlayerInventories)
  - Для ammo-entries использовать их «bullets» как количество и умножать на вес патрона (из KV/дефолта).
  - Для обычных items/оружия брать фиксированный вес по classname.
- Добавить функцию `float InventorySystem::GetPlayerMaxCarryWeight(CBasePlayer*)`, которая возвращает текущий лимит (пока просто ConVar; позже можно расширять под бонусы костюмов/перки).

### 3) Эффект перегруза (очень медленно)
- В server/hl2/hl2_player.cpp (или в общем player think, если нужно) добавить проверку перегруза:
  - `bool over = (curWeight > maxWeight)`
  - Если `over`:
    - принудительно ограничить `SetMaxSpeed( sv_inventory_weight_over_speed )` (или `min(current, current*mult)`)
    - (опционально) выключить прыжок/спринт как дополнительный «штраф»
  - Если `!over` — вернуть нормальную скорость по текущему состоянию (walk/sprint/suit).
- Хранить на сервере «предыдущее состояние перегруза» на игроке (чтобы не спамить уведомления каждую тик).

### 4) Отображение веса в UI инвентаря
- Расширить shared/inventory_netmessages.h новыми msgType (например `kInventoryUpdate_InventoryWeight = 4`).
- В InventorySystem::SendInventoryToPlayer отправлять дополнительное сообщение с:
  - текущий вес (float)
  - максимальный вес (float)
  - флаг перегруза (byte/bool)
- На клиенте в client/clientmode_shared.cpp (обработчик `__MsgFunc_Inventory_Update`) разобрать новый тип и сохранить значения в CVGuiInventoryPanel.
- В client/vgui_inventorypanel.h/.cpp добавить:
  - поля `m_flCurWeight`, `m_flMaxWeight`, `m_bOverencumbered`
  - новый Label внизу (в footer) вида `Weight: 12.5 / 35.0 kg` (и, если перегруз — красным/с акцентом).

### 5) Уведомление сверху слева
- При переходе состояния `over: false -> true` на сервере отправлять:
  - `engine->ClientCommand(pPlayer->edict(), "ui_toast \"inventory\" \"You are overencumbered\" \"friends/message.wav\"\n")`
- (Опционально) при возврате в норму показать `"You are no longer overencumbered"` — но только если нужно.

## Затрагиваемые файлы
- [server/inventory_system.cpp] — веса, подсчёт, отправка в UI.
- [shared/inventory_netmessages.h] — новый тип сообщения.
- [client/clientmode_shared.cpp] — парсинг нового сообщения.
- [client/vgui_inventorypanel.h/.cpp] — отображение веса.
- [server/hl2/hl2_player.cpp] — ограничение скорости при перегрузе.
- Новый файл: `scripts/inventory_weights.txt` (таблица весов).

## Проверка
- Локально: накидать в инвентарь много патронов/оружия, убедиться:
  - вес в UI обновляется,
  - при превышении лимита скорость падает сильно,
  - toast появляется один раз при входе в перегруз,
  - при снятии/дропе веса — скорость возвращается.

Если ок — подтверждай план, и я внесу изменения.