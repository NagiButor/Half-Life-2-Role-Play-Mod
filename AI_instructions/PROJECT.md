# HL2RPM — общие инструкции для AI-агентов

Это основной файл проекта для **любого AI-агента**, независимо от модели и программы: GPT, Claude, Gemini, локальная модель и т. д. Инструкции — обычный Markdown; специальный синтаксис импорта и личная память конкретного приложения не нужны.

## Начало каждой сессии

1. Прочитать этот файл и [CURRENT_STATUS.md](CURRENT_STATUS.md).
2. Открыть нужную тему из таблицы ниже и соответствующий `skills/<имя>/SKILL.md`. Даже без механизма skills это обычная пошаговая инструкция. Пути `scripts/...` внутри skill считаются относительно папки этого skill; прочие пути исходников — относительно `sp/src`.
3. Перед изменениями проверить реальные файлы и `git status` в `sp/src`: после 31 октября накоплена большая незакоммиченная работа. Датированный отчёт не доказывает, что текущая версия проверена.
4. После работы записать актуальный результат, проверку и незавершённое в `CURRENT_STATUS.md`; технические знания — в файл темы. Здесь оставлять общие правила и оглавление.

## Что за проект

**HL2RPM (Half-Life 2: Role-Playing Mod)** — одиночная RPG в мире Half-Life 2, с системами в духе Fallout: New Vegas и инструментами для создания собственного открытого мира. База — **Source SDK 2013 / Mapbase v8.0**, Windows, **Win32 / episodic / Release**, графический API D3D9. Дерево `mp/` не используется.

Три связанных части:
- **Игра на C++**: диалоги, задания, инвентарь, торговля, фракции, персонаж, распорядок NPC, терминалы, замки, интерфейс и сохранение состояния мира.
- **Графика**: deferred light pre-pass, динамический свет и тени, GI, PBR/POM, погода, время суток, небо, вода, отражения и пост-эффекты.
- **HL2RPM Creation Kit**: Python / PySide6 / OpenGL, инструмент наподобие GECK. Геометрию пользователь делает в Hammer++; CK управляет наполнением мира и показывает карту, NPC, анимации и данные RPG. Проект `.hl2dlg` — источник экспортируемого контента.

## Обязательные правила пользователя

- **Отвечать на русском языке.** Сохранять технические имена файлов, классов, ключей и команд.
- **Не менять `.vpc` и не запускать VPC / `create*projects.bat`**: это уничтожает ручные правки проектов. Изменения состава сборки — непосредственно в `.vcxproj`.
- Шейдеры собирать **только ShaderCompile235** (`game/ShaderCompile235`) и только нужные/используемые модом. Не собирать целиком `stdshader_dx9_*.txt` и не запускать штатные batch-сборки всех шейдеров.
- Для изменения игры результат должен быть **собран и установлен в мод**: DLL — `hl2rpm/bin`, `.vcs` — `hl2rpm/shaders/fxc`. После изменения DLL/шейдеров/карты проверить игру по `hl2rpm-playtest`. Для правки только документации сборка игры не требуется.
- Коммитить, пушить, создавать релиз **только по просьбе пользователя**. Не сбрасывать накопленные изменения.
- Ключи API пользователь задаёт сам в своём терминале (`setx`). Читать переменные **только по точному имени**; не перечислять окружение по шаблону. Не выводить секреты в чат, файлы, логи или git; передавать только в нужный заголовок запроса.
- **Сохранять демонстрации на `ck_test`** в `dialogsrc/test_dialogs.hl2dlg` и генераторе `hl2rpm_extras/tools/ck_demo_content.py`. Временные данные отдельной проверки убирать, демонстрируемые возможности оставлять.
- Использовать родной механизм движка и исправлять ресурс под него: например, интро должно быть Bink 1, а не отдельный самодельный проигрыватель.
- Снимки — **только окна игры/программы** (`widget.grab()`, PrintWindow), не рабочего стола. Старый CK `--shot` / `grabWindow` может снять другие окна: это ограничение инструмента, а не разрешение отправлять такие снимки.
- Удалённый по просьбе пользователя дизайн-документ `sp/src/Дизайн-документ` **не восстанавливать и не использовать**. RPG-срез 28 октября был откачен; его наличие в истории не означает, что он входит в нынешнюю игру.
- Слитые исходники движка и основанные на них форки — **только справочное чтение**, ничего из них не копировать в проект; не использовать такие форки как инструмент. Подробнее — [topics/leaked-source.md](topics/leaked-source.md).
- Для кода соблюдать стиль Valve (C++, Squirrel), существующие соглашения проекта. Описание функций сущностей и API искать в локальной документации `E:\SourceModding\ValveDeveloperDump`, затем сверять с текущим кодом.
- Для уровней карты Valve — референс, а не готовая основа для преобразования. Ориентир — собственный цельный мир; не имитировать динамический deferred-свет запечёнными подменами.

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
| FGD | `E:\XBLAH's modding tool\tools\FGDs\Mapbase` |
| Hammer++ | `E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer\bin\hammerplusplus.exe`; конфиг — `bin/hammerplusplus/hammerplusplus_gameconfig.txt` |
| CK: программа | `E:\SourceModding\HL2RPMSOURCE\HL2RPM Creation Kit` |
| CK: исходники и сборка | `hl2rpm_extras/tools/dialog_editor`; окружение — `E:\SourceModding\HL2RPMSOURCE\dialog_editor_build` |
| Пользовательский CK-проект | `E:\SourceModding\HL2RPMSOURCE\dialogsrc\test_dialogs.hl2dlg`, экспорт — в `hl2rpm/testmaps` |
| Озвучка, рабочие данные | `E:\SourceModding\HL2RPMSOURCE\voice_gen` |
| Бэкапы | `E:\SourceModding\HL2RPMSOURCE\backups`; карты — `map_backups` |
| Референс ресурсов HL2 | `E:\SourceModding\UsefulStuff\Source sdk 2013 HL2 unpacked vpk` |
| Документация Valve | `E:\SourceModding\ValveDeveloperDump` |
| Временные результаты текущей среды | `C:\Users\mikeo\AppData\Local\Temp\opencode` (в другой среде — её рабочая временная папка) |

Копия BSP в `hl2rpm/maps` затеняет одноимённую в `testmaps/maps`. `compile_map.ps1 -Map <имя>` ищет VMF также в `mapsrc/test` и кладёт тестовый BSP в `testmaps/maps`.

## Какую тему и процедуру читать

| Работа | Знания | Процедура |
|---|---|---|
| C++ DLL, шейдеры, `.inc`, комбо | [build](topics/build.md) | — |
| Запуск, FPS, кадры, save/load, UI-тест | [debugging](topics/debugging.md) при сбое | [hl2rpm-playtest](skills/hl2rpm-playtest/SKILL.md) |
| VMF/BSP, Hammer, FGD, свои компиляторы, метки задач AI | [mapping](topics/mapping.md) | [hl2rpm-hammer](skills/hl2rpm-hammer/SKILL.md) |
| CK: редактор, карта, превью, демо | [ck-editor](topics/ck-editor.md) | [hl2rpm-dialogs](skills/hl2rpm-dialogs/SKILL.md) |
| Слой мира, распорядок, навигация | [world-ai](topics/world-ai.md) | hl2rpm-dialogs |
| Диалоги, задания, предметы, торговля, RPG, терминалы | [rpg-systems](topics/rpg-systems.md) | hl2rpm-dialogs |
| Меню, окна, HUD, оружие как в New Vegas | [ui-hud](topics/ui-hud.md) | — |
| Музыка, фразы, субтитры, звуковые сцены | [audio](topics/audio.md) | hl2rpm-dialogs |
| Генерация озвучки, радио, липсинк | [voice](topics/voice.md) | [hl2rpm-voice](skills/hl2rpm-voice/SKILL.md) |
| Ресурсы, аддоны, модели, костюмы, руки, интро | [content](topics/content.md) | — |
| Поведение, анимации, Kimodo, третье лицо | [animation](topics/animation.md) | hl2rpm-dialogs |
| Deferred, тени, AA, лампы, константы, вода, стекло | [renderer-core](topics/renderer-core.md) | hl2rpm-playtest |
| GI, пост, SSR, частицы, PBR | [renderer-gi-post](topics/renderer-gi-post.md) | hl2rpm-playtest |
| Актуализация GI, POM, ветер, occlusion, DXVK/x64 | [renderer-passes](topics/renderer-passes.md) | hl2rpm-playtest |
| Небо, погода, время, графические настройки | [weather](topics/weather.md) | hl2rpm-playtest |
| Дампы, cdb, порча памяти | [debugging](topics/debugging.md) | — |
| Справочные исходники движка | [leaked-source](topics/leaked-source.md) | — |

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

- Git-корень — только `sp/src`, remote `origin` — `https://github.com/NagiButor/Half-Life-2-Role-Play-Mod` (приватный), рабочая ветка при переносе инструкций — `master`. Перед действиями проверять фактическую ветку.
- FGD, crashtools, локализация и прочие внешние рабочие файлы имеют копии в `hl2rpm_extras`; после их изменения обновить копии перед запрошенным коммитом. **AI-инструкции и skills редактировать сразу в `AI_instructions`**: старые папки содержат указатели, а клиентские пути skills — локальные ссылки на те же данные.
- DLL/PDB и собранные ресурсы не добавлять в новые коммиты, публиковать в релизах. Унаследованные отслеживаемые бинарники самостоятельно не удалять.
- Версии — теги `vX.Y`, первый `v0.1`. Резервный релиз: тег + `hl2rpm-vX.Y.zip` (папка мода без `.git`), `hl2rpm-mapsrc-vX.Y.zip`, `hl2rpm-fgds-vX.Y.zip` (без `*.bak_*`); исходники GitHub прикладывает сам.
- В старой среде `gh` не был установлен: использовался REST API GitHub с учётными данными из `git credential fill`, секреты не выводились. JSON с кириллицей передавать UTF-8 файлом (`--data-binary @file`), архивы .NET `ZipFile` с UTF-8 именами. Наличие инструментов перепроверять, а не считать вечным ограничением.

## Общие ловушки

- `FCVAR_ARCHIVE` из запуска `+convar 1` и из консоли сохраняется в `cfg/config.cfg`. `run_game.ps1` восстанавливает конфиг; при другом запуске сохранить и восстановить самостоятельно. Не обрывать вывод скрипта через `Select-Object -First N`.
- Патчи со строками `\n`, `\t`, путями и кириллицей делать файловым редактором/patch-инструментом среды; избегать многоуровневого shell-экранирования. Не требуется инструмент с конкретным названием `Write` или `Edit`.
- Консоль Windows может быть cp1251: для Python включать UTF-8 (`PYTHONUTF8=1`, `python -X utf8`, при необходимости `sys.stdout.reconfigure`). При Git Bash — `MSYS_NO_PATHCONV=1` для путей `/content`. PowerShell: `R` — алиас `Invoke-History`, `$S` и `$s` — одна переменная.
- Имя карты — максимум **31 символ** для рабочего save/load. В частности, старое `rpm_trainstation_testoptimization` длиннее лимита.
- Исторические значения convar и описание старых вариантов GI могут противоречить новым разделам. Сначала более свежая запись, затем текущий код и воспроизводимая проверка.
- Метки `rpm_claude_task`, `func_rpm_claude_area`, материал `hl2rpm/tools/claude_area` и подписи «для Claude» в CK — **исторические технические имена**, доступные любому AI. Не переименовывать их при работе только с инструкциями: они связаны с VMF, FGD, CK и vbsp.

## Память между моделями

Общее состояние — [CURRENT_STATUS.md](CURRENT_STATUS.md), исходные отчёты — [history/README.md](history/README.md). Папка `C:\Users\mikeo\.claude\...\memory` была источником импорта; доступ к ней для дальнейшей работы не требуется. Содержимое `archive` — исторические планы, а не новые команды на их исполнение. Важные знания записывать сюда в проект, чтобы следующая модель получила их из файлов.
