## Почему сейчас не работает
- Оторванные части тела антлиона при разрыве создаются как `prop_physics` (класс `CBreakableProp`) через [BreakModelCreate_Prop](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/props.cpp#L3884-L3945), а не как `prop_ragdoll`.
- ConVar’ы `ragdoll_corpse_blood_splashes` / `ragdoll_corpse_blood_wall_decals` сейчас обрабатываются только в `CRagdollProp::TraceAttack`, поэтому на `prop_physics`-кусках эффектов нет.

## План
### 1) Сделать поддержку этих ConVar для кусков `prop_physics` (CBreakableProp)
- Добавить в `CBreakableProp` поле `m_iCorpseGibBloodColor` (по умолчанию `DONT_BLEED`) и сохранить его в datadesc.
- Добавить метод-сеттер `SetCorpseGibBloodColor(int)`.
- Переопределить `CBreakableProp::TraceAttack(...)` и, если `m_iCorpseGibBloodColor != DONT_BLEED/MECH`:
  - при `ragdoll_corpse_blood_splashes=1` делать `SpawnBlood(...)`;
  - при `ragdoll_corpse_blood_wall_decals=1` делать трассы и `UTIL_BloodDecalTrace(...)` (логика как у ragdoll).
- Для доступа к ConVar’ам в `props.cpp` добавить `extern ConVar ragdoll_corpse_blood_splashes;` и `extern ConVar ragdoll_corpse_blood_wall_decals;`.

### 2) Проставлять цвет крови этим кускам при создании
- В [BreakModelCreate_Prop](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/props.cpp#L3884-L3945) после `CreateNoSpawn(...)`:
  - если `pOwner` является `CBaseCombatCharacter` и его `BloodColor()` не `DONT_BLEED/MECH` — вызвать `pEntity->SetCorpseGibBloodColor( pOwner->BloodColor() )`.
- Это сделает кровь корректного цвета (для антлиона/зомби/хедкраба и т.д.) на оторванных частях.

### 3) (Дополнительно, но в тему) Проставить blood color для ragdoll-субмоделей
- В [CreateServerRagdollSubmodel](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/physics_prop_ragdoll.cpp#L1536-L1566) копировать `BloodColor()` из `pOwner` в `pRagdoll->SetRagdollBloodColor(...)`.

### 4) Сборка
- Пересобрать `server_episodic` (Release|Win32) и убедиться, что `server.dll` опубликован в `hl2rpm/bin`.

## Проверка
- Взорвать антлиона → по оторванным кускам стрелять/бить: появляются сплэши и/или кровь на стенах по ConVar.
- Отключение:
  - `ragdoll_corpse_blood_splashes 0`
  - `ragdoll_corpse_blood_wall_decals 0`