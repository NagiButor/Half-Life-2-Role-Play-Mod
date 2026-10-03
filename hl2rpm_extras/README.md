# hl2rpm_extras

Файлы проекта, которые живут вне `sp/src`, но должны храниться в git.
Здесь лежат **копии**; рабочие файлы остаются на своих местах — после правки
там скопировать сюда и закоммитить.

| Папка | Рабочее расположение |
|---|---|
| `fgd/` | `E:\XBLAH's modding tool\tools\FGDs\Mapbase` (Hammer++ грузит их оттуда) |
| `crashtools/` | `E:\Steam\steamapps\sourcemods\hl2rpm\crashtools` |
| `resource/` | `E:\Steam\steamapps\sourcemods\hl2rpm\resource` — локализация мода (`hl2rpm_<язык>.txt`, UTF-16LE **с BOM**) и `gamemenu.res` |
| `tools/` | скрипты-генераторы и dev-утилиты (оригиналы, рабочих копий нет) — см. ниже |
| `claude_skills/` | `E:\SourceModding\HL2RPMSOURCE\.claude\skills` — навыки Claude Code: `hl2rpm-hammer` (VMF/BSP, компиляция карт), `hl2rpm-playtest` (запуск и проверка мода), `hl2rpm-dialogs` (диалоги, квесты, лут) |

### tools/

| Скрипт | Что делает |
|---|---|
| `generate_weather_sounds.py <папка>` | синтезирует петли дождя/ветра для `sound\hl2rpm\weather\` |
| `make_thunder.py` | режет записи грома из `E:\SourceModding\ProceduralWeatherAndSkyPromt\lighting sounds` (нужен ffmpeg) в `sound\hl2rpm\weather\thunder\close_*/mid_*/far_*.wav` |
| `make_lightning_tex.py` | генерирует `materials\hl2rpm\weather\lightning_glow.vtf` + `lightning_bolt.vmt` (канал молнии) |
| `add_loc_tokens.py` | вписывает токены `#HL2RPM_*` в `resource\hl2rpm_<язык>.txt` мода (UTF-16LE с BOM, делает `.bak_`) — после запуска скопировать файлы в `resource/` здесь |
| `fgdcheck.py <fgd>...` | упрощённая проверка FGD в порядке загрузки Hammer (скобки, неизвестные base-классы) |
| `run_tour.ps1 -Map <карта> -Extra "<+команды>"` | запускает фото-тур погоды, выводит скриншоты и важные строки `console.log` |
| `dmx_dump.py <file.pcf> [фильтр]` | дамп систем частиц из бинарного `.pcf` (операторы и их параметры) |
| `dmx.py` | чтение/запись бинарного DMX (`.pcf`) — модуль для скриптов ниже |
| `make_weather_pcf.py` | собирает `particles\hl2rpm_weather.pcf` мода (дождь, ливень, снег, пепел с операторами `HL2RPM Precip *`) из `rain_fx.pcf` (mapbase_shared VPK); параметры систем — в начале скрипта |
| `flicker_stats.py <папка jpg> <out.png>` | покадровая разница серии скриншотов/кадров `startmovie` — поиск мерцания |
| `crawl_stats.py <папка> <префикс> <out.png>` | вторая разность по времени (≈0 при плавном движении) — «ёрзанье» теней при ходе солнца, «песок» на облаках; кадры писать в `tga` |
| `vpk_list.py <dir.vpk> [фильтр] [--extract папка]` | список/распаковка файлов из VPK |

Собранные DLL/PDB, шейдеры `.vcs` мода, карты и прочие ресурсы в git не кладутся —
они публикуются архивами в GitHub Releases.
