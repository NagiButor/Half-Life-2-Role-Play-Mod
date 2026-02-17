## Что реально показывает твой лог
- `open after` превращает `weapon_pistol` в `weapon_pistol:0:0:0:-1:0`, и это нормально.
- Дальше начинается невозможное: при `take_slot before` сервер видит вместо `weapon_shotgun:0:0:0:-1:0` строку `'face flex'`, а потом вообще мусор байтами. Это не логика инвентаря — это **порча данных строки loot_items на ragdoll**.

## Корневая причина
`CRagdollProp` хранит лут как `string_t m_strLootItems` и сеттер сейчас делает:
- `m_strLootItems = MAKE_STRING(items);`

`MAKE_STRING()` в Source работает только когда `items` указывает на строку из глобальной string table (например keyvalue из карты). Когда мы передаём туда **runtime-строку** из `CUtlString` (`newLoot.Get()`, `rightPayload.Get()`), это обычный heap-буфер, не string table.
Итог: `string_t` получается неверным, а `STRING(m_strLootItems)` потом читает случайные строки вроде `face flex` или мусор.

Это на 100% совпадает с твоим логом: первый `loot='weapon_pistol;'` (из Hammer, валидно), а после первой же записи через `SetLootItems(rightPayload.Get())` начинается нестабильность.

## План фикса
1. **Исправить CRagdollProp::SetLootItems** в [physics_prop_ragdoll.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/physics_prop_ragdoll.cpp):
   - заменить `MAKE_STRING(items)` на `AllocPooledString(items)`.
   - оставить `NULL_STRING` для пустой строки.
   Это сделает `loot_items` безопасным для любых строк, которые строятся на лету.

2. (Опционально, но безопасно) **Так же перевести SetSourceClassName на AllocPooledString**, чтобы не зависеть от того, откуда пришёл classname.

3. **Проверка по твоему сценарию** с `sv_loot_debug 1`:
   - после Put/Take `take_slot before` должен показывать именно `weapon_shotgun:...`, а не `face flex`.
   - после повторного открытия больше не должно появляться кракозябр.

После этого уже можно оценивать, нужны ли дополнительные правки UI/формата, но сначала нужно убрать порчу хранения — сейчас она ломает всё выше по стеку.