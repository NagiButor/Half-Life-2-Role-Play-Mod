## Как сейчас хранится лут у каждого NPC
- У каждого NPC **есть своё поле** `m_iszLootItems` (keyvalue `loot_items`). Это часть `CAI_BaseNPC`: [ai_basenpc.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/ai_basenpc.cpp#L12379-L12380), геттер: [ai_basenpc.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/ai_basenpc.h#L978-L979).
- При смерти NPC создаётся `prop_ragdoll`, и **ровно один раз** копируется строка лута из NPC в ragdoll: [physics_prop_ragdoll.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/physics_prop_ragdoll.cpp#L1512-L1516).
- Дальше «база данных» лута трупа — это **сам ragdoll**, поле `CRagdollProp::m_strLootItems` (`loot_items`), которое меняется `SetLootItems()` и используется при каждом открытии: [physics_prop_ragdoll.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/physics_prop_ragdoll.cpp#L270-L276).
- Если у NPC лута нет — `loot_items` пустая строка/NULL_STRING, правая колонка должна быть пустой.

## Что по симптомам не так
- “Put работает, Take не меняет ничего” + “после переоткрытия предмет становится мусором/пропадает” — почти наверняка означает, что **серверная строка `m_strLootItems` на ragdoll становится некорректной** (битый формат, пустые токены, невалидные classname, несогласованный формат weapon-записей). Клиентский VGUI лишь отображает присланное.

## План фикса (после подтверждения)
1. **Диагностика (обязательно, иначе будем гадать):**
   - Добавить `sv_loot_debug` и `DevMsg` в `OpenLootForPlayer`, `loot_put_slot`, `loot_take_slot`:
     - entindex игрока и ragdoll;
     - `GetLootItems()` до/после;
     - результат парса (Count + элементы);
     - запрошенный slot.
   Это сразу покажет, где ломается: команда не дошла / индекс не совпал / строка портится.

2. **Нормализация данных трупа как «источника истины»:**
   - Реализовать `NormalizeLootItems()` (сейчас заглушка) так, чтобы:
     - удалять пустые элементы;
     - жёстко нормализовать classname (как `NormalizeClassname()`), отбрасывая мусор;
     - для weapon гарантировать формат `weapon_class:bullets:clip1:clip2:reserveIdx:reserveCount` (6 токенов);
     - для ammo суммировать дубликаты.
   Это устранит «кривые» записи, из-за которых при повторном открытии всё разваливается.

3. **Свести парсинг weapon/ammo к одной функции:**
   - В `loot_system.cpp` сейчас две разные `ExtractLootEntryFull` (разные сигнатуры). Оставить одну «каноническую» и использовать её везде, чтобы исключить рассинхрон записи/чтения.

4. **Стабилизировать индексацию слотов:**
   - Сделать серверный `ParseLootItems()` симметричным клиентскому: не добавлять пустые токены.
   - Всегда прогонять `NormalizeLootItems()` перед `BuildLootItems()` и перед отправкой payload.

5. **Проверка на твоём кейсе:**
   - Убить NPC → открыть лут → Put shotgun → Take shotgun → закрыть/открыть снова.
   - По логам `sv_loot_debug 1` убедиться, что ragdoll `loot_items` меняется ожидаемо, и UI получает корректный payload.

Если по логам выяснится, что `loot_take_slot` вообще не исполняется (а не данные ломаются), тогда вместо шагов 2–4 переключусь на проверку регистрации/флагов ConCommand и маршрута `ClientCmd → server ConCommand`.