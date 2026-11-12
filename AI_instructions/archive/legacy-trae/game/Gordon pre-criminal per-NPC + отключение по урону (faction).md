## Что именно добавлю
Чтобы это работало «каждому NPC» и при этом не ломало `vvis` (лимиты токенов), сделаю **короткие keyvalue** и **локальный override**, который дополняет глобальный `gordon_precriminal`.

### Новые keyvalue (на всех npc_*)
- `gpc` (choices):
  - `0` = Default (использовать глобальный `gordon_precriminal`)
  - `1` = ON (для этого NPC игрок считается pre-criminal)
  - `2` = OFF (для этого NPC игрок считается criminal)
- `gpc_off_dmg` (0/1): если включено — **при уроне от игрока** этому NPC автоматически ставится `gpc=OFF`.
- `gpc_off_f_dmg` (0/1): если включено и задан `ftag` — при уроне от игрока этому NPC ставится `gpc=OFF` **у всех NPC с тем же `ftag`**.

(Все имена короткие, чтобы vvis не ругался на `token too long`.)

## Реализация в коде
### 1) CAI_BaseNPC: хранение override + API
- Добавлю в `CAI_BaseNPC` поле `m_iGordonPrecriminalOverride` (int: 0/1/2) и два bool-флага `m_bGpcOffOnPlayerDamage`, `m_bGpcOffFactionOnPlayerDamage` + «одноразовые» триггеры.
- Добавлю метод:
  - `bool IsGordonPrecriminalForMe() const;`
    - если `gpc==ON` → true
    - если `gpc==OFF` → false
    - иначе → `GlobalEntity_GetState("gordon_precriminal") == GLOBAL_ON`

### 2) Отключение по урону
- В `CAI_BaseNPC::OnTakeDamage_Alive()` в ветке `attacker->IsPlayer()`:
  - если `gpc_off_dmg` → поставить **этому** NPC `gpc=OFF`
  - если `gpc_off_f_dmg` и `ftag` задан → пробежать по всем NPC и поставить `gpc=OFF` тем, у кого такой же `ftag`.

### 3) Подключить override к тем NPC, которые сейчас смотрят только глобал
Заменю проверки `GlobalEntity_GetState("gordon_precriminal")` на `IsGordonPrecriminalForMe()` в местах:
- [npc_metropolice.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/hl2/npc_metropolice.cpp) (несколько проверок)
- [npc_citizen17.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/hl2/npc_citizen17.cpp) (Classify/weapon search)
- [npc_playercompanion.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/hl2/npc_playercompanion.cpp)
- [npc_scanner.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/hl2/npc_scanner.cpp)
- [npc_spotlight.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/hl2/npc_spotlight.cpp)
- [npc_vehicledriver.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/npc_vehicledriver.cpp)

Так override начнёт реально влиять на поведение тех NPC, которые раньше зависели только от глобального состояния.

## FGD
- Добавлю `gpc`, `gpc_off_dmg`, `gpc_off_f_dmg` в `BaseNPC` в [base_vizzys.fgd](file:///e:/XBLAH%27s%20modding%20tool/tools/FGDs/Mapbase/base_vizzys.fgd).

## Проверка
- Проверю, что в изменённых файлах нет диагностик.
- После сборки у тебя локально:
  - Поставить 2 NPC с одним `ftag`.
  - Включить `gpc=ON`, `gpc_off_f_dmg=Yes`.
  - Ударить одного → оба должны начать считать игрока criminal (т.е. поведение как при `gordon_precriminal OFF`).