# Half-Life 2: Role-Playing Mod (HL2RPM)

Однопользовательский RPG-мод для Half-Life 2 на **Source SDK 2013 / Mapbase v8.0**
(конфигурация `episodic`, Win32). В репозитории — исходники игровых DLL и шейдеров
(`sp/src` оригинального SDK) плюс служебные файлы мода: FGD для Hammer, локализация,
инструменты и документация.

> Собранные DLL, карты, модели и прочие ресурсы в git не хранятся — они публикуются
> архивами в [Releases](../../releases).

## Что есть в моде

### RPG-системы

- **Диалоги с NPC** — текстовые файлы `scripts/dialogs/*.txt` с ветвлением реплик,
  запуск по USE, триггером `trigger_dialog_start` или из консоли.
- **Квесты, репутация, фракции** — квестовые действия прямо из реплик, варианты ответа
  с условиями (например, по репутации).
- **Инвентарь и лут** — предметы и оружие через инвентарь, обыск трупов NPC, контейнеры
  `func_loot_container`.
- **Переходы между картами** с переносом времени суток, погоды, инвентаря и квестов.

Код: `game/server/dialog*`, `quest_system.*`, `inventory_system.*`, `loot_*`;
интерфейс — `game/client/vgui_dialogpanel.*`, `vgui_inventorypanel.*`.

### Deferred-рендер

Поверх Mapbase встроен deferred-рендер (light pre-pass, порт Biohazard90 deferred):

- **Тени солнца** — 2–4 каскада (CSM) с подгонкой под фрустум, 24-битная глубина, фильтр
  кубическим B-сплайном, мягкая полутень PCSS, контактные экранные тени.
- **Лампы** — точечные и прожекторные с тенями, бюджет теней по расстоянию, отражённый свет
  ламп; классические `light`/`light_spot` конвертируются в deferred при загрузке карты;
  вспышки выстрелов и взрывы (dlight) тоже попадают в deferred.
- **Непрямой свет (GI-пробы)** — сетка проб вокруг камеры: в закрытых помещениях темно,
  свет из окон и проёмов мягко растекается по комнате.
- **SSAO и видимость неба** — затенение амбиента в углах и под навесами.
- **Вода и зеркала** — отражение и преломление, прозрачность по глубине, пена и каустика,
  отражающее стекло `func_reflective_glass`.
- **Сглаживание** — TAA (по умолчанию) или FXAA.
- **Пост-обработка** — адаптация экспозиции, блум с грязью объектива, блик солнца,
  цветокоррекция, виньетка, зерно, хроматическая аберрация, резкость, глубина резкости
  (при прицеливании и в диалогах).
- **Частицы** — мягкие частицы (дым и пар без жёсткой линии у пола), пылинки в лучах света.

### Погода, небо, время суток

- **Процедурное небо** — атмосфера по LUT (Hillaire), солнце, луна и звёзды, смена дня и ночи.
- **Объёмные облака** — raymarch с временным накоплением, тени облаков на земле.
- **14 пресетов погоды** — от ясной до грозы, туман, морось, снег, пепел; плавные переходы
  и автоматическая смена погоды с учётом времени суток.
- **Осадки** — GPU-дождь, снег и пепел с картой крыш (под навесом сухо), брызги в точках удара.
- **Мокрые поверхности и лужи** с отражениями (SSR), высотный туман, лучи солнца.
- **Гроза** — молнии трёх дистанций, гром с задержкой по расстоянию.

### Инструменты

- **Меню «Настройки → Графика / Эффекты»** — пресеты качества и отдельные переключатели.
- **F1-редактор** — время суток и погода; редактор ламп (`r_deferred_light_editor_toggle`).
- **Отладчик крашей** — минидамп, текстовый отчёт и автоматический разбор через `cdb`
  (`hl2rpm\crashes`, команды `rpm_crash_*`).
- **Фото-тур** — автоматический прогон карты по пресетам погоды со скриншотами и FPS.

## Структура репозитория

| Путь | Что там |
|---|---|
| `game/client`, `game/server`, `game/shared` | игровой код (client.dll / server.dll) |
| `game/client/deferred`, `game/client/weather` | deferred-рендер и погода (клиент) |
| `game/shared/weather` | пресеты погоды, `env_weather` |
| `materialsystem/stdshaders` | шейдеры: `.fxc`, C++ обёртки, `game_shader_dx9.dll` |
| `game/ShaderCompile235` | компилятор шейдеров (без perl и DirectX SDK) |
| `hl2rpm_extras/` | копии FGD, crashtools, локализации, навыков Claude Code и скрипты — см. [README](hl2rpm_extras/README.md) |
| `CLAUDE.md` | подробная техническая документация: архитектура, convars, найденные ловушки |

## Сборка

Нужна Visual Studio 2022 (тулсет v143, платформа Win32). Проекты `.vcxproj` правятся вручную —
**VPC не запускать** (`create*projects.bat` затрёт правки).

```powershell
$msb = "<VS2022>\MSBuild\Current\Bin\MSBuild.exe"
& $msb "game\client\client_episodic.vcxproj" /p:Configuration=Release /p:Platform=Win32 /m
& $msb "game\server\server_episodic.vcxproj" /p:Configuration=Release /p:Platform=Win32 /m
& $msb "materialsystem\stdshaders\game_shader_dx9_episodic.vcxproj" /p:Configuration=Release /p:Platform=Win32 /m
```

PostBuild копирует DLL и PDB в `<Steam>\steamapps\sourcemods\hl2rpm\bin`.

Шейдеры собираются **только** `ShaderCompile235` и только нужные (списки —
`materialsystem/stdshaders/hl2rpm_custom.txt`, `deferred_shaders.txt`):

```powershell
cd materialsystem\stdshaders
..\..\game\ShaderCompile235\bin\ShaderCompile.exe -ver 30 -shaderpath . lightingpass_global_ps30.fxc
python check_shader_combos.py   # .vcs и C++ должны совпадать: 0 mismatched
```

Готовые `.vcs` из `stdshaders\shaders\fxc` копируются в `hl2rpm\shaders\fxc`.

## Установка мода

1. Steam: **Source SDK Base 2013 Singleplayer** (appid 243730), Half-Life 2, Episode One, Episode Two.
2. В `steamapps\sourcemods` — папки Mapbase (`mapbase_shared`, `mapbase_hl2`, `mapbase_episodic`).
3. Распаковать архив `hl2rpm-vX.Y.zip` из Releases в `steamapps\sourcemods\hl2rpm`, перезапустить Steam.

Исходники карт (`.vmf`) и FGD для Hammer++ — архивы `hl2rpm-mapsrc-*.zip` и `hl2rpm-fgds-*.zip`
там же; порядок загрузки FGD описан в `CLAUDE.md`.

## Основные консольные переменные

| Переменная | Назначение |
|---|---|
| `r_csm_quality` 0..5 | качество каскадных теней солнца |
| `r_csm_pcss` | мягкие тени солнца |
| `r_deferred_aa` 0 / 1 / 2 | сглаживание: нет / FXAA / TAA |
| `r_deferred_gi` | непрямой свет (GI-пробы) |
| `r_deferred_ssao`, `r_deferred_skyvis` | SSAO и видимость неба |
| `r_deferred_contact_shadows` | контактные тени |
| `r_deferred_water_views` | отражения и преломление воды |
| `r_hl2rpm_post`, `r_hl2rpm_bloom`, `r_hl2rpm_dof`, `r_hl2rpm_flare` | пост-обработка |
| `r_deferred_soft_particles`, `r_weather_motes` | мягкие частицы, пылинки |
| `sv_weather <имя> [сек]`, `sv_weather_auto`, `sv_weather_list` | погода |
| `sv_timecycle_speed_scale` | скорость игрового времени |
| `r_deferred_timecycle_editor` | редактор времени суток и погоды (F1) |

Запуск без deferred (сток Mapbase): параметр `-nodeferred`.

## Производительность

Ориентир — ноутбук с GTX 960M, 1600×900: открытая карта со всеми эффектами ~55–60 FPS.
Стоимость отдельных эффектов и способы измерения — в `CLAUDE.md`.

## Благодарности

- Valve — Source SDK 2013
- [Mapbase](https://github.com/mapbase-source/source-sdk-2013) — база мода
- Biohazard90 — Alien Swarm Deferred, на котором основан deferred-рендер мода
- Sébastien Hillaire — модель атмосферы для процедурного неба
- Timothy Lottes — FXAA 3.11

Код SDK распространяется по лицензии Source 1 SDK; ресурсы Half-Life 2 принадлежат Valve.
