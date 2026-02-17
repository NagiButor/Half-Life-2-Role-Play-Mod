## Причина, почему сейчас «ничего не поднимает»
- В `inventory_grab` я проверял `ClassMatches("item_")` / `ClassMatches("weapon_")`.
- `ClassMatches` для префикса требует wildcard (например `item_*`). Без `*` совпадения нет, поэтому команда всегда выходила.

## Что изменю
- В `inventory_grab` заменить проверки на:
  - `ClassMatches("item_*")` и `ClassMatches("weapon_*")`.
- Убрать лишнюю проверку `VPhysicsGetObject()` и просто вызывать `pPlayer->PickupObject(target)` (как это делалось раньше в `CItem::Use` при выключенном инвентаре).
- Toggle-логика отпускания останется: если `GetUseEntity()` == `player_pickup`, повторное нажатие отпускает через `ClearUseEntity()`.

## Проверка
- `bind g inventory_grab`
- Навёлся на `item_*`/`weapon_*` → нажал `g` → предмет берётся в руки.
- Нажал `g` ещё раз → отпускается.
- Проверю диагностику IDE после правок.