## Почему раньше не проставил числа
- В рабочих директориях проекта у меня не было доступного `skill.cfg`, поэтому я мог только сделать общую подсказку “дефолт берётся из skill/кода”, без конкретных X.
- Сейчас ты прислал значения — можно честно и точно вшить их в описания в FGD.

## Что сделаю
### 1) Соберу таблицу соответствий `npc_*` → `sk_*_health`
- По твоему куску `skill.cfg` сопоставлю:
  - `npc_barnacle` → `sk_barnacle_health` = 35
  - `npc_barney` → `sk_barney_health` = 35
  - `npc_bullseye` → `sk_bullseye_health` = 35
  - `npc_citizen` → `sk_citizen_health` = 40
  - `npc_combine_s` → `sk_combine_s_health` = 50
  - `npc_strider` → `sk_strider_health` = 350
  - `npc_headcrab` → `sk_headcrab_health` = 10
  - `npc_headcrab_fast` → `sk_headcrab_fast_health` = 10
  - `npc_manhack` → `sk_manhack_health` = 25
  - `npc_metropolice` → `sk_metropolice_health` = 40
  - `npc_cscanner` (City Scanner) → `sk_scanner_health` = 30
  - `npc_stalker` → `sk_stalker_health` = 50
  - `npc_vortigaunt` → `sk_vortigaunt_health` = 100
  - `npc_zombie` → `sk_zombie_health` = 50
  - `npc_poisonzombie` → `sk_zombie_poison_health` = 175
  - `npc_antlion` → `sk_antlion_health` = 30
  - `npc_antlionguard` → `sk_antlionguard_health` = 500
  - `npc_ichthyosaur` → `sk_ichthyosaur_health` = 200
- То, чего нет в FGD (например `npc_combine_guard`, `npc_headcrab_poison`) — не трогаю, потому что в FGD таких классов не найдено.

### 2) Обновлю подсказки `health` в FGD для этих NPC
- В [halflife2_vizzys.fgd](file:///e:/XBLAH's%20modding%20tool/tools/FGDs/Mapbase/halflife2_vizzys.fgd) в каждом соответствующем `@NPCClass ... = npc_...` добавлю/переопределю строку:
  - `health(integer) : "Health" : 0 : "0 = default. Default (Easy/Normal/Hard): X / X / X (sk_..._health)"`
- Для `npc_zombie_custom`/`npc_zombie_custom_torso` обновлю уже существующую подсказку, чтобы она тоже показывала `50 / 50 / 50`.

### 3) Минимальная правка “для всех остальных”
- В `BaseNPC.health` (в [base_vizzys.fgd](file:///e:/XBLAH's%20modding%20tool/tools/FGDs/Mapbase/base_vizzys.fgd)) оставлю общую подсказку, но уточню, что конкретные значения смотри в описании конкретного NPC (там где оно добавлено).

## Верификация
- Сделаю .bak-копию `halflife2_vizzys.fgd` перед правками.
- Проверю, что FGD синтаксически корректен (нет дублей/поломанных блоков), и что строки `health(...)` появились в нужных `npc_*`.
- Прогоню диагностику редактора.

Если ок — приступаю к внесению правок в FGD прямо по этой таблице.