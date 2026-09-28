# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Правила пользователя (обязательно)

- **Всегда отвечать на русском языке.**
- Проект — мод **HL2RPM** на Source SDK 2013 (Mapbase v8.0), singleplayer, конфигурация **episodic, Release**.
  Рабочие исходники: `E:\SourceModding\HL2RPMSOURCE\source-sdk-2013-mapbase-v8.0\sp\src`. Дерево `mp/` не используется.
- **Не трогать `.vpc` файлы.** Не перегенерировать проекты через VPC (`create*projects.bat`) — это затрёт ручные правки. Правки сборки вносить прямо в `.vcxproj`.
- Шейдеры компилировать **только** кастомным ShaderCompile235 (`sp\src\game\ShaderCompile235`) и **только нужные/используемые модом** шейдеры — никогда не собирать весь `stdshader_dx9_*.txt` (занимает вечность).
- Результат работы — **всё собрано и лежит в папке мода** `E:\Steam\steamapps\sourcemods\hl2rpm` (DLL в `bin\`, шейдеры в `shaders\fxc\`).
- Референс по игровым ресурсам (модели, текстуры, скрипты, звуки HL2): `E:\SourceModding\UsefulStuff\Source sdk 2013 HL2 unpacked vpk`.

## Расположение частей проекта

| Что | Где |
|---|---|
| Исходники C++/шейдеров (git) | `E:\SourceModding\HL2RPMSOURCE\source-sdk-2013-mapbase-v8.0\sp\src` |
| Собранный мод | `E:\Steam\steamapps\sourcemods\hl2rpm` (имеет свой локальный `.git` без remote) |
| Исходники карт (`.vmf`) | `E:\XBLAH's modding tool\content\Mapbase\hl2rpm\mapsrc` |
| FGD для Hammer (с кастомными энтити) | `E:\XBLAH's modding tool\tools\FGDs\Mapbase` |
| Hammer++ и его конфиг | `E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer\bin\hammerplusplus\hammerplusplus_gameconfig.txt` |

## Git и резервные копии

- Репозиторий — только `sp\src`, remote `origin` = https://github.com/NagiButor/Half-Life-2-Role-Play-Mod (приватный), ветка `master`.
- Этот `CLAUDE.md` — основной (в корне `HL2RPMSOURCE` лежит только `@import` на него). Копии FGD и `crashtools` хранятся в `hl2rpm_extras/` (см. README там): после правки рабочих файлов обновить копии перед коммитом. Бинарники (DLL/PDB) в git не класть — только в релизы.
- Коммитить/пушить только по просьбе пользователя. Версии помечаются тегами `vX.Y` (первый — `v0.1`).
- Резервный релиз на GitHub = тег + архивы, прикреплённые к релизу: `hl2rpm-vX.Y.zip` (папка мода без `.git`), `hl2rpm-mapsrc-vX.Y.zip`, `hl2rpm-fgds-vX.Y.zip` (без `*.bak_*`); исходники GitHub прикладывает сам. `gh` CLI не установлен — релизы создаются через REST API (`api.github.com` / `uploads.github.com`) с токеном из `git credential fill`. JSON с кириллицей передавать файлом (`--data-binary @file`), иначе ломается кодировка. Архивы собирать через .NET `ZipFile` с UTF-8 именами (7-Zip не установлен).

## Отладчик крашей

- Ядро: `game/shared/hl2rpm_crashdebug.{h,cpp}` — чистый Win32 без Source-заголовков (в vcxproj `PrecompiledHeader=NotUsing`; снимает `#define`-запреты `strncpy/_snprintf/fopen`). Собирается в client и server; буфер «крошек» общий через named shared memory `Local\hl2rpm_crashdbg_<pid>`.
- Клиентская обвязка: `game/client/hl2rpm_crashhandler_client.cpp` (установка в `CHLClient::Init` сразу после получения `engine`, снятие последним в `CHLClient::Shutdown`; перехват spew для консоли; команды `rpm_crash_crumbs`, `rpm_crash_report`, `rpm_crash_test null|div|stack` (cheat), convar `rpm_crash_handler`).
- Если отчёта нет: Windows (WER) хранит полные дампы `hl2.exe` в `%LOCALAPPDATA%\CrashDumps`, а журнал «Application Error» даёт модуль/код/смещение. `0x80000003` в `tier0.dll` = `Error()`/Assert движка — текст ошибки читать из дампа (`da` по первому аргументу `tier0!Error`) или в «Last console output» отчёта.
- При краше (VEH, до 3 отчётов за сессию, пропускается если подключён отладчик; ловит в т.ч. `int 3` от `Error()`) пишет `hl2rpm\crashes\crash_<время>.dmp` + `.txt` и запускает `hl2rpm\crashtools\analyze_crash.bat` (x86 `cdb` из Windows Kits → `.analysis.txt`). Запасной вариант — `crashtools\run_under_debugger.bat` (игра под cdb, ловит second-chance).
- Для символов PostBuild client/server копирует `.pdb` в `hl2rpm\bin`.
- Добавлять крошки в подозрительный код: `#include "hl2rpm_crashdebug.h"` + `RPM_CRUMB( "fmt", ... )` (дёшево, потокобезопасно). Уже расставлены в пути попаданий: `FireBullets`, `ImpactTrace`, `UTIL_DecalTrace`, серверный `DispatchEffect`, `CBaseHLBludgeonWeapon::Swing/Hit/ImpactEffect`, клиентский `DispatchEffectToCallback` (FX begin/end), `Impact()`/декали, `PerformCustomEffects`, `PlayImpactSound`.

## FGD / Hammer

- Hammer++ грузит FGD по порядку: `commentary.fgd`, `deferred.fgd`, `halflife2_vizzys.fgd` (включает `base_vizzys.fgd`), `hl2rpm.fgd`, `hammerplusplus_fgd.fgd`. `commentary`/`deferred` делают `@include "base.fgd"`, который находится не рядом, а в `bin` Hammer'а (fallback по cwd).
- `hl2rpm.fgd` опирается на базовые классы из `halflife2_vizzys.fgd` (`Weapon`, `BaseNPC` и др.). Синтаксическая ошибка в любом раньше загружаемом FGD обрывает его разбор → каскад ошибок «undefined base class» и энтити «obsolete» в Hammer.
- Перед правкой FGD делать копию `*.fgd.bak_YYYYMMDD_...` (так принято в папке). Кастомные правки помечены комментарием `//Pravka`; правка свойств существующей энтити должна быть внутри её `[ ]`, а не отдельным блоком.

## Сборка C++

Visual Studio 2022 Community: `E:\Programs\Microsoft Visual Studio\Community 2022`, тулсет v143, платформа Win32.

```powershell
$msb = "E:\Programs\Microsoft Visual Studio\Community 2022\MSBuild\Current\Bin\MSBuild.exe"
# client.dll / server.dll
& $msb "game\client\client_episodic.vcxproj" /p:Configuration=Release /p:Platform=Win32 /m
& $msb "game\server\server_episodic.vcxproj" /p:Configuration=Release /p:Platform=Win32 /m
# game_shader_dx9.dll (C++ часть шейдеров)
& $msb "materialsystem\stdshaders\game_shader_dx9_episodic.vcxproj" /p:Configuration=Release /p:Platform=Win32 /m
```
(пути относительно `sp\src`). Решения: `games.sln`, `shaders.sln`, `everything.sln`.

- PostBuild-события в `client_episodic`, `server_episodic` и `game_shader_dx9_episodic` уже копируют DLL в `E:\Steam\steamapps\sourcemods\hl2rpm\bin`. Если копирование падает — скорее всего запущена игра и DLL заблокирована.
- Предупреждение «This VCPROJ is out of sync with its VPC scripts» ожидаемо (vcxproj правятся вручную) — игнорировать, VPC не запускать.
- Используются только проекты `*_episodic` (не `*_hl2`). Выходные каталоги: `Release_mod_episodic` / `Release_dx9_mod_episodic`.
- В сообщениях компилятора кириллица из‑за локали MSVC может выводиться кракозябрами — ориентироваться на коды ошибок (C2065 и т.п.).

## Компиляция шейдеров (ShaderCompile235)

Исходники `.fxc` лежат в `sp\src\materialsystem\stdshaders`. Компилятор: `sp\src\game\ShaderCompile235\bin\ShaderCompile.exe` (standalone, без perl/DXSDK). Штатные `buildshaders.bat`/`buildepisodicshaders.bat` не использовать — они собирают всё.

```powershell
$sc = "E:\SourceModding\HL2RPMSOURCE\source-sdk-2013-mapbase-v8.0\sp\src\game\ShaderCompile235\bin\ShaderCompile.exe"
cd "E:\SourceModding\HL2RPMSOURCE\source-sdk-2013-mapbase-v8.0\sp\src\materialsystem\stdshaders"
# один шейдер: -ver 30 для *_ps30/*_vs30, -ver 20b для *_ps20b/*_vs20
& $sc -ver 30 -shaderpath . lightingpass_global_ps30.fxc
# только .inc-заголовок (без компиляции комбо): добавить -dynamic
```
Или списком через `process_shaders.ps1 -File <список.txt> -Version 30`.

- **Какой `.inc` видит C++:** в `game_shader_dx9_episodic.vcxproj` include-пути идут `fxctmp9;vshtmp9;include`, т.е. C++ берёт `.inc` из `fxctmp9\` (старый формат, классы `sdk_*`), а ShaderCompile235 пишет новые в `include\` (классы `SDK_*`). Если комбо шейдера меняются — обновить и `fxctmp9\<name>.inc` (копия из `include\` с заменой `SDK_`→`sdk_` в именах), иначе `.vcs` и C++ разойдутся → «Using invalid shader combo» → `Error()` → краш (так падали декали от пуль, 2026-09).
- **Проверка рассинхрона:** `python materialsystem\stdshaders\check_shader_combos.py` — сверяет каждый `.vcs` в `hl2rpm\shaders\fxc` с `.inc`, против которого собран C++ (число и порядок комбо). Запускать после любой компиляции шейдеров; должно быть `0 mismatched`.
- Новый fxc не принимает `register( D3DVERTEXTEXTURESAMPLER0, s0 )` (X3089) — писать `register( s0 )`.
- Hammer++ держит `hl2rpm\bin\game_shader_dx9.dll` открытой → PostBuild падает и удаляет собранную DLL. Либо закрыть Hammer, либо переименовать занятую DLL (`*.old_inuse`) и пересобрать.
- Выход: `.vcs` → `stdshaders\shaders\fxc\`, `.inc` → `stdshaders\include\`. После компиляции **скопировать нужные `.vcs` в `E:\Steam\steamapps\sourcemods\hl2rpm\shaders\fxc\`**.
- При изменении комбо/констант в `.fxc` сначала обновить `.inc`, затем пересобрать `game_shader_dx9_episodic` (C++ использует `.inc`; ошибки вида `vsh_forgot_to_set_dynamic_X` / необъявленные `iShadowMode` означают рассинхрон `.cpp` и `.inc`).
- Списки шейдеров мода: `stdshaders\hl2rpm_custom.txt` (используемые модом) и `stdshaders\deferred_shaders.txt` (весь deferred-набор). Добавляя новый шейдер, дописать его в `hl2rpm_custom.txt`.

## Архитектура: deferred-рендер

Поверх Mapbase встроен deferred-рендер (порт Biohazard90 deferred / vs2022-deferred) с каскадными тенями солнца. Подробный разбор шейдеров — `E:\SourceModding\HL2RPMSOURCE\SHADER_RESEARCH_SUMMARY.md`.

- **Клиент** `sp\src\game\client\deferred\`: `viewrender_deferred.cpp` (порядок проходов: G-buffer → тени → освещение → композит), `cdeferred_manager_client.cpp` (инициализация/шейдерный API), `deferred_rt.cpp` (рендер‑таргеты), `cascade_t.cpp` (параметры CSM-каскадов), `clight_manager.cpp`/`def_light_t.cpp` (точечные/прожекторные/глобальный свет), `clight_editor.cpp` (редактор света), `flashlighteffect_deferred.cpp`.
- **Шейдеры** `sp\src\materialsystem\stdshaders\`: C++ проходы `defpass_gbuffer/shadow/composite.cpp`, обёртки материалов `deferred_brush/model/eyes/teeth.cpp`, проходы освещения `lighting_global/world/volume.cpp`, радиосити `radiosity_*.cpp`; общие HLSL-заголовки `common_deferred_fxc.h`, `common_shadowmapping_fxc.h`; общие константы C++/HLSL — `deferred_global_common.h` (CSM, режимы теней, метод фильтрации).
- Связь клиент ↔ шейдерная DLL идёт через интерфейс `IDeferredExt` (`IDeferredExt.cpp` в stdshaders, `IDeferredExtClient.cpp` в клиенте): клиент передаёт туда данные света/каскадов, шейдеры читают их при биндинге констант. При изменении числа каскадов/регистров нужно синхронно править `deferred_global_common.h`, `cascade_t.*`, C++ биндинг констант и `.fxc` (регистры констант пиксельного шейдера ограничены — проверять пересечения `register(cN)`).
- Также есть процедурное небо/атмосфера (`sky_atmo_*`, `skyatmo_proc.cpp`) и `sun_godrays.cpp`.
