# hl2rpm_extras — внешние файлы и инструменты в git

> Канонический справочник перенесён из `hl2rpm_extras/README.md` в общую базу `AI_instructions/references/`. Рабочие файлы мода остаются на своих местах — после правки там скопировать в `sp/src/hl2rpm_extras/` и закоммитить по запросу пользователя.

| Папка | Рабочее расположение |
|---|---|
| `fgd/` | `E:\SourceModding\HL2RPMSOURCE\Mapping\fgd` (Hammer++ грузит их оттуда) |
| `crashtools/` | `E:\Steam\steamapps\sourcemods\hl2rpm\crashtools` |
| `resource/` | `E:\Steam\steamapps\sourcemods\hl2rpm\resource` — локализация мода (`hl2rpm_<язык>.txt`, UTF-16LE **с BOM**), `gamemenu.res`, `clientscheme.res` (шрифты `RPMUI_*` окон M6 — `game/client/hl2rpm_ui.*`) |
| `scripts/` | `E:\Steam\steamapps\sourcemods\hl2rpm\scripts` — `hl2rpm_pbr.txt` (из чего сделаны материалы, PBR), `hl2rpm_relief.txt` (исключения рельефа материалов без карты нормалей), `HudLayout.res` |
| `materials/` | `E:\Steam\steamapps\sourcemods\hl2rpm\materials` — текстовые материалы мода: `hl2rpm/water/*.vmt` (типы воды: чистая, мутная, болотная, морская, радиоактивная — `$water_absorb`/`$water_glow`, `stdshaders/water.cpp`) |
| `tools/` | скрипты-генераторы и dev-утилиты (оригиналы, рабочих копий нет) — см. ниже |
| `imported/` | отчёты `import_gma.py`: что из каждого аддона Garry's Mod легло в мод, что пропущено и почему |
| `art/` | исходные картинки пользователя и галерея `README.md`: `readme/*.jpg` (чистые игровые скриншоты для GitHub), `lambda_graffiti_src.jpg` (лямбда-графити меню и Steam; «прозрачность» нарисована шахматкой — её вырезает `make_menu_assets.py`), `icon_src.jpg` (значок мода), `loading_valve_ref.png` (референс вентиля экрана загрузки) |
| `../skills/` | канонические процедуры для любой AI-модели: `hl2rpm-hammer`, `hl2rpm-playtest`, `hl2rpm-dialogs`, `hl2rpm-voice`. Клиентские `.claude/skills` и `.agents/skills` — локальные junction на [../skills](../skills) |

## tools/

| Скрипт | Что делает |
|---|---|
| `world/` | инструменты планирования карты мира Сити-27 (`world_layout.json`, `rebuild_source_3d.py`, `world_check.py`) — см. [tools-world-planner.md](tools-world-planner.md) |
| `city27/` | ранний greybox-прототип `city27_test` (`validate_vmf.py`, `probe_bsp.py`, `brushes.py`) — см. [tools-city27-prototype.md](tools-city27-prototype.md) |
| `generate_weather_sounds.py <папка>` | синтезирует петли дождя/ветра для `sound\hl2rpm\weather\` |
| `make_thunder.py` | режет записи грома из `E:\SourceModding\ProceduralWeatherAndSkyPromt\lighting sounds` (нужен ffmpeg) в `sound\hl2rpm\weather\thunder\close_*/mid_*/far_*.wav` |
| `make_lightning_tex.py` | генерирует `materials\hl2rpm\weather\lightning_glow.vtf` + `lightning_bolt.vmt` (канал молнии) |
| `make_vitals_assets.py [--seed N] [--preview]` | ассеты «раненого» взгляда без костюма (`hl2rpm_vitals.cpp`, `hl2rpm_lowhealth_ps30`): `materials\hl2rpm\effects\lowhealth_veins.vtf` и `sound\hl2rpm\player\heartbeat_once.wav` |
| `add_loc_tokens.py` | вписывает токены `#HL2RPM_*` в `resource\hl2rpm_<язык>.txt` мода (UTF-16LE с BOM, делает `.bak_`) — после запуска скопировать файлы в `resource/` |
| `fgdcheck.py <fgd>...` | упрощённая проверка FGD в порядке загрузки Hammer (скобки, неизвестные base-классы) |
| `install_compilers.ps1 [-NoBuild]` | собирает компиляторы карт мода (форк vbsp/vvis/vrad Mapbase в `utils/`) и ставит их в `bin` SDK как `hl2rpm_vbsp/vvis/vrad.exe` (+ `hl2rpm_*_dll.dll`) |
| `run_tour.ps1 -Map <карта> -Extra "<+команды>"` | запускает фото-тур погоды, выводит скриншоты и важные строки `console.log` |
| `dmx_dump.py <file.pcf> [фильтр]` | дамп систем частиц из бинарного `.pcf` (операторы и их параметры) |
| `dmx.py` | чтение/запись бинарного DMX (`.pcf`) |
| `make_weather_pcf.py` | собирает `particles\hl2rpm_weather.pcf` мода из `rain_fx.pcf` (mapbase_shared VPK) |
| `flicker_stats.py <папка jpg> <out.png>` | покадровая разница серии скриншотов/кадров `startmovie` — поиск мерцания |
| `crawl_stats.py <папка> <префикс> <out.png>` | вторая разность по времени (≈0 при плавном движении) — поиск «ёрзанья» теней и облаков; кадры писать в `tga` |
| `vpk_list.py <dir.vpk> [фильтр] [--extract папка]` | список/распаковка файлов из VPK |
| `make_gi_test.py <out.vmf>` | генерирует тестовую карту `gi_test` для непрямого света |
| `pom/` | исследование рельефа (POM) из текстур без карт нормалей (`vtf.py`, `relief_proto*.py`, `relief_sheet.py`, `relief_dump.py`) |
| `make_water_test.py <out.vmf>` | генерирует тестовую карту `water_test` с бассейнами стоковых и новых типов воды |
| `item_models/make_suit_piles.py` | сложенные костюмы-предметы «как в New Vegas / Сталкере» → `hl2rpm\models\hl2rpm\items` |
| `item_models/make_item_models.py` | модели мелких предметов (шпилька, ключи), сделанные кодом в Blender → `modelsrc\hl2rpm_items` |
| `hd_textures.py` | HD-версии текстур (Real-ESRGAN + перенос деталей, свой DXT/VTF) |
| `make_menu_assets.py` | ресурсы главного меню и паузы (`materials/vgui/hl2rpm/menu/`) |
| `make_loading_valve.py` | вентиль экрана загрузки (Blender → `materials/vgui/hl2rpm/menu/loading_valve`) |
| `make_menu_music.py` | синтез трека меню (`sound/music/hl2rpm/menu/hl2rpm_menu_theme.mp3`, нужен ffmpeg) |
| `make_steam_assets.py [--grid]` | значок мода и обложки Steam → `E:\SourceModding\HL2RPMSOURCE\steam_assets` |
| `import_gma.py <файл.gma> [--only <префикс>] [--force]` | распаковка аддона Garry's Mod в мод (VTF 7.5 → 7.4, пропуск MDL v49), отчёт в `imported/` |
| `mdl_decompile.py` | разбор MDL/VVD/VTX/PHY → SMD + QC и `compile_qc()` |
| `make_characters.py [имена]` | персонажи и пропы из аддонов под SDK 2013 (`models/hl2rpm/characters/`) |
| `make_hands.py` | руки от первого лица, вырезанные из тела костюма (`models/hl2rpm/hands/`) |
| `make_vm_hands.py` | перекраска рук стокового оружия HL2 под костюм |
| `make_zombie_hazmat.py` | зомби в химзащите (`models/hl2rpm/characters/zombie_hazmat.mdl`) |
| `gen_image.py "запрос" -o out.png` | генерация изображений через Google Gemini (ключ `GEMINI_API_KEY`) |
| `make_melee_weapons.py` | оружие ближнего боя из HL2 Melee Pack |
| `make_beretta.py` | Беретта 92 (`models/weapons/hl2rpm/{v,w}_beretta.mdl`) |
| `anims/make_anims.py` | библиотека анимаций NPC и игрока (`models/hl2rpm/anims/rpm_<скелет>_anims.mdl`, `actbusy.txt`) — см. [../topics/animation.md](../topics/animation.md) |

Собранные DLL/PDB, шейдеры `.vcs` мода, карты и прочие ресурсы в git не кладутся — они публикуются архивами в GitHub Releases.
