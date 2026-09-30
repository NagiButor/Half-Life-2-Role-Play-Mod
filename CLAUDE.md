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
- **Известный краш при выходе (не регрессия):** с включённым deferred выход из игры (даже «загрузить карту → `quit`») падает в `filesystem_stdio.dll` (0xC0000005 / 0xC0000374) из завершения `materialsystem`; с `-nodeferred` не воспроизводится. Причина не найдена — не путать с новыми крашами.

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
- Связь клиент ↔ шейдерная DLL идёт через интерфейс `IDeferredExt` (`IDeferredExt.cpp` в stdshaders, `IDeferredExtClient.cpp` в клиенте): клиент передаёт туда данные света/каскадов/погоды (`weatherData_t`), шейдеры читают их при биндинге констант. При изменении числа каскадов/регистров нужно синхронно править `deferred_global_common.h`, `cascade_t.*`, C++ биндинг констант и `.fxc` (регистры констант пиксельного шейдера ограничены — проверять пересечения `register(cN)`). Структуру `weatherData_t` менять только с пересборкой **обеих** DLL (client + game_shader_dx9).
- CSM: 2–4 каскада, подогнанных под фрустум камеры (practical split), текстел-снэппинг, дальние каскады обновляются реже (`r_csm_stagger`); пресеты — таблица в `cascade_t.cpp` (`r_csm_quality` 0..5, `r_csm_distance_scale`).
- **c30 (`cLightScale`/`GAMMA_LIGHT_SCALE`) движок между draw-вызовами не восстанавливает**: проходы с каскадными константами (c26–c33) оставляют там мусор. Шейдер, читающий `GAMMA_LIGHT_SCALE`/`LINEAR_LIGHT_SCALE`, должен сам ставить c30 = 1 (так сделано в `defpass_composite.cpp` и `volume_blend.cpp`; без этого пропадал весь объёмный свет).
- **Кэш констант шейдерного API ненадёжен для полноэкранных проходов**: запись, которую он считает повторной (особенно нулевой вектор), пропускается, и шейдер читает значение предыдущего прохода в тех же регистрах. Так god rays при солнце за спиной (c0/c1 = 0) брали позицию камеры и направление света из `volumpass_sun` → ярко-синее небо и синие ореолы по краям геометрии. Во всех HL2RPM-проходах константы пишутся с `bForce`: `SetPixelShaderConstant( reg, v, n, true )` (так же, как уже было в `defpass_shadow.cpp`). В новых проходах делать так же.
- Версия интерфейса — `DeferredExtensionVersion005`; меняя структуры `IDeferredExt.h`, поднимать её и пересобирать client + game_shader_dx9 вместе.
- **Классические лампы → deferred** (сервер, `game/server/deferred/cdeferred_manager_server.cpp`; система добавлена через `IGameSystem::Add` в `gameinterface.cpp` — без этого конвертация не запускалась вообще). Читает entity lump и `LUMP_WORLDLIGHTS` из `.bsp`: vbsp удаляет безымянные `light*`, а текстурные лампы (`emit_surface`) есть только в worldlights. Радиус — по нормировке vrad (I(d)=I0·S/(c+l·d+q·d²), S=c+100l+10000q); у deferred-прожектора конус задаётся **полным** углом, у классического `_cone` — половинным. Тени только у прожекторов и ярких точек (25 ламп с тенями на `demo_map` заметно роняли FPS). Если на карте уже есть `light_deferred`, конвертация пропускается. Именованные лампы сохраняют I/O (у `light_deferred*` входы `TurnOn/TurnOff/Toggle`). Convars `r_deferred_convert_worldlights`, `_surfacelights`, `_max`. На картах с `light_environment` без `light_deferred_global` глобальный свет (а с ним `env_weather` и процедурное небо) создаётся автоматически.
- **Движковые dlight/elight** (вспышки выстрелов, взрывы, огонь) → точечные deferred-источники без теней: `deferred/deferred_dlight_bridge.cpp` (прокси `IVEfx` запоминает ключи elight — перечислить их иначе нельзя), вызывается в начале `CLightingManager::LightSetup`. Convars `r_deferred_dlights`, `_scale`, `_max`, `_debug`.
- **SSAO и видимость неба**: `deferred/deferred_ssao.cpp` + `deferred_ssao_ps30`/`deferred_ssaoblur_ps30` (половинное разрешение, раздельный блюр с учётом глубины). Результат читает `lightingpass_global_ps30` (s6) и гасит **только амбиент**; s7 — карта окклюзии дождя сверху → «видимость неба» (амбиент под крышей × `r_deferred_skyvis_indoor`). Convars `r_deferred_ssao`, `_radius`, `_intensity`, `r_deferred_skyvis*`; пункт SSAO есть в меню «Графика».

## Погода, небо, время суток

- **Сервер** `game/shared/weather/`: `env_weather` (целевой пресет, длительность перехода, авто-смена погоды — марковская цепь с весами по времени суток в `weather_shared.cpp`; создаётся автоматически на картах с `light_deferred_global`, в FGD — `deferred.fgd`). Там же таблица пресетов `WeatherParams_t` (9 обычных + `snow`, `ash` — в автосмене не участвуют, только вручную/из FGD) и `Weather_SunlightColor()` (цвет солнца через атмосферу, те же коэффициенты, что в шейдерах неба). Консоль: `sv_weather <имя|номер> [сек]`, `sv_weather_auto 0/1`, `sv_weather_next`, `sv_weather_list`, `sv_weather_procedural_sky` (заменяет статичный skybox на `sky_proc_atmo_01`).
- **Клиент** `game/client/weather/`: `C_WeatherSystem` плавно смешивает параметры, считает влажность/лужи, молнии/гром, звук дождя/ветра, луну; `ModifyGlobalLight()` (из `CDeferredLightGlobal::GetState`) превращает погоду в свет/амбиент. `weather_render.cpp` — все проходы: облака (`weather_clouds_ps30`, raymarch в 1/2–1/4 разрешения с джиттером → `weather_cloudresolve_ps30`: история репроецируется по повороту камеры и клиппится по окрестности 3×3 — без шлейфов при движении), мокрые поверхности/лужи/высотный туман (`weather_post_ps30`), GPU-струи дождя (`weather_rain_vs30/ps30`), лучи солнца (`volumpass_sun_ps30` + экранные god rays `weather_godrays_ps30` → буфер volumetrics), канал молнии (меш `hl2rpm/weather/lightning_bolt`). 3D-шум облаков и weather map генерируются на CPU (`weather_noise.cpp`).
- **Карта окклюзии дождя** (вид сверху, ortho): рендерится проходом теней с отдельным индексом `DEFERRED_SHADOW_INDEX_RAIN` (на индексе 0 она портила данные солнца) и с `m_bForceNoVis` — иначе PVS камеры отбрасывал крыши/пропы над игроком и пол под ними намокал. Отладка: `r_weather_debug_view 1` (красный = открытое небо, синий = под крышей), `2` — сама карта.
- **Осадки** — штатные частицы HL2 (`weather/weather_precip.cpp`: `rain`, `rain_storm`, `snow`, `ash` + `*_outer`), слои вокруг камеры: CP1 наклонён по ветру, CP3.x = плотность; гроза подмешивается при rain > 0.8. Оператор `Cull relative to Ray Trace Environment` в particles.lib 2013 отсутствует → реализован в клиенте (`C_INIT_HL2RPM_PrecipCull`: трасса вверх, кэш ячеек 24×24×48) — под крышей капли не рождаются. Инициализатор particles.lib вызывается, только если пишет ещё не инициализированный атрибут → нужен `InitMultipleOverride() == true`; частицы обновляются в потоках (`r_threaded_particles`) → кэши под мьютексом. Яркость частиц и детальных спрайтов масштабируется по текущему свету (ночью не светятся). Convars `cl_weather_precip`, `_density`, `r_precip_cull(_debug)`.
- **Молнии**: классы близко/средне/далеко (звуки `sound/hl2rpm/weather/thunder/*`, задержка грома по расстоянию), канал из 2–5 импульсов, видимый разряд у горизонта. Вспышка применяется к глобальному свету **после** временного сглаживания (`r_deferred_light_global_smooth_tau`), иначе съедается. `cl_weather_lightning [0|1|2]` — вызвать удар (близко/средне/далеко; есть кнопка в F1-редакторе).
- Покрытие облаков **откалибровано по реальному шуму** (`CoverageThreshold` в `weather_clouds_ps30.fxc` и копия в `lightingpass_global_ps30.fxc` для теней облаков): coverage = доля закрытого неба. Меняя генерацию шума — перекалибровать (симуляция шума на numpy, подбор порога бисекцией).
- **Небо**: `procsky_atmo_ps20b.fxc` (компилировать `-ver 30` → `procsky_atmo_ps30.vcs`) + LUT атмосферы `sky_atmo_*_lutgen` (Hillaire). Параметризация sky-view LUT: u = sqrt(0.5−0.5·cos(азимут к солнцу)), v = sqrt(угол места) — генератор и выборка должны совпадать. LUT-ы билинейные (point sampling давал «ступеньки»). Небо освещается нетонированным солнцем (`weatherData_t::vecSkyLight`), цвет даёт сама атмосфера.
- **F1-редактор** → «Время суток и погода» (`deferred/vgui/vgui_editor_timecycle.cpp`, команда `r_deferred_timecycle_editor`).
- **Настройки → Графика**: `game/client/hl2rpm_options_graphics.cpp` встраивает страницу в закрытый `OptionsDialog` GameUI (виртуальный `PropertyDialog::AddPage`, ABI vgui_controls совпадает с Valve). Пресеты и управляемые convars — таблицы в начале файла. `cl_options_graphics_page 0` отключает встраивание.
- **Фото-тур для проверки визуала**: `hl2.exe -game ... +cl_weather_tour_map test_deferred` (опции `cl_weather_tour_first/last`, `_exec <cfg>`, `_run 0`) — грузит карту, проходит пресеты/время суток, делает `screenshots/*.jpg`, пишет в `console.log` строки `[tour]` с FPS каждого шага. Свой сценарий: `cl_weather_tour_script <файл в cfg>` — строка `<флаги>|<команды>`, флаги `shot`, `sun`, `spin`, `flash0..2` (молния перед снимком). Скриншоты `jpeg` в меню без карты чёрные — окна VGUI снимать через PrintWindow.
- **Ловушка:** `FCVAR_ARCHIVE`-переменные, переданные в командной строке (`+cl_weather 0`, `+rpm_crash_handler 0` …), сохраняются в `hl2rpm\cfg\config.cfg`. После тестовых запусков проверять и возвращать значения.
- Локализация мода `resource/hl2rpm_<язык>.txt` — **UTF-16LE с BOM** (без BOM движок файл молча игнорирует). Токены погоды/графики — `#HL2RPM_Weather_*`, `#HL2RPM_TCE_*`, `#HL2RPM_Gfx_*`.
- `CFourWheelVehiclePhysics::Initialize` отказывается создавать колёсную машину на модели без аттачментов `wheel_*` (на `map_v11` декоративная `car004a` как `prop_vehicle` взрывала vphysics через ~10 с после загрузки).
