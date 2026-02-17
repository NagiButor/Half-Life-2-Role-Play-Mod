## 1) Камера двигается 2 раза
Причина: каждый `dialog_open_payload` вызывает `BeginDialog()`, который сбрасывает состояние автонаводки; затем `SetPayloadFlags(auto_face_npc=1)` снова стартует автонаводку на следующей реплике.

### Исправление
- В `CVGuiDialogPanel` добавить флаг сессии `m_autoFaceConsumed`.
- В `BeginDialog()`:
  - если панель уже открыта и `entityName` тот же — это продолжение; не сбрасывать `m_autoFaceConsumed` и не сбрасывать параметры автонаводки.
  - если новый диалог (панель была скрыта или другая entity) — `m_autoFaceConsumed=false`.
- В `SetPayloadFlags()` запускать автонаводку только если `autoFaceNPC && !m_autoFaceConsumed`, затем `m_autoFaceConsumed=true`.

Результат: автонаводка выполняется **строго один раз** в начале диалога.

## 2) Убрать блокиратор урона по союзникам, но NPC НЕ агрессивны
Блокировка урона проверяет отношение **атакующего к жертве** (`attacker->IRelationType(victim) == D_LI`). Нужно изменить только отношение игрока к NPC, не трогая обратное.

### Исправление
- В `CHalfLife2::InitDefaultAIRelationships()` добавить цикл:
  - для каждого `i` (кроме `CLASS_NONE` и `CLASS_PLAYER`) сделать:
    - `SetDefaultRelationship(CLASS_PLAYER, (Class_T)i, D_HT, 0)`
  - НЕ менять `(Class_T)i -> CLASS_PLAYER`.

Результат:
- Игрок наносит урон любым NPC без `ai_relationship`.
- NPC по умолчанию остаются неагрессивными к игроку.

## 3) Настраиваемый Health для любого NPC (чтобы Spawn не перетирал)
Проблема: многие NPC в `Spawn()` выставляют дефолтное здоровье из `sk_*` и перетирают `health`, заданный в Hammer.

### Исправление
- В `CAI_BaseNPC` добавить поле `m_iMapHealthOverride` (0 по умолчанию).
- В `CAI_BaseNPC::KeyValue()`:
  - если ключ `health` и значение > 0 — сохранить в `m_iMapHealthOverride` (и дать базовой обработке продолжить работу).
- В `CAI_BaseNPC::NPCInit()` перед `m_iMaxHealth = m_iHealth`:
  - если `m_iMapHealthOverride > 0` — установить `m_iHealth = m_iMapHealthOverride`.

Результат: `health` из Hammer реально работает на любом NPC.

## 4) Подсказка в Hammer: дефолтный Health по уровням сложности
В HL2 (см. `CGameRules::RefreshSkillData`) по умолчанию загружается один `skill.cfg`, а EASY/HARD синтезируются (т.е. базовые `sk_*_health` обычно одинаковы по сложности).

### Реализация подсказки
- В `hl2rpm.fgd` для keyvalue `health` у NPC (или у базового NPC-класса, если так устроено наследование FGD) добавить текст-подсказку вида:
  - "Default (Easy/Normal/Hard): <значение> / <значение> / <значение> (по умолчанию HL2 одинаково; задайте health чтобы переопределить)".
- Для ключевых NPC (citizen, barney, metropolice, combine_s, vortigaunt, zombie и т.д.) подтянуть значение из `hl2/cfg/skill.cfg` по соответствующему `sk_*_health`.

## Файлы
- Клиент: `vgui_dialogpanel.h/.cpp`
- Shared: `shared/hl2/hl2_gamerules.cpp`
- Сервер: `ai_basenpc.cpp` (KeyValue + NPCInit), `ai_basenpc.h`
- Hammer: `hl2rpm.fgd`

## Проверка
- Камера: нет повторного довода на 2-й реплике.
- Урон: игрок наносит урон ally NPC без агра NPC.
- Health: заданный `health` в Hammer применяется.
- FGD: в Hammer видно дефолтный Health в подсказке.