## Что именно ломает декали на лице
- В `CAI_BaseNPC::TraceAttack` есть правило E3: если попадание в `HITGROUP_HEAD` и NPC **остаётся жив**, ставится `m_fNoDamageDecal = true` [ai_basenpc.cpp:L1627-L1631](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/ai_basenpc.cpp#L1627-L1631).
- Затем при попытке нарисовать импакт/декаль по этому же трейсу `CAI_BaseNPC::ImpactTrace`/`DecalTrace` видят `m_fNoDamageDecal` и делают ранний `return` [ai_basenpc.cpp:L1489-L1512](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/ai_basenpc.cpp#L1489-L1512).
- Поэтому:
  - по телу у `npc_citizen` декали есть (там флаг не выставляется),
  - по голове/лицу **при неубийственном хедшоте** декалей нет.
- Почему у `npc_breen` «работает» чаще: у него мало HP (`m_iHealth = 8`) [npc_breen.cpp:L83-L115](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/hl2/npc_breen.cpp#L83-L115), поэтому выстрел в голову чаще убивает, условие `m_iHealth - subInfo.GetDamage() > 0` становится ложным и подавление не включается.

## План исправления (ровно под твою задачу)
1. Ввести серверную ConVar (для отката/тонкой настройки), например `ai_allow_face_decals_while_alive` (по умолчанию `1`).
2. В `CAI_BaseNPC::TraceAttack` изменить логику так, чтобы **не выставлять** `m_fNoDamageDecal` для `HITGROUP_HEAD` когда ConVar включена.
   - Это даст декали на лице у `npc_citizen`, `npc_barney` и любых других NPC при любых выстрелах в голову.
3. Ничего не трогать в клиентском рэгдолльном коде: Mapbase уже умеет переносить убийственный импакт на рэгдолл в тот же тик (`g_ragdoll_steal_impacts_*`) и наносить декали по рэгдоллу при попаданиях (`g_ragdoll_client_impact_decals`) [fx_impact.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/fx_impact.cpp).
   - После шага (2) “убийственный” и “неубийственный” хедшоты будут одинаково давать декаль на голове; значит и на рэгдолле это перестанет «пропадать».
4. Сборка и проверка:
   - Стрельба по `npc_citizen` в голову из слабого оружия (чтобы не убивать с первого) — декаль должна появляться прямо на лице.
   - Убийство хедшотом и проверка рэгдолла — декаль от убийственного выстрела должна оказаться на голове рэгдолла, и последующие попадания по голове рэгдолла тоже должны декалиться.

## Если после этого у конкретной модели всё равно нет декалей на лице
- Тогда это уже не код, а материалы/модель (например, материал головы с флагом `no decals`). В твоих материалах мода `$nodecal` не найден, значит чаще всего проблема именно в `m_fNoDamageDecal`, и фикс выше её закрывает.