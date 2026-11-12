## Что именно будет сделано
- Расширю подсказку для keyvalue `health` в Hammer **для всех NPC, которые описаны в FGD**, чтобы в описании было: `Default (Easy/Normal/Hard): X / X / X`.
- Для NPC где `health` уже есть в FGD (например, turret/zombie_custom) — обновлю описание, чтобы оно показывало дефолтные значения.

## Откуда возьмутся дефолтные значения
- **Skill-значения**: возьму `sk_*_health` из `hl2/cfg/skill.cfg` (в HL2 загружается через `skill_manifest.cfg`).
- **Hardcoded/фоллбек**: если соответствующий `sk_*_health` в `skill.cfg` не задан (или равен 0 и код подставляет константу), возьму дефолт из `Spawn()` конкретного NPC (типичный паттерн: `m_iHealth = sk_...; if (!m_iHealth) m_iHealth = <fallback>` или просто `m_iHealth = <число>`).
- **Сложности**: в HL2 по умолчанию используется один `skill.cfg` и EASY/HARD «синтезируются», поэтому чаще всего X одинаковое для всех трёх — но я всё равно выведу `Easy/Normal/Hard` в подсказке.

## Какие FGD файлы будут обновлены
- `base_vizzys.fgd`: оставить/допилить общий keyvalue `health` в `BaseNPC` (чтобы был у всех NPC).
- `halflife2_vizzys.fgd`: пройтись по **каждому** `@NPCClass`/NPC-точке с `base(...BaseNPC...)` и добавить/обновить строку:
  - `health(integer) : "Health" : 0 : "... Default (Easy/Normal/Hard): X / X / X (source)"`
- `hl2rpm.fgd`: если в нём есть собственные NPC-классы (или Base-классы для NPC) — тоже добавлю/уточню `health` и подсказку.

## Как я покрою «вообще на всех»
- Составлю список всех `npc_*` из FGD (по строкам вида `= npc_... :`).
- Для каждого найду источник дефолтного HP:
  - приоритет 1: `sk_*_health` в `skill.cfg`
  - приоритет 2: hardcoded/фоллбек в исходниках NPC
- Затем обновлю FGD так, чтобы каждый NPC в Hammer показывал подсказку с числом.

## Проверка
- Быстрая проверка, что в FGD нет синтаксических ошибок и повторяющихся keyvalue-строк в конфликтном виде.
- Точечная проверка нескольких NPC (например: crow, turret, rollermine, dog, gman), что у каждого появилась корректная подсказка `Default (Easy/Normal/Hard)`.

Файлы: `e:\XBLAH's modding tool\tools\FGDs\Mapbase\base_vizzys.fgd`, `...\halflife2_vizzys.fgd`, `...\hl2rpm.fgd`