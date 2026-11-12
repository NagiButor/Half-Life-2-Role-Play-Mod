# Маппинг: FGD / Hammer, компиляторы hl2rpm_vbsp / vvis / vrad

> Общая техническая справка для любой AI-модели. Правила — `../PROJECT.md`, статус — `../CURRENT_STATUS.md`. Процедура и скрипты — `../skills/hl2rpm-hammer/`.

## FGD / Hammer

- Hammer++ грузит FGD по порядку: `commentary.fgd`, `deferred.fgd`, `halflife2_vizzys.fgd` (включает `base_vizzys.fgd`), `hl2rpm.fgd`, `hammerplusplus_fgd.fgd`. `commentary`/`deferred` делают `@include "base.fgd"`, который находится не рядом, а в `bin` Hammer'а (fallback по cwd).
- `hl2rpm.fgd` опирается на базовые классы из `halflife2_vizzys.fgd` (`Weapon`, `BaseNPC` и др.). Синтаксическая ошибка в любом раньше загружаемом FGD обрывает его разбор → каскад ошибок «undefined base class» и энтити «obsolete» в Hammer.
- Перед правкой FGD делать копию `*.fgd.bak_YYYYMMDD_...` (так принято в папке). Кастомные правки помечены комментарием `//Pravka`; правка свойств существующей энтити должна быть внутри её `[ ]`, а не отдельным блоком. Проверка синтаксиса: `python hl2rpm_extras/tools/fgdcheck.py <файл.fgd>`.
- У `light_deferred`/`light_deferred_spot` есть ключ `autoshadow` («Automatic shadow», 1 по умолчанию) — см. «Тени ламп».

## Компиляторы карт HL2RPM (hl2rpm_vbsp / vvis / vrad)

Свои vbsp/vvis/vrad — форк компиляторов Mapbase из `sp/src/utils` (2026-10-30). Новый формат карт невозможен (`engine.dll` читает только BSP v20, дерево BSP нужно ему для коллизий, физики, звука, AI и отрисовки), поэтому это те же инструменты, доработанные под открытый мир и deferred.

- **Сборка и установка**: `powershell -File hl2rpm_extras\tools\install_compilers.ps1` (`-NoBuild` — только копирование). Проекты `utils/vbsp/vbsp.vcxproj`, `utils/vvis/vvis_dll.vcxproj`, `utils/vvis_launcher`, `utils/vrad/vrad_dll.vcxproj`, `utils/vrad_launcher` (v143) кладут результат в `sp\game\bin`; скрипт копирует в `Source SDK Base 2013 Singleplayer\bin` как `hl2rpm_vbsp.exe`, `hl2rpm_vvis.exe` + `hl2rpm_vvis_dll.dll`, `hl2rpm_vrad.exe` + `hl2rpm_vrad_dll.dll` — инструменты работают только из `bin` SDK (`steam.dll`, `filesystem_stdio.dll`), лаунчеры Mapbase грузят `<своё имя>_dll.dll` (нет файла — молча берут стоковый `vrad_dll.dll`). Стоковые exe не тронуты.
- **Hammer++** (`bin\hammerplusplus\hammerplusplus_gameconfig.txt`, пути BSP/Vis/Light) смотрит на `hl2rpm_*.exe`; бэкап до переключения — `hammerplusplus_gameconfig.txt.bak_20261030_043414` (откат — вернуть три строки). `compile_map.ps1` навыка `hl2rpm-hammer` по умолчанию тоже на них (`-Tools stock` — Valve).
- **vbsp**: списки треугольников против трещин T-стыков (примитивы, 65536 индексов на карту — `unsigned short` формата) раздаются после всех граней, крупнейшим первыми; не поместившиеся грани — веером с угла, у которого меньше T-стыков (возможна трещина в пиксель, как у `-notjunc`), в логе их число и площадь. Раньше — `Too many t-junctions to fix up!` и обрыв компиляции (`rpm_trainstation_testoptimization`: 3259 таких граней, 3156 поместились). В конце — **бюджет карты**: доля каждого лимита формата, ≥ 80 % — предупреждение (T-стыки, физика и текст сущностей — не жёсткие лимиты).
- **vvis**: быстрый проход порталов по умолчанию (полный — `-full`: на карте без hint/areaportals часами и всё равно пропускает почти всё), радиус видимости — `-radius N` или `farz` `env_fog_controller`. Тонкое отсечение — в игре (Occlusion culling).
- **vrad**: по умолчанию **плоские лайтмапы** (`g_bDeferredLighting`: свет в точках lightmap не собирается, отскоков нет, амбиент `0.4` серого): deferred лайтмапы не читает (`deferred_brush.cpp` ставит лишь флаги формата). Считаются worldlights (из них сервер делает deferred-лампы, в т.ч. текстурные), амбиент листьев (`-fastambient`), свет detail props. `-full` или `-final` — запечённый свет стокового vrad (для `-nodeferred`). `rpm_trainstation_testoptimization`: 42 с на LDR+HDR против ~12 мин у `-ultrafast`.
- **Assert'ы библиотек** (`mathlib.lib` собрана с assert'ами: `color_conversion.cpp` при свете ярче формата lightmap) больше не открывают модальное окно «Assertion Failed» — только в лог, и одинаковый — один раз (`CmdLib_SpewOutputFunc`, `utils/common/cmdlib.cpp`; окно — лишь при подключённом отладчике).
- Время полной компиляции `rpm_trainstation_testoptimization` (15к граней, 9к листьев): vbsp 16 с, vvis 6 с, vrad 2×26 с. Регрессия на `test_deferred`: все счётчики геометрии совпадают со стоковым vbsp.
- Предупреждение `Error! Invalid VTX file checksum ... combine_smallmonitor001.dx80.vtx` — несовпадение файлов модели в моде (не компилятора).
- **Имя карты — не длиннее 31 символа**: в сохранении движок хранит 31 символ имени (`rpm_trainstation_testoptimization`, 33, обрезается до `..._testoptimizati` → «Save file is not valid», загрузка не работает), `engine->GetLevelName()` — 39 с `maps/`. `hl2rpm_vbsp` предупреждает о длинном имени.

## Метки задач для AI-агента в Hammer (2026-11-04)

- Пользователь делает набросок карты вручную и оставляет задачи: точка `rpm_claude_task` (значок «AI») или объём `func_rpm_claude_area`
  (браш-сущность с полупрозрачной оранжевой текстурой `hl2rpm/tools/claude_area`, `%compilenodraw`). Ключи: `targetname` (имя, по нему
  искать), `task` / `task2` / `task3` (текст задания), `kind`, `priority`, `status` (todo / doing / question / done), `reply` (ответ AI-агента).
  FGD - `hl2rpm.fgd` (копия `hl2rpm_extras/fgd`), ассеты - `hl2rpm_extras/tools/make_claude_task_assets.py`.
- `hl2rpm_vbsp` выбрасывает обе сущности (и браши области) при компиляции: в BSP их нет, в логе «HL2RPM: task area '<имя>' ... dropped».
- Читать: `vmf.py tasks <vmf> [--status todo]` (имя, тип, приоритет, точка или коробка области, текст); отвечать:
  `vmf.py reply <vmf> --name N --status doing "текст"`. Hammer пишет кириллицу в cp1251 - `vmf.py` читает обе кодировки и пишет
  ответ в кодировке файла. Hammer не должен держать карту открытой при правке файла (перезапишет при сохранении).
- **Увидеть результат без компиляции**: `vmf_shot.py <vmf> <out>` навыка (виды сверху/спереди/сбоку/3D + задачи + info.json) и CK,
  режим «Геометрия из VMF» (см. ck-editor.md); окно самого Hammer++ — `hammer_shot.py <png>`.

Имена с `claude` — совместимые идентификаторы существующих VMF/FGD/материалов и скриптов, а не ограничение по модели. Любой агент читает и заполняет эти задачи теми же командами.

## City-26: regularized scan greybox — 2026-11-12

- Новый `city26_test`: VMF в `mapsrc/test`, BSP в `hl2rpm/testmaps/maps`. Снимок исходника — `hl2rpm_extras/mapsrc/city26_test.vmf`; генератор/JSON/проверки — `hl2rpm_extras/tools/city26/README.md`. Перед изменением сравнить рабочую VMF со снимком: пользователь может продолжить её вручную.
- Исходник `E:\Google Map 3D Exporter v3.0\city-26-prototype.fbx` содержит 3 мировых меша после пользовательского размещения и scale около 1/256. FBX-проекции — только референс: автоматический threshold крыш сливает кроны и кварталы; в BSP пошли **вручную упрощённые** контуры `layout.json`, а не шумные контуры автоматического анализа.
- Нельзя выдавать выбранный коэффициент 1024 Hammer units на FBX-world unit за реальную метрическую привязку. XY сохраняет общую компоновку; высоты улиц/зданий нормализованы. Город без интерьеров: объёмы зданий сплошные, дворы открыты к небу, ворота — наружные проходы под перемычкой.
- Свежая геометрия строится выпуклыми призмами: constrained Delaunay + объединение выпуклых соседей для невыпуклых контуров/колец. Vertex XY округляются до 1 unit, плоскости скатов аналитические; проверять объём **по пересечению полупространств**, не только по трём точкам VMF-плоскостей.
- Непрерывные world-ground/sky герметизируют карту; детали крыши/тротуары/карнизы — func_detail. Water только верхний материал `hl2rpm/water/murky`, боковины/низ nodraw. Все water-объёмы на одном z, под ними world-дно. Лестницы 12-unit riser, около 24-unit tread.
- `validate_vmf.py` проверяет каждую выпуклую brush, `probe_bsp.py` проверяет реальные solid/water contents **после компиляции**, включая detail из leafbrush lists (не только дерево nodes). В BSP leaf v1 — 32 байта, старый — 56; brushsides lump 19, brushes 18, leafbrushes 17.
- `compile_map.ps1`/компиляторы **дописывают** `<map>.log`: счётчики первой компиляции не относятся к последней версии. Проверять последний `Map budget` или читать lumps самого BSP.
- Playtest tour каждый шаг переустанавливает углы по умолчанию, если в нём нет `setang`. При тесте ходьбы **каждый** шаг с `+forward`/`-forward` должен явно задавать направление; `setpos/getpos` требует паузы до проверки. Глаз игрока примерно на 64 выше опоры; итоговое z=192 подтверждает улицу z=128. Телепорт в соседнюю стену не доказывает непроходимость ворот: ставить старт внутри открытого прохода с запасом для 32-unit hull.

## ck_test как двор City 17 — 2026-11-12

- Запрос 4 ноября (п.5): `ck_test` для ролика — лаконичные постройки City 17 вместо голых стен, пол — серая dev-плитка (`DEV/DEV_MEASUREGENERIC01B`, уже стояла). Сделано генератором `hl2rpm_extras/tools/make_ck_city17.py`: он **правит существующую** `mapsrc/test/ck_test.vmf` (бэкап `.bak_*` от vmf.py), всё своё кладёт в visgroup `ck_city17` и при повторном запуске заменяет только это. Внутри двора (±1536) ничего не строится — NPC, маркеры, предметы, патрули и 569 узлов навигации CK на месте.
- Вокруг двора — дома 2–5 этажей до ±2048 (новая sky-оболочка, потолок 1536), фасады — штатные текстуры `building_template` HL2 (масштаб 0.25: тайл 512 px = этаж 128 ед.), каждый этаж — свой браш с выравниванием по этажу; цоколь, межэтажный пояс, карниз, балконы — `func_detail`. Двери с козырьком и лампой, уличные фонари (`lamppost03a_on` + `light_spot` вниз), пост Альянса в юго-западном углу (зона «Пост Надзора 17»), выгоревший дом с завалами у пустыря (северо-восток), тупиковый проулок с решёткой на востоке. Внешние стены домика смотрителя — камень с окнами (`003D`/`003A`), козырёк и лампа над дверью; внутри без изменений.
- **Узлы навигации CK стоят в 20 ед. от края двора**: ничего не выдвигать в двор ниже 72 ед. (выступ цоколя есть только у домов с отступом), фонари/баррикады ставить с проверкой по `scripts/world/ck_test_nav.txt`.
- Просмотр текстур HL2 без распаковки: `hl2rpm_extras/tools/vpktex.py` (`list`, `png`, `sheet` — лист VTF с подписями; `mdlbox` — габариты модели из заголовка `.mdl`).
- `hl2rpm/maps/ck_test.bsp` (компиляция Hammer++ 8 ноября) затенял `testmaps/maps/ck_test.bsp`; сохранён в `map_backups/ck_test_maps_20261108.bsp`, в `maps/` и `testmaps/maps/` теперь одинаковая новая сборка.
