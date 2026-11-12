## Почему падает сборка
- C2280 `QuestDef::QuestDef(const QuestDef&)`: я добавил в `QuestDef` поля `CUtlVector<CUtlString>`, а `CQuestSystem::RegisterQuestDef()` сейчас делает копирование (`m_QuestDefs[i] = def;` и `m_QuestDefs.AddToTail(def);`). Для `QuestDef` компилятор генерирует copy ctor/оператор, но он становится удалённым из-за некопируемых членов.
- LNK4075 всё ещё появляется: `client_episodic.vcxproj` перегенерируется VPC-ом на каждом билде (prebuild вызывает vpc.exe), поэтому правки в vcxproj не гарантированно держатся. В логах линковки видно, что Release-конфиг всегда получает `/OPT:REF` из vpc-шаблона `vpc_scripts/source_dll_win32_release.vpc`, из-за чего и появляется LNK4075.

## Что сделаю
### 1) Исправлю C2280 (QuestDef без копирования)
- В [quest_system.cpp] перепишу `CQuestSystem::RegisterQuestDef()` так, чтобы он **не копировал** `QuestDef`.
- Добавлю локальный helper `CopyQuestDef(dst, src)` который:
  - копирует простые поля (`id/title/desc/minReputation/killTarget/killCount/failOnKillTarget/failOnKillRepPenalty`),
  - делает `dst.killTargets.RemoveAll()` и добавляет элементы циклом,
  - аналогично для `failOnKillTargets`.
- Вместо `m_QuestDefs[i] = def;` буду вызывать `CopyQuestDef(m_QuestDefs[i], def)`.
- Вместо `AddToTail(def)` буду делать `m_QuestDefs.AddToTail();` и заполнять `Tail()` через `CopyQuestDef`.

### 2) Уберу LNK4075 на уровне VPC (не vcxproj)
- В [source_dll_win32_release.vpc] сделаю `$References` условным:
  - `$References "Eliminate Unreferenced Data (/OPT:REF)" [$RETAIL]`
  - `$References "No (/OPT:NOREF)" [!$RETAIL]`
  Это уберёт конфликтующий `/OPT:REF` в дев-сборках, где VS может пытаться включать E&C/отладочные режимы.
- При необходимости применю то же правило к [source_exe_win_win32_release.vpc] (чтобы не ловить LNK4075 в утилитах).

## Проверка
- Пересоберу Server (Episodic): C2280 должен исчезнуть.
- Пересоберу Client (Episodic): LNK4075 должен исчезнуть (после регенерации проектов VPC-ом).