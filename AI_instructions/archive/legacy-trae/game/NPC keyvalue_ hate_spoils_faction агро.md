## Требования (как я понял)
- У каждого NPC должны быть настраиваемые keyvalue (Да/Нет):
  1) Если игрок наносит урон NPC → NPC начинает ненавидеть игрока и атакует.
  2) Если игрок наносит урон NPC → NPC помечается spoiled и с ним нельзя больше говорить.
  3) Если игрок напал на одного NPC → другие NPC той же «фракции» тоже spoiled и тоже hate к игроку.
- Плюс должны быть inputs, чтобы это можно было дергать через I/O outputs (вручную портить/агрить других NPC).

## Что уже есть в коде (опорные точки)
- Диалоговая «порча» уже реализована: `DialogDefinitions::MarkEntitySpoiled(entityName)` и `SendDialogToPlayer()` не открывает диалог для spoiled. Это в [dialog_definitions.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/dialog_definitions.cpp).
- У каждого NPC есть гарантированная точка “меня ударил игрок”: `CAI_BaseNPC::OnTakeDamage_Alive()` и outputs `OnDamagedByPlayer/OnDamagedByPlayerSquad` в [ai_basenpc.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/ai_basenpc.cpp).

## Дизайн
### Новые keyvalue (всем NPC)
Добавлю в `CAI_BaseNPC` и в FGD (в `BaseNPC`) такие поля:
- `hate_player_on_player_damage` (Да/Нет)
- `spoil_dialog_on_player_damage` (Да/Нет)
- `faction_tag` (string) — идентификатор фракции/группы (например `combine`, `citizen`, `bandits`).
- `propagate_hate_to_faction_on_player_damage` (Да/Нет)
- `propagate_spoil_to_faction_on_player_damage` (Да/Нет)
- Внутренний флаг “сработало уже” (без keyvalue), чтобы не спамить триггерами на каждой пуле.

### Новые inputs/outputs (для Hammer I/O)
На всех NPC:
- Inputs:
  - `HatePlayer` (void/bool): делает NPC `D_HT` к игроку и старается назначить игрока врагом.
  - `SpoilDialog` (void/bool): вызывает `MarkEntitySpoiled` для ключа диалога.
  - `HateFaction` (string, optional): делает hate к игроку всем NPC с таким `faction_tag` (если параметр пустой — использует `faction_tag` этого NPC).
  - `SpoilFaction` (string, optional): spoil всем NPC с таким `faction_tag` (аналогично).
  - `ClearSpoilDialog` (void): снять spoiled (для отладки/квестов).
- Outputs (чтобы удобно цеплять логику):
  - `OnHatedPlayer` (fires once)
  - `OnDialogSpoiled` (fires once)
  - `OnFactionHatedPlayer` / `OnFactionDialogSpoiled` (optional, тоже once) — если включена пропагация.

### “Ключ диалога”
Сейчас диалоги и spoiled привязаны к `entityName` (targetname NPC), см. [dialogsystem_server.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/dialogsystem_server.cpp#L21-L47).
- В реализации буду использовать **targetname** как ключ (если пустой — spoil не применяется, это ожидаемо).
- Опционально добавлю отдельный keyvalue `dialog_key` позже, но сейчас не буду расширять область задачи.

## Реализация (файлы и шаги)
1) **Сервер: логика hate/spoils/faction**
- Внести поля/Datadesc в:
  - [ai_basenpc.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/ai_basenpc.h)
  - [ai_basenpc.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/ai_basenpc.cpp)
- В `CAI_BaseNPC::OnTakeDamage_Alive()`:
  - если `attacker->IsPlayer()` и ещё не триггерилось:
    - если `hate_player_on_player_damage` → вызвать `HatePlayer`.
    - если `spoil_dialog_on_player_damage` → вызвать `SpoilDialog`.
    - если включена пропагация и `faction_tag` не пустой → пробежать по всем `CAI_BaseNPC` и применить к тем, у кого такой же `faction_tag`.
- Реализация “сделать агро”:
  - `AddEntityRelationship(pPlayer, D_HT, 99)`
  - `SetEnemy(pPlayer)` + `UpdateEnemyMemory(pPlayer, pPlayer->GetAbsOrigin())` + пробуждение AI (через существующие методы CAI_BaseNPC)

2) **FGD: добавить keyvalue/inputs/outputs в BaseNPC**
- Обновить [base_vizzys.fgd](file:///e:/XBLAH%27s%20modding%20tool/tools/FGDs/Mapbase/base_vizzys.fgd) в блоке `@BaseClass ... = BaseNPC`:
  - keyvalue: `hate_player_on_player_damage`, `spoil_dialog_on_player_damage`, `faction_tag`, `propagate_*`.
  - inputs/outputs из секции выше.

3) **FGD мода (если нужно)**
- Если в [hl2rpm.fgd](file:///e:/XBLAH%27s%20modding%20tool/tools/FGDs/Mapbase/hl2rpm.fgd) есть собственные базовые NPC-классы (например BaseCombine), продублирую keyvalue/inputs там тоже, чтобы они точно отображались при загрузке только hl2rpm.fgd.

## Проверка
- Быстро соберу проект (server dll) и проверю, что нет ошибок компиляции.
- В игре/консоли:
  - Поставить 2 NPC с одинаковым `faction_tag`.
  - Включить `spoil_dialog_on_player_damage=Yes` и `propagate_spoil...=Yes`.
  - Открыть диалог → нанести урон → убедиться, что:
    - текущий NPC и остальные той же фракции больше не открывают диалог,
    - при `hate_player_on_player_damage=Yes` они переходят в агро на игрока.

Если ок — начну внедрять прямо по этому плану.