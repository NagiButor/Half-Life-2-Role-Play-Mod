## Почему слышно wpn_denyselect
- В `CHL2_Player::PlayerUse()` если `FindUseEntity()` ничего не нашёл, при нажатии `E` ставится флаг `m_bPlayUseDenySound = true`, и позже в `ItemPostFrame()` проигрывается `EmitSound("HL2Player.UseDeny")` (это и есть `common/wpn_denyselect.wav`).
- Loot container сейчас открывается отдельным Think’ом триггера, поэтому для `PlayerUse()` это выглядит как «ничего не использовал» → включается deny.

## План исправления
### 1) Перенести открытие loot container в PlayerUse (чтобы это считалось USE)
- В `server/hl2/hl2_player.cpp` в `CHL2_Player::PlayerUse()` в ветке, где `pUseEntity == NULL` и нажали `IN_USE`, добавлю проверку: если игрок **касается** `func_loot_container` (через `IsTouching(this)`), то:
  - вызвать `EmitSound("HL2Player.Use")` (обычный “select/use” звук; в HL2 это как раз `common/wpn_select.wav`),
  - вызвать `OpenLootForPlayer(this, pContainer)`,
  - отметить как `usedSomething=true` и подставить `pUseEntity = pContainer`, чтобы сработал debounce и не включился deny.

### 2) Отключить открытие через Think в loot_container.cpp
- Чтобы не было дублей/гонок, уберу из `loot_container.cpp` опрос `IN_USE` в `LootThink()` (либо вообще уберу Think) — контейнер останется триггером с данными лута, а открытие будет идти через `PlayerUse()`.

## Проверка
- После сборки: при нажатии `E` внутри loot container проигрывается “select” (`HL2Player.Use`/`common/wpn_select.wav`), а deny больше не звучит.