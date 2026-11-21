# Half-Life 2: Role-Playing Mod (HL2RPM)

Однопользовательский RPG-мод для Half-Life 2 на **Source SDK 2013 / Mapbase v8.0**
(конфигурация `episodic`, Win32). В репозитории — исходники игровых DLL и шейдеров
(`sp/src` оригинального SDK) плюс служебные файлы мода: FGD для Hammer, локализация,
инструменты и документация.

> Собранные DLL, карты, модели и прочие ресурсы в git не хранятся — они публикуются
> архивами в [Releases](../../releases).

## Что есть в моде

### RPG-системы и Creation Kit

- **Диалоги с NPC** — текстовые файлы `scripts/dialogs/*.txt` с ветвлением реплик,
  запуск по USE, триггером `trigger_dialog_start` или из консоли; озвучка и липсинк.
- **Квесты, репутация, фракции** — квестовые действия прямо из реплик, варианты ответа
  с условиями (например, по репутации), маркеры целей на HUD.
- **Инвентарь, торговля, терминалы и лут** — предметы, оружие, 3D-карточки в интерфейсе,
  обыск трупов NPC, контейнеры `func_loot_container`, замки и взлом терминалов.
- **Слой мира и распорядок NPC** — размещение NPC и маркеров, суточные пакеты ИИ.
- **Переходы между картами** с переносом времени суток, погоды, инвентаря и квестов.
- **HL2RPM Creation Kit** (`hl2rpm_extras/tools/dialog_editor`) — внешний редактор диалогов,
  квестов, предметов, фракций и слоя мира на Python / PySide6 / OpenGL.

Код: `game/server/dialog*`, `quest_system.*`, `inventory_system.*`, `loot_*`;
интерфейс — `game/client/hl2rpm_*`, `vgui_dialogpanel.*`, `vgui_inventorypanel.*`.

### Deferred-рендер

Поверх Mapbase встроен deferred-рендер (light pre-pass, порт Biohazard90 deferred) с
возможностью переключения в классический **Forward** прямо в настройках графики:

- **Тени солнца** — 2–4 каскада (CSM) с подгонкой под фрустум, 24-битная глубина, фильтр
  кубическим B-сплайном, мягкая полутень PCSS, контактные экранные тени.
- **Лампы** — точечные и прожекторные с тенями, бюджет теней по расстоянию, отражённый свет
  ламп, карта досягаемости света (Light Reach); классические `light`/`light_spot`
  конвертируются в deferred при загрузке карты; вспышки выстрелов и взрывы (dlight) тоже
  попадают в deferred.
- **SSAO и видимость неба** — затенение амбиента в углах и под навесами.
- **Вода и зеркала** — отражение и преломление, прозрачность по глубине, пена, каустика,
  текучая вода (flowmap), отражающее стекло `func_reflective_glass`.
- **PBR и рельеф (POM)** — материалы моделей и мира с параллакс-окклюзией.
- **Сглаживание** — FXAA и масштабирование рендера (`r_hl2rpm_renderscale`).
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
| `hl2rpm_extras/` | копии FGD, crashtools, локализации и инструменты — см. [справочник](AI_instructions/references/hl2rpm-extras.md) |
| `AI_instructions/` | общие инструкции для любых AI-моделей: [проект и сборка](AI_instructions/PROJECT.md), [текущий статус](AI_instructions/CURRENT_STATUS.md), темы, skills и история |
| `AGENTS.md` | основной вход AI-агента; `CLAUDE.md`, `GEMINI.md` и Copilot-вход ведут к тем же знаниям |

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
3. Распаковать архив `hl2rpm-vX.Y.zip` из Releases в `steamapps\sourcemods` (в архиве уже лежит
   папка `hl2rpm`), перезапустить Steam.

Исходники карт (`.vmf`) и FGD для Hammer++ — архивы `hl2rpm-mapsrc-*.zip` и `hl2rpm-fgds-*.zip`
там же; порядок загрузки FGD описан в `AI_instructions/topics/mapping.md`.

## Основные консольные переменные

| Переменная | Назначение |
|---|---|
| `r_deferred` 0 / 1 | режим рендера: Forward / Deferred (после перезапуска) |
| `r_csm_quality` 0..5 | качество каскадных теней солнца |
| `r_csm_pcss` | мягкие тени солнца |
| `r_deferred_aa` 0 / 1 | сглаживание: нет / FXAA |
| `r_deferred_ssao`, `r_deferred_skyvis` | SSAO и видимость неба |
| `r_deferred_contact_shadows` | контактные тени |
| `r_deferred_water_views` | отражения и преломление воды |
| `r_hl2rpm_post`, `r_hl2rpm_bloom`, `r_hl2rpm_dof`, `r_hl2rpm_flare` | пост-обработка |
| `r_hl2rpm_renderscale` | масштаб разрешения рендера (0.5–1.0) |
| `cl_rpm_fps` 0 / 1 | счётчик FPS и времени кадра в правом верхнем углу |
| `r_deferred_soft_particles`, `r_weather_motes` | мягкие частицы, пылинки |
| `sv_weather <имя> [сек]`, `sv_weather_auto`, `sv_weather_list` | погода |
| `sv_timecycle_speed_scale` | скорость игрового времени |
| `r_deferred_timecycle_editor` | редактор времени суток и погоды (F1) |

Запуск без deferred (сток Mapbase): параметр `-nodeferred`.

## Производительность

Ориентир — ноутбук с GTX 960M, 1600×900: открытая карта со всеми эффектами ~55–60 FPS.
Стоимость отдельных эффектов и способы измерения — в `AI_instructions/topics/renderer-gi-post.md`
и `AI_instructions/skills/hl2rpm-playtest/SKILL.md`.

## Благодарности

- Valve — Source SDK 2013
- [Mapbase](https://github.com/mapbase-source/source-sdk-2013) — база мода
- Biohazard90 — Alien Swarm Deferred, на котором основан deferred-рендер мода
- Sébastien Hillaire — модель атмосферы для процедурного неба
- Timothy Lottes — FXAA 3.11

Код SDK распространяется по лицензии Source 1 SDK; ресурсы Half-Life 2 принадлежат Valve.
