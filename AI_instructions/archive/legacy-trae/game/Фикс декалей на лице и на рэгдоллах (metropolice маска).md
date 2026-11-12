## Что выяснилось по коду (почему так происходит)
- Для ударов/выстрелов по NPC декаль на модели ставится через `UTIL_ImpactTrace → pEnt->ImpactTrace()` (например, у дубинок это идёт после нанесения урона) [basebludgeonweapon.cpp:L163-L214](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/basebludgeonweapon.cpp#L163-L214) и [util.cpp:L1484-L1496](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/util.cpp#L1484-L1496).
- У NPC есть флаг `m_fNoDamageDecal`, который при некоторых попаданиях заставляет `CAI_BaseNPC::ImpactTrace/DecalTrace` сделать ранний `return` [ai_basenpc.cpp:L1489-L1512](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/ai_basenpc.cpp#L1489-L1512).
- Для хедшотов по живому NPC Valve исторически выставляли этот флаг (“не декалить лицо пока жив”), из-за чего у части NPC на лице декаль не появляется.
- С `npc_metropolice` отдельный случай: когда он мёртв и это уже рэгдолл, трейсы/импакты берут **материал из физики** (surfaceprop), и у маски он часто получается как пластик → звук «пластик» и подбираются «не кровавые»/неподходящие декали. Это не «ошибка» одного места, это следствие того, что у рэгдолла surfaceprop может отличаться от живой модели.

## План исправления
1. Сделать «разрешение декалей на лице» железобетонным:
   - Оставить текущую ConVar `ai_allow_face_decals_while_alive`.
   - Дополнительно поменять `CAI_BaseNPC::ImpactTrace` и `CAI_BaseNPC::DecalTrace`: если `m_fNoDamageDecal` выставлен, но ConVar включена и в трейсе `hitgroup == HITGROUP_HEAD`, **не подавлять** декаль/импакт.
   - Это накроет случаи, когда флаг выставился не только в `TraceAttack`, и объясняет почему у тебя “не работает” даже после предыдущей правки.

2. Починить «метрополис: маска на рэгдолле не кровавится + звук пластик» на уровне импактов:
   - В серверном `UTIL_ImpactTrace` добавить проверку: если цель — рэгдолл (`CBaseAnimating::IsRagdoll()`), и у него среди physics-частей вообще есть “flesh/alienflesh” (можно определить через обход `VPhysicsGetObjectList()` и `physprops->GetSurfaceData(material)->game.material` как это уже делается в `CRagdollProp::VPhysicsGetFlesh()` [physics_prop_ragdoll.cpp:L1236-L1249](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/physics_prop_ragdoll.cpp#L1236-L1249)), то перед `pEntity->ImpactTrace(...)` принудительно подменять `pTrace->surface.surfaceProps` на `physprops->GetSurfaceIndex("flesh")`.
   - Результат: удары/попадания по любым частям **человеческого рэгдолла** будут звучать и декалиться как по плоти (и на маске тоже появится «кровавый» импакт), вместо пластика.

3. Сборка и проверка в игре
   - Собрать server DLL.
   - Проверить 2 кейса:
     - `npc_citizen`: удар/выстрел в голову без убийства → декаль на лице должна появляться.
     - `npc_metropolice`: после смерти бить по маске → звук должен стать “по плоти”, и декали должны появляться на маске.

## Важное ограничение
- Если конкретный материал модели реально помечен как “не принимать декали” (аналог `$nodecal`), кодом это не перебить. Но поведение metropolice (есть при жизни, нет на рэгдолле) больше похоже именно на «surfaceprop из физики», а не на запрет декалей в VMT.