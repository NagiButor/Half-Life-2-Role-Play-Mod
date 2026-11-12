## Что происходит
- Декали на теле живой Alyx сейчас режутся флагом `m_fNoDamageDecal = true;`, который Alyx ставит в своём `TraceAttack()`. Из-за `ai_allow_face_decals_while_alive=1` голова обходит этот запрет, поэтому «на лице есть, на теле нет».
  - См. [npc_alyx.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/hl2/npc_alyx.cpp#L254-L260) и [npc_alyx_episodic.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/hl2/npc_alyx_episodic.cpp#L2413-L2419)

## План правок
### 1) Сделать `cl_clear_nodecal` включённым по умолчанию
- Поменять дефолтное значение ConVar `cl_clear_nodecal` с `"0"` на `"1"` в client.dll, оставив callback/auto-apply.
- Это сделает снятие `SUPPRESS_DECALS` активным сразу без необходимости вручную вводить команду.

### 2) Исправить декали на теле живой Alyx
- Добавить ConVar `npc_alyx_suppress_damage_decals` (дефолт `0`).
- В `CNPC_Alyx::TraceAttack()`:
  - вызывать `BaseClass::TraceAttack(...)` как сейчас;
  - выставлять `m_fNoDamageDecal = true` **только если** `npc_alyx_suppress_damage_decals=1`.
- Так живой Alyx будет получать декали на теле (как труп), а при желании можно вернуть ванильное поведение.
- Применить одинаково в `npc_alyx.cpp` и `npc_alyx_episodic.cpp`.

### 3) Сборка
- Пересобрать `client_episodic` и `server_episodic` (Release|Win32) и убедиться, что `client.dll/server.dll` опубликованы в `hl2rpm/bin`.

## Как проверить после сборки
- Без ввода команд декали на моделях должны работать (cl_clear_nodecal уже 1).
- По живой Alyx: попадания должны оставлять декали на теле.
- Для отката: `npc_alyx_suppress_damage_decals 1` вернёт старое поведение (только лицо по bypass).