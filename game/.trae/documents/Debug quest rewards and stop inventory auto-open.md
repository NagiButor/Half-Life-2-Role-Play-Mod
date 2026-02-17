## 1) Почему инвентарь открывается после сдачи квеста
- Нашёл точную причину: `CVGuiInventoryPanel::SetItems()` **всегда вызывает** `BeginInventory()` ([vgui_inventorypanel.cpp:L1384-L1392](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.cpp#L1384-L1392)).
- Когда награда шлёт `Inventory_Update`, клиент получает новые items и тут же открывает окно.

## 2) Исправление авто-открытия
- Изменить `SetItems()` так:
  - если панель **видима** → `BeginInventory()` (как раньше, чтобы UI обновлялся);
  - если панель **не видима** → только обновить `m_Items` и НЕ открывать UI.

## 3) Дебаг наград через DevMsg (чтобы ты присылал консоль)
- Добавить `ConVar quest_reward_debug` (по умолчанию 0).
- Логи на сервере (через `DevMsg`, выводится при `developer 1`):
  - при `RegisterQuestDef()`/регистрации квеста: id, rewards count, список reward items;
  - при `CompleteQuest()`: id, найден ли def, rewards count, факт вызова `AddItemToPlayer*`, итоговый `SendInventoryToPlayer`;
  - в `InventorySystem::AddItemToPlayer`/`AddItemToPlayerWithBullets`: печатать что реально добавили (player, item, bullets/stack) при включённом convar;
  - в `InventorySystem::SendInventoryToPlayer`: печатать количество строк-итемов, которые ушли на клиента.
- Добавить явный `DevMsg`/toast в `dialogsystem_server.cpp` уже есть для fail-case; расширю его, чтобы логировать и success (при debug).

## 4) Как ты будешь присылать логи
- В игре включаешь: `developer 1; quest_reward_debug 1`.
- Делаешь сдачу квеста.
- Скидываешь кусок консоли от момента клика по опции сдачи до момента `Inventory_Update`.

После подтверждения я внесу правки в указанные файлы и проверю диагностику IDE.