## Почему это происходит
- Автоподбор у `weapon_*` уже отключён (у вас есть блок в `CBaseCombatWeapon::DefaultTouch()`), но **`item_*` всё ещё подбираются по Touch** через `CItem::ItemTouch()`.
- Ваш `inventory_scan_map` отключает Touch только для предметов, уже стоящих на карте в `LevelInitPostEntity()`. Всё что появляется позже (дроп с NPC, лут из `item_item_crate`, dynamic resupply) создаётся заново и сохраняет дефолтный Touch → автоподбор возвращается.

## Что сделаю (фикс по месту, который ловит ВСЕ источники дропа)
### 1) Глобально запретить автоподбор `item_*` при включённом инвентаре
- В `CItem::ItemTouch()` добавить ранний выход:
  - если `sv_inventory_enable == 1` и предмет **не** помечен `SF_ITEM_ALWAYS_TOUCHABLE` → просто `return` (не подбирать по касанию).
- Это сразу исправит: `item_ammo_ar2_altfire`, любые `item_health*` и содержимое `item_item_crate`, и любые будущие `item_*` из resupply/NPC drops.

### 2) Не сломать использование предметов из инвентаря (`inventory_use`)
- Сейчас `inventory_use` для `item_*` специально «выплёвывает» предмет перед игроком и оставляет Touch включённым, чтобы `MyTouch()` мгновенно применил эффект.
- После пункта (1) это перестанет работать, поэтому:
  - при спавне `item_*` из `inventory_use` выставлять `SF_ITEM_ALWAYS_TOUCHABLE` (и в ветке `GiveNamedItem`, и в fallback `Create()`), чтобы такой предмет **мог** сработать по Touch.

## Файлы правок
- `server/item_world.cpp` — правка `CItem::ItemTouch()`.
- `server/inventory_system.cpp` — выставление `SF_ITEM_ALWAYS_TOUCHABLE` при `inventory_use` для `item_*`.

## Проверка
- Убить NPC/сломать `item_item_crate` и пройтись по дропу: предметы больше не подбираются сами.
- Нажать `E` на дропе: предмет уходит в инвентарь (через `CItem::Use()` как сейчас).
- Вызвать `inventory_use item_healthkit` (или аналог): предмет по-прежнему мгновенно применится.
- Проверю диагностику IDE после правок.