## Что происходит сейчас (причина)
- В [ai_basenpc.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/ai_basenpc.cpp) у `CAI_BaseNPC` есть флаг `m_fNoDamageDecal`, который принудительно блокирует `DecalTrace` и `ImpactTrace`.
- Этот флаг выставляется:
  - при попадании в голову по живому NPC (специально «не декалить лицо пока жив») [ai_basenpc.cpp:L1627-L1631](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/ai_basenpc.cpp#L1627-L1631)
  - при блокировке урона фильтром/дружественным огнём (типично для `npc_citizen`, `npc_barney`) [ai_basenpc.cpp:L1051-L1116](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/ai_basenpc.cpp#L1051-L1116) и [ai_basenpc.cpp:L1612-L1637](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/ai_basenpc.cpp#L1612-L1637)
- Из‑за этого «Impact» эффект/декали могут вообще не отправляться, поэтому кровь не остаётся ни на модели, ни на рэгдолле (особенно на убийственном выстреле/хедшоте).

## План исправления
1. Убрать/сделать отключаемым подавление декалей через `m_fNoDamageDecal` для всех NPC:
   - В `CAI_BaseNPC::DecalTrace` и `CAI_BaseNPC::ImpactTrace` перестать делать ранний `return` (или обернуть в ConVar, но по умолчанию включить декали).
2. Разрешить декали на голове/лице:
   - В `CAI_BaseNPC::TraceAttack` удалить/отключить блок, который выставляет `m_fNoDamageDecal` при `HITGROUP_HEAD` и NPC ещё жив.
3. Разрешить декали даже когда урон блокируется (friendly fire / damage filter / запрет крови):
   - В `CAI_BaseNPC::PassesDamageFilter` убрать выставление `m_fNoDamageDecal = true` в ветках, где урон возвращается `false`.
   - В `TraceAttack` в ветке `!bBloodAllowed` не глушить декали.
4. Сохранить управляемость через ConVar (опционально, но рекомендую):
   - Добавить, например, `ai_npc_always_allow_damage_decals 1` (по умолчанию 1 для твоего запроса). При 0 поведение вернётся к ванильному.
5. Проверка после сборки:
   - На карте с `npc_citizen`/`npc_barney` проверить попадания по корпусу/конечностям/лицу: декали должны оставаться на модели.
   - Убить NPC и стрелять по рэгдоллу: декали должны появляться на рэгдолле тоже.

## Ограничения, которые стоит знать
- Если конкретные материалы модели помечены `$nodecal`, никакой код не заставит их принимать декали — тогда потребуется правка материалов моделей. Но для стандартных human NPC обычно проблема именно в `m_fNoDamageDecal`.
