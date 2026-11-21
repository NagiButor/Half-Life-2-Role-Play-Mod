# HL2RPM — общие инструкции для AI-агентов

Это основной мастер-файл проекта для **любого AI-агента**, независимо от модели и программы. Все проектные `.md`-инструкции, процедуры и справочники сведены по папкам в `AI_instructions/` (единый стандарт — см. [README.md](README.md)).

## Начало каждой сессии

1. Прочитать этот файл (`PROJECT.md`) и [CURRENT_STATUS.md](CURRENT_STATUS.md).
2. Открыть нужную тему из `topics/` и процедуру из `skills/<имя>/SKILL.md` по таблице ниже. Пути `scripts/...` внутри skill считаются относительно папки этого skill; прочие пути исходников — относительно `sp/src`.
3. Перед изменениями проверить реальные файлы и `git status` в `sp/src`: после 31 октября накоплена большая незакоммиченная работа. Датированный отчёт не доказывает, что текущая версия проверена.
4. После работы записать актуальный результат, проверку и незавершённое в [CURRENT_STATUS.md](CURRENT_STATUS.md); технические знания — в файл `topics/` или в живые разделы `skills/`. Здесь оставлять общие правила и оглавление.

## Что за проект

**HL2RPM (Half-Life 2: Role-Playing Mod)** — одиночная RPG в мире Half-Life 2, с системами в духе Fallout: New Vegas и собственным миром **Сити-27** (референс — Будапешт, Венгрия). База — **Source SDK 2013 / Mapbase v8.0**, Windows, **Win32 / episodic / Release**, графический API D3D9. Дерево `mp/` не используется.

Три связанных части:
- **Игра на C++**: диалоги, задания, инвентарь, торговля, фракции, персонаж, распорядок NPC, терминалы, замки, интерфейс и сохранение состояния мира.
- **Графика**: deferred light pre-pass, динамический свет и тени, GI, PBR/POM, погода, время суток, небо, вода, отражения и пост-эффекты.
- **HL2RPM Creation Kit**: Python / PySide6 / OpenGL, инструмент наподобие GECK. Геометрию пользователь и агент делают в Hammer++ / VMF; CK управляет наполнением мира и показывает карту, NPC, анимации и данные RPG. Проект `.hl2dlg` — источник экспортируемого контента.

## Обязательные правила пользователя

- **Отвечать на русском языке.** Сохранять технические имена файлов, классов, ключей и команд.
- **Не менять `.vpc` и не запускать VPC / `create*projects.bat`**: это уничтожает ручные правки проектов. Изменения состава сборки — непосредственно в `.vcxproj`.
- Шейдеры собирать **только ShaderCompile235** (`game/ShaderCompile235`) и только нужные/используемые модом. Не собирать целиком `stdshader_dx9_*.txt` и не запускать штатные batch-сборки всех шейдеров.
- Для изменения игры результат должен быть **собран и установлен в мод**: DLL — `hl2rpm/bin`, `.vcs` — `hl2rpm/shaders/fxc`. После изменения DLL/шейдеров/карты проверить игру по [hl2rpm-playtest](skills/hl2rpm-playtest/SKILL.md). Для правки только документации сборка игры не требуется.
- Коммитить, пушить, создавать релиз **только по просьбе пользователя**. Не сбрасывать накопленные изменения.
- **Не изменять и не сохранять исходную сцену `hl2rpm_map_developing/hl2rpm_map.blend`** — использовать её только для чтения/рендера в `--background --factory-startup`.
- Ключи API пользователь задаёт сам в своём терминале (`setx`). Читать переменные **только по точному имени**; не перечислять окружение по шаблону. Не выводить секреты в чат, файлы, логи или git; передавать только в нужный заголовок запроса.
- **Сохранять демонстрации на `ck_test`** в `dialogsrc/test_dialogs.hl2dlg` и генераторе `hl2rpm_extras/tools/ck_demo_content.py`. Временные данные отдельной проверки убирать, демонстрируемые возможности оставлять.
- Использовать родной механизм движка и исправлять ресурс под него: например, интро должно быть Bink 1, а не отдельный самодельный проигрыватель.
- Снимки — **только окна игры/программы** (`widget.grab()`, PrintWindow), не рабочего стола.
- Удалённый по просьбе пользователя дизайн-документ `sp/src/Дизайн-документ` **не восстанавливать и не использовать**. RPG-срез 28 октября был откачен; его наличие в истории не означает, что он входит в нынешнюю игру.
- Слитые исходники движка и основанные на них форки — **только справочное чтение**, ничего из них не копировать в проект; не использовать такие форки как инструмент. Подробнее — [topics/leaked-source.md](topics/leaked-source.md).
- Для кода соблюдать стиль Valve (C++, Squirrel), существующие соглашения проекта. Описание функций сущностей и API искать в локальной документации `E:\SourceModding\ValveDeveloperDump`, затем сверять с текущим кодом.
- Для уровней карты Valve — референс, а не готовая основа для преобразования. Ориентир — собственный цельный мир Сити-27; не имитировать динамический deferred-свет запечёнными подменами.

## Где что лежит

Все абсолютные пути ниже — **текущая установка пользователя**, а не требование к имени диска у любой модели. При переносе сначала найти соответствующие папки; не подменять пути молча.

| Часть | Путь |
|---|---|
| Рабочая область | `E:\SourceModding\HL2RPMSOURCE` |
| Исходники и настоящий git-корень | `E:\SourceModding\HL2RPMSOURCE\source-sdk-2013-mapbase-v8.0\sp\src` |
| Канонические AI-инструкции, в git-корне | `sp/src/AI_instructions` |
| Удобный вход из рабочей области | `E:\SourceModding\HL2RPMSOURCE\AI_instructions` — junction на каноническую папку |
| Собранный мод | `E:\Steam\steamapps\sourcemods\hl2rpm` — собственный локальный `.git`, без remote |
| Игровые карты | `hl2rpm/maps` |
| Dev-карты и ресурсы | `hl2rpm/testmaps/{maps,scripts,scenes,sound,models,materials}` — путь поиска ниже корня мода, в релиз не идёт |
| Исходники карт | `E:\XBLAH's modding tool\content\Mapbase\hl2rpm\mapsrc`; тестовые — `mapsrc/test` |
| Референсная 3D-сцена Сити-27 и координаты | `E:\SourceModding\HL2RPMSOURCE\hl2rpm_map_developing\hl2rpm_map.blend` и `hl2rpm_map_inspiration.txt` |
| FGD | `E:\XBLAH's modding tool\tools\FGDs\Mapbase` |
| Hammer++ | `E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer\bin\hammerplusplus.exe`; конфиг — `bin/hammerplusplus/hammerplusplus_gameconfig.txt` |
| Blender (CLI/фоновый рендер) | Blender 5.3 (`blender-launcher.exe` / Store) и Blender 4.4.3 (`Google Map 3D Exporter v3.0\02 Prepare Blender 4.4.3 with Map's Plugin\blender-4.4.3-windows-x64\blender.exe`) |
| CK: программа | `E:\SourceModding\HL2RPMSOURCE\HL2RPM Creation Kit` |
| CK: исходники и сборка | `hl2rpm_extras/tools/dialog_editor`; окружение — `E:\SourceModding\HL2RPMSOURCE\dialog_editor_build` |
| Пользовательский CK-проект | `E:\SourceModding\HL2RPMSOURCE\dialogsrc\test_dialogs.hl2dlg`, экспорт — в `hl2rpm/testmaps` |
| Озвучка, рабочие данные | `E:\SourceModding\HL2RPMSOURCE\voice_gen` |
| Бэкапы | `E:\SourceModding\HL2RPMSOURCE\backups`; карты — `map_backups` |
| Референс ресурсов HL2 | `E:\SourceModding\UsefulStuff\Source sdk 2013 HL2 unpacked vpk` |
| Документация Valve | `E:\SourceModding\ValveDeveloperDump` |

Копия BSP в `hl2rpm/maps` затеняет одноимённую в `testmaps/maps`. `compile_map.ps1 -Map <имя>` ищет VMF также в `mapsrc/test` и кладёт тестовый BSP в `testmaps/maps`.

## Оглавление базы `AI_instructions/`

### Темы (`topics/`) и процедуры (`skills/`)

| Работа | Знания (`topics/`) | Процедура (`skills/`) |
|---|---|---|
| C++ DLL, шейдеры, `.inc`, комбо | [build](topics/build.md) | — |
| Запуск, FPS, кадры, save/load, UI-тест | [debugging](topics/debugging.md) при сбое | [hl2rpm-playtest](skills/hl2rpm-playtest/SKILL.md) |
| VMF/BSP, Hammer, FGD, свои компиляторы, метки задач AI | [mapping](topics/mapping.md) | [hl2rpm-hammer](skills/hl2rpm-hammer/SKILL.md) |
| CK: редактор, карта, превью, демо | [ck-editor](topics/ck-editor.md) | [hl2rpm-dialogs](skills/hl2rpm-dialogs/SKILL.md) |
| Слой мира, распорядок, навигация | [world-ai](topics/world-ai.md) | [hl2rpm-dialogs](skills/hl2rpm-dialogs/SKILL.md) |
| Диалоги, задания, предметы, торговля, RPG, терминалы | [rpg-systems](topics/rpg-systems.md) | [hl2rpm-dialogs](skills/hl2rpm-dialogs/SKILL.md) |
| Меню, окна, HUD, оружие как в New Vegas | [ui-hud](topics/ui-hud.md) | — |
| Музыка, фразы, субтитры, звуковые сцены | [audio](topics/audio.md) | [hl2rpm-dialogs](skills/hl2rpm-dialogs/SKILL.md) |
| Генерация озвучки, радио, липсинк | [voice](topics/voice.md) | [hl2rpm-voice](skills/hl2rpm-voice/SKILL.md) |
| Ресурсы, аддоны, модели, костюмы, руки, интро | [content](topics/content.md) | — |
| Поведение, анимации, Kimodo, третье лицо | [animation](topics/animation.md) | [hl2rpm-dialogs](skills/hl2rpm-dialogs/SKILL.md) |
| Deferred, тени, AA, лампы, константы, вода, стекло | [renderer-core](topics/renderer-core.md) | [hl2rpm-playtest](skills/hl2rpm-playtest/SKILL.md) |
| GI, пост, SSR, частицы, PBR | [renderer-gi-post](topics/renderer-gi-post.md) | [hl2rpm-playtest](skills/hl2rpm-playtest/SKILL.md) |
| Актуализация GI, POM, ветер, occlusion, DXVK/x64 | [renderer-passes](topics/renderer-passes.md) | [hl2rpm-playtest](skills/hl2rpm-playtest/SKILL.md) |
| Небо, погода, время, графические настройки | [weather](topics/weather.md) | [hl2rpm-playtest](skills/hl2rpm-playtest/SKILL.md) |
| Дампы, cdb, порча памяти | [debugging](topics/debugging.md) | — |
| Справочные исходники движка | [leaked-source](topics/leaked-source.md) | — |

### Справочники и инструменты (`references/`, `history/`, `archive/`)

- [references/README.md](references/README.md) — индекс справочников по инструментам и модулям:
  - [hl2rpm-extras.md](references/hl2rpm-extras.md) — карта `sp/src/hl2rpm_extras/` (FGD, локализация, генераторы и утилиты);
  - [creation-kit.md](references/creation-kit.md) — полное руководство по HL2RPM Creation Kit;
  - [tools-world-planner.md](references/tools-world-planner.md) — глобальный 3D-мастерплан Сити-27 (`hl2rpm_extras/tools/world/`);
  - [tools-city27-prototype.md](references/tools-city27-prototype.md) — ранний прототип `city27_test` и общий валидатор `validate_vmf.py`;
  - [kimodo-hl2rpm-notes.md](references/kimodo-hl2rpm-notes.md), [motion-bricks-hl2rpm-notes.md](references/motion-bricks-hl2rpm-notes.md) — справки по инструментам анимации.
- [history/README.md](history/README.md) — 31 датированный отчёт и история решений.
- [archive/README.md](archive/README.md) — архивные планы Trae, завершённый план CK, раннее исследование шейдеров и старые шаблоны.
- [tools/setup_compat.ps1](tools/setup_compat.ps1) — восстановление Windows-junction для AI-клиентов.

## Сборка — кратко

Команды запускать из git-корня `sp/src`. Подробности и порядок обновления `.inc` — [topics/build.md](topics/build.md).

```powershell
$msb = "E:\Programs\Microsoft Visual Studio\Community 2022\MSBuild\Current\Bin\MSBuild.exe"
& $msb "game\client\client_episodic.vcxproj" /p:Configuration=Release /p:Platform=Win32 /m
& $msb "game\server\server_episodic.vcxproj" /p:Configuration=Release /p:Platform=Win32 /m
& $msb "materialsystem\stdshaders\game_shader_dx9_episodic.vcxproj" /p:Configuration=Release /p:Platform=Win32 /m
python materialsystem\stdshaders\check_shader_combos.py
```

VS 2022, v143; только проекты `*_episodic`. PostBuild копирует DLL/PDB в `hl2rpm/bin`; игра или Hammer могут блокировать копирование. Проверка комбо должна дать `0 mismatched`. Сам компилятор шейдеров создаёт `.vcs` и `.inc`, после чего нужные `.vcs` нужно скопировать в мод. Сборка CK: `powershell -File build_editor.ps1` из `hl2rpm_extras/tools/dialog_editor`.

## Git, ресурсы и резервные копии

- Git-корень — только `sp/src`, remote `origin` — `https://github.com/NagiButor/Half-Life-2-Role-Play-Mod` (приватный), рабочая ветка — `master`. Перед действиями проверять фактическую ветку.
- FGD, crashtools, локализация и прочие внешние рабочие файлы имеют копии в `hl2rpm_extras`; после их изменения обновить копии перед запрошенным коммитом. **Все AI-инструкции, темы, справочники и skills редактировать строго в `AI_instructions/`**.
- DLL/PDB и собранные ресурсы не добавлять в новые коммиты, публиковать в релизах. Унаследованные отслеживаемые бинарники самостоятельно не удалять.
- Версии — теги `vX.Y`, первый `v0.1`. Резервный релиз: тег + `hl2rpm-vX.Y.zip` (папка мода без `.git`), `hl2rpm-mapsrc-vX.Y.zip`, `hl2rpm-fgds-vX.Y.zip` (без `*.bak_*`); исходники GitHub прикладывает сам.

## Общие ловушки

- `FCVAR_ARCHIVE` из запуска `+convar 1` и из консоли сохраняется в `cfg/config.cfg`. `run_game.ps1` восстанавливает конфиг; при другом запуске сохранить и восстановить самостоятельно. Не обрывать вывод скрипта через `Select-Object -First N`.
- Патчи со строками `\n`, `\t`, путями и кириллицей делать файловым редактором/patch-инструментом среды; избегать многоуровневого shell-экранирования.
- Консоль Windows может быть cp1251: для Python включать UTF-8 (`PYTHONUTF8=1`, `python -X utf8`, при необходимости `sys.stdout.reconfigure`). При Git Bash — `MSYS_NO_PATHCONV=1` для путей `/content`. PowerShell: `R` — алиас `Invoke-History`, `$S` и `$s` — одна переменная.
- Имя карты — максимум **31 символ** для рабочего save/load. В частности, старое `rpm_trainstation_testoptimization` длиннее лимита.
- Исторические значения convar и описание старых вариантов GI могут противоречить новым разделам. Сначала более свежая запись, затем текущий код и воспроизводимая проверка.
- Метки `rpm_claude_task`, `func_rpm_claude_area`, материал `hl2rpm/tools/claude_area` и подписи «для Claude» в CK — **исторические технические имена**, доступные любому AI. Не переименовывать их: они связаны с VMF, FGD, CK и vbsp.
