## Почему сейчас работает только citizen
- Вызов `E` по энтити происходит только если `ObjectCaps()` содержит `FCAP_IMPULSE_USE`.
- У `CAI_BaseNPC` есть хелпер `UsableNPCObjectCaps(...)`, который добавляет `FCAP_IMPULSE_USE` для живых NPC, но базовый `ObjectCaps()` его **не использует** (см. [ai_basenpc.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/ai_basenpc.h#L2268-L2277)). Многие NPC (alyx/breen и т.п.) в итоге не считаются usable, поэтому `E` до `Use()` не доходит.

## Что сделаю
## 1) Включить FCAP_IMPULSE_USE для всех живых NPC
- Изменить `CAI_BaseNPC::ObjectCaps()` так, чтобы он возвращал `UsableNPCObjectCaps(BaseClass::ObjectCaps() | FCAP_NOTIFY_ON_TRANSITION)`.
- Это сделает **любой живой NPC** доступным для `E`, а дальше уже твой хук в [baseentity.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/baseentity.cpp#L2845-L2861) будет стартовать диалог, если у NPC есть `targetname` и найден диалог.

## 2) Проверка
- Протестировать `npc_breen`, `npc_alyx`, `npc_citizen`: при нажатии `E` должен вызываться `Use`.
- Проверить, что диалог стартует при наличии `targetname` + файла в `scripts/dialogs`.

## Побочный эффект (ожидаемый)
- `E` будет “прилипать” ко всем NPC (включая врагов), т.к. они станут usable. Если позже понадобится фильтр (например, только при наличии диалога) — добавим отдельную проверку в `PlayerUse` или добавим spawnflag для отключения use у конкретных NPC.