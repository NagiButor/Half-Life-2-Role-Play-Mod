## Диагноз
### 1) Куски антлиона после разрыва исчезают
- Это не `CGib`, а break-куски, которые antlion создаёт через `PropBreakableCreateAll(...)` в [npc_antlion.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/hl2/npc_antlion.cpp#L4089-L4122).
- Эти куски получают `fadeTime` из break-данных модели и на сервере фейдятся/удаляются через `SUB_StartFadeOut(fadeTime)` в `BreakModelCreateSingle` (см. отчёт поиска).

### 2) Кровь по трупам как по живым
- После смерти NPC у вас в основном остаются server-side `prop_ragdoll` (уже включено `ragdoll_force_server_on_npc_death=1`), значит можно добавить поведение прямо в `CRagdollProp`.
- Нужно 2 конвара (оба по умолчанию = 1):
  - включать «сплэши крови» на месте попадания по трупу;
  - включать «кровь на стенах» (wall decals) при попадании по трупу.

## План правок
### A) Антлион: не фейдить разорванные куски
- В `CNPC_Antlion::CorpseGib()` заменить `PropBreakableCreateAll(...)` на ручной путь:
  - `CUtlVector<breakmodel_t> list; BreakModelList(list, GetModelIndex(), params.defBurstScale, params.defCollisionGroup);`
  - для каждого элемента `list[i].fadeTime = 0.0f;`
  - затем вызвать `CreateGibsFromList(list, GetModelIndex(), NULL, params, this, -1, true, true);`
- Это сохраняет текущую механику разлёта, но выключает auto-fade именно для кусков антлиона.

### B) Два ConVar для крови по трупам (по умолчанию включено)
- Добавить ConVar’ы (server):
  - `ragdoll_corpse_blood_splashes` = 1
  - `ragdoll_corpse_blood_wall_decals` = 1
- В `CRagdollProp::TraceAttack(...)` (или рядом) добавить:
  - если включён `ragdoll_corpse_blood_splashes` и `m_iBloodColor` не `DONT_BLEED/MECH` и урон не `DMG_SHOCK` → вызвать `SpawnBlood(ptr->endpos, dir, m_iBloodColor, damage)`;
  - если включён `ragdoll_corpse_blood_wall_decals` → выполнить trace как в `TraceBleed` (несколько лучей с шумом) и делать `UTIL_BloodDecalTrace(&Bloodtr, m_iBloodColor)`.
- Поскольку у `prop_ragdoll` нет корректного `BloodColor()`, будем использовать уже сохранённый `m_iBloodColor`.

### C) Сборка/проверка
- Пересобрать `server_episodic` (Release|Win32) и убедиться, что `server.dll` опубликован в `hl2rpm/bin`.
- Быстрые проверки в игре:
  - взорвать антлиона → части тела остаются навсегда (не исчезают по таймеру);
  - убить любого NPC → стрелять/бить труп → появляются кровавые сплэши на месте попадания и кроводекали на стенах.

Приступаю к правкам и сборке.