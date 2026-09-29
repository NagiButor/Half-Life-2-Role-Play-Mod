# hl2rpm_extras

Файлы проекта, которые живут вне `sp/src`, но должны храниться в git.
Здесь лежат **копии**; рабочие файлы остаются на своих местах — после правки
там скопировать сюда и закоммитить.

| Папка | Рабочее расположение |
|---|---|
| `fgd/` | `E:\XBLAH's modding tool\tools\FGDs\Mapbase` (Hammer++ грузит их оттуда) |
| `crashtools/` | `E:\Steam\steamapps\sourcemods\hl2rpm\crashtools` |
| `resource/` | `E:\Steam\steamapps\sourcemods\hl2rpm\resource` — локализация мода (`hl2rpm_<язык>.txt`, UTF-16LE **с BOM**) и `gamemenu.res` |
| `tools/` | скрипты-генераторы; `generate_weather_sounds.py <папка>` создаёт петли дождя/ветра для `sound\hl2rpm\weather\` |

Собранные DLL/PDB, шейдеры `.vcs` мода, карты и прочие ресурсы в git не кладутся —
они публикуются архивами в GitHub Releases.
