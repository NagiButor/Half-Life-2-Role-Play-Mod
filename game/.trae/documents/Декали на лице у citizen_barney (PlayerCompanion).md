## Почему citizen/barney не декалятся по лицу
- `npc_citizen` и `npc_barney` наследуются от `CNPC_PlayerCompanion`.
- У `CNPC_PlayerCompanion` есть жёсткий оверрайд `DecalTrace`, который **всегда запрещает декали по голове/лицу**:
  - [npc_playercompanion.cpp:L2754-L2761](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/hl2/npc_playercompanion.cpp#L2754-L2761)
  - там прямое `if( pTrace->hitgroup == HITGROUP_HEAD ) return;`
- Поэтому любые декали, которые идут через `DecalTrace` (в т.ч. часть «кровавых»/уронных декалей в некоторых ситуациях), на голове citizen/barney **не появятся никогда**, даже если мы правили базовый `CAI_BaseNPC`.

## Почему metropolice починился «как рэгдолл»
- У metropolice проблема после смерти была в surfaceprop/материале удара по vphysics (получался «пластик»), из-за этого и звук/декаль были не те. Это уже исправлено отдельной логикой.

## План исправления citizen/barney (и живые, и мёртвые)
1. Убрать жёсткий запрет на head decals у `CNPC_PlayerCompanion`:
   - Изменить `CNPC_PlayerCompanion::DecalTrace` так, чтобы он **не возвращал** на `HITGROUP_HEAD`, когда включено `ai_allow_face_decals_while_alive` (используем уже существующую ConVar).
   - Иначе (когда ConVar=0) оставить ванильное поведение.

2. Если после шага (1) на рэгдоллах citizen всё ещё нет декалей на голове:
   - Расширить логику «как у metropolice» для рэгдоллов людей: в `UTIL_ImpactTrace` принудительно считать surfaceprop “flesh” для рэгдоллов, чьи модели относятся к humans (например, по имени модели `models/humans/...`).
   - Это нужно на случай, если у некоторых human-ragdoll vphysics-частей вообще нет `CHAR_TEX_FLESH` и наша текущая проверка “есть ли flesh-часть” не срабатывает.

3. Сборка и проверка
   - Пересобрать server DLL.
   - Проверить:
     - `npc_citizen` / `npc_barney`: попадание/удар в голову пока жив → декаль на лице должна появляться.
     - Убийство и удары/попадания по голове рэгдолла → декали должны появляться и там.
