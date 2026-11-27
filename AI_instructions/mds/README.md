# AI_instructions — единая база знаний HL2RPM

**Начать с [PROJECT.md](PROJECT.md), затем [CURRENT_STATUS.md](CURRENT_STATUS.md).** Все `.md`-инструкции, правила, темы, навыки, справочники, отчёты истории и архив проекта собраны здесь по папкам.

## Структура базы

- [PROJECT.md](PROJECT.md) — главный мастер-индекс проекта: правила пользователя, пути, сборка, ограничения и оглавление всех остальных файлов.
- [CURRENT_STATUS.md](CURRENT_STATUS.md) — актуальная передача работы: последние изменения, проверки и открытые задачи.
- `topics/` — 16 технических тем по системам мода (рендер, погода, маппинг, CK, RPG, AI, звук, озвучка, анимации, сборка, отладка).
- `skills/` — 4 пошаговые процедуры (`SKILL.md`) с рабочими скриптами:
  - `hl2rpm-hammer/` — редактирование VMF, компиляция (`hl2rpm_vbsp/vvis/vrad`), проверка BSP;
  - `hl2rpm-playtest/` — запуск `hl2.exe`, скриншоты, FPS, save/load, `console.log`;
  - `hl2rpm-dialogs/` — RPG, Creation Kit, квесты, NPC, распорядок;
  - `hl2rpm-voice/` — генерация озвучки и липсинка.
- [references/](references/README.md) — справочники инструментов и руководства (`hl2rpm-extras.md`, `creation-kit.md`, `tools-world-planner.md`, `tools-city27-prototype.md`, `kimodo-hl2rpm-notes.md`, `motion-bricks-hl2rpm-notes.md`).
- [history/](history/README.md) — 31 исходный датированный отчёт и фиксация предпочтений пользователя (исторический срез).
- [archive/](archive/README.md) — старые планы Trae, завершённый план CK, раннее исследование шейдеров и старые шаблоны.
- `MIGRATION.md` и `migration-sources.json` — отчёт и контрольные суммы консолидации базы 2026-11-10.
- `tools/setup_compat.ps1` — восстановление Windows-junction (`AI_instructions`, `.agents/skills`, `.claude/skills`) после клонирования.

## Входные файлы для разных AI-клиентов

Каноническое хранилище физически находится в **`source-sdk-2013-mapbase-v8.0/sp/src/AI_instructions`** (внутри git-корня), а `HL2RPMSOURCE/AI_instructions` — Windows-junction на него. Минимальные файлы-ссылки `AGENTS.md`, `CLAUDE.md`, `GEMINI.md` и `.github/copilot-instructions.md` в корне рабочей области и `sp/src/` служат точками автозагрузки и не дублируют правила.

## Как обновлять

1. Правила и маршрутизация — `PROJECT.md`.
2. Детали конкретной подсистемы — соответствующий файл в `topics/`.
3. Повторяемая процедура и накопленные правила по маппингу/тестам — соответствующий `skills/<имя>/SKILL.md`.
4. Справочник инструментов — `references/`.
5. Итог сессии — `CURRENT_STATUS.md`: дата, сделано, проверено каким способом, открытые вопросы.

Различать «есть в исходниках», «собрано», «проверено в игре» и «принято пользователем».
