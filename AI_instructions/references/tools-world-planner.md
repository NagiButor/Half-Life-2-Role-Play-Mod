# Инструменты планирования мира City-27 (tools/world)

> Канонический справочник перенесён из `hl2rpm_extras/tools/world/README.md` в общую базу `AI_instructions/references/`.

## Текущая схема мира: геометрически сжатый 3D-мастерплан (v3)

Канонические данные: **`hl2rpm_extras/tools/world/world_layout.json` v3**. Актуальный обзорный результат этапа 1 — 3D-сцена и спецификация Hammer в `hl2rpm_map_developing/source_3d/`.

Сборка и проверка 3D-мастерплана (из `sp/src`):

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' `
  -b --python "$PWD/hl2rpm_extras/tools/world/rebuild_source_3d.py"
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' `
  -b 'E:\SourceModding\HL2RPMSOURCE\hl2rpm_map_developing\source_3d\city27_masterplan.blend' `
  --python "$PWD/hl2rpm_extras/tools/world/verify_source_3d.py"
python -X utf8 hl2rpm_extras/tools/world/world_check.py
```

*(Примечание: на текущей машине также доступен Blender 5.3 через `blender-launcher.exe` и Blender 4.4.3 в каталоге плагина карт.)*

Сцена создаётся как **новый** файл; она не открывает и не перезаписывает пользовательский `hl2rpm_map.blend`.

Основные результаты в `hl2rpm_map_developing/source_3d/`:
- `city27_masterplan.blend` — полная 3D-сцена, по одной коллекции на каждую планируемую Source BSP;
- `city27_masterplan_top.png`, `city27_masterplan_iso.png` — проверочные рендеры;
- `city27_hammer_layout.json` — локальные origin, границы в HU и парные переходы;
- `city27_hammer_build_spec.md` — рабочая спецификация на русском языке;
- `city27_masterplan_verification.json` — отчёт перепроверки геометрии.

## Ограничения и принципы

- Координаты Source: `−16384…+16384 HU`; масштаб `52.4934 HU/м`.
- Игровые клетки ограничены **600 × 600 м**, оставляя запас до краёв для оболочки и деталей переходов.
- В каждую VMF переносятся локальные координаты (вычитая `local_origin_world_m`).
- Для этапа 2 (строительство конкретной локации поочерёдно по запросу пользователя) опираться на [../topics/mapping.md](../topics/mapping.md) и [../skills/hl2rpm-hammer/SKILL.md](../skills/hl2rpm-hammer/SKILL.md).

## Прежний 2D-атлас и инспекция сканов

- `render_world_atlas.py` строит подробный OSM-атлас (справочный инвентарь, **не** схема копирования 1:1).
- `city27_plan.py` / `render_master.py` — устаревший ошибочный радиальный набросок, не использовать.
- `inspect_blender.py` делает несохраняющий ортографический осмотр сканов в `hl2rpm_map.blend`.
- Кэш OSM JSON находится в `hl2rpm_map_developing/osm_world` (© OpenStreetMap contributors, ODbL 1.0); не перекачивать без необходимости.
