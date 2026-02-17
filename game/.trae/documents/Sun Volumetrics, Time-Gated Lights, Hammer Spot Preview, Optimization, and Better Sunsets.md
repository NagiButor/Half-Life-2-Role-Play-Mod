## Что сейчас происходит (коротко)
- **Солнце “volumetric как у spot”** не выйдет текущим screen-space пассом: он даёт 2D glow и может быть дорогим/непохожим.
- **FPS 200→30** почти наверняка из-за того, что мой sun-пасс включает лишние проходы/блюр каждый кадр и/или делает лишнюю работу при попадании солнца в кадр.
- **Light_deferred time-gate** всё ещё стартует включённым, потому что состояние может уйти в первый снапшот до того, как логика time-gate успела принудительно переопределить spawnflags.
- **Резкий «переключатель» неба** идёт из `env_timecycle::UpdateSun()`: вне окна [sunrise..sunset] высота солнца жёстко ставится в `-8°`, что даёт скачок и визуальный “рубильник”.
- **Hammer превью spot** нельзя условно переключить по `light_type` внутри одного classname — нужен второй classname для spot-варианта (FGD выбирает превью по classname).

## 1) Солнце: сделать volumetric как у light_deferred spot
1. **Убрать/выключить текущий screen-space SUN_GODRAYS**, чтобы вернуть FPS и избежать “не того” эффекта.
2. **Сделать солнце как синтетический deferred spotlight**, который рендерится тем же volumetric пайплайном, что и `light_deferred` spot:
   - На клиенте создать 1 “виртуальный” `def_light_t` типа SPOT (не entity в карте), который каждый кадр обновляется:
     - origin: `view.origin` (или чуть смещён назад)
     - forward: `sunDir`
     - cone: узкий (например 1–3°)
     - radius: большой (например 4096–8192)
     - diffuse цвет: нормализованный (0..1), интенсивность отдельным множителем
     - **volumetrics ON**, shadows/cookie OFF
3. **Разрешить volumetrics без теней**:
   - Сейчас `m_bDrawVolumetrics` и очередь volume-пассов завязаны на `(HasShadow && HasVolumetrics)`.
   - Меняю условие на `HasVolumetrics()` и добавляю ветку рендера для volumetric без shadow/cookie (в шейдере это уже поддержано комбо `NUM_SHADOWED=0`).
4. **Контроль качества/производительности**:
   - ConVar: `r_deferred_sun_volumetrics 0/1`, `r_deferred_sun_volumetrics_intensity`, `r_deferred_sun_volumetrics_cone`, `r_deferred_sun_volumetrics_radius`.
   - Сэмплы для солнца — минимальные (например 6–10), без лишних дополнительных проходов.

Файлы: `clight_manager.cpp` (интеграция в очередь volumetrics), `def_light_t.*` (если понадобится компактный конструктор/фабрика для виртуального света), возможно `lighting_pass_volum.cpp` (комбо/данные volumeData).

## 2) Light_deferred: гарантированно стартует по времени из Hammer
1. **Перенести первичную time-gate синхронизацию в Spawn() (GAME_DLL)** у `CDeferredLight`:
   - в `Spawn()` выставить `m_iDefFlags = GetSpawnFlags()` и сразу применить time-gate до первого снапшота.
   - если `env_timecycle` ещё нет — принудительно выключить.
2. **Сделать первый апдейт безусловным**:
   - не только сравнивать `m_bLastTimeEnabled`, а всегда сверять с текущим `DEFLIGHT_ENABLED` и приводить к `shouldEnable`.
3. **Диагностика** (временно, через ConVar): по желанию добавить `r_deferred_timegate_debug` чтобы в консоль печатать hour/start/end/shouldEnable на старте карты.

Файлы: `CDefLight.h/.cpp`, при необходимости `env_timecycle.cpp`.

## 3) Hammer: чтобы spot выглядел как направленный
1. Добавить второй classname, например **`light_deferred_spot`**, который в коде мапится на тот же `CDeferredLight`.
2. В FGD:
   - `light_deferred` остаётся “лампочкой” (point)
   - `light_deferred_spot` получает `lightprop("models/editor/spot.mdl")` + `lightcone()` и дефолт `light_type=1`.

Файлы: `CDefLight.cpp` (добавить второй `LINK_ENTITY_TO_CLASS`), `deferred.fgd` (оба места: tools и repo).

## 4) Оптимизация (чтобы FPS не падал)
- Убрать текущий screen-space sun-pass.
- Сделать так, чтобы volumetrics рисовались **только когда реально нужно**:
  - солнце — только если `dot(viewFwd,sunDir)` выше порога и солнце в кадре
  - уменьшить сэмплы для sun-volumetrics
  - блюр volumetrics — либо остаётся как есть (quarter-res), либо добавлю опцию `r_deferred_volumetrics_blur 0/1`.

## 5) Небо: плавный переход синее→оранжевый закат, без “рубильника”
1. Починить источник скачка: в `env_timecycle::UpdateSun()` убрать жёсткое `altitudeDeg = -8`.
2. Ввести **twilight-полосу**:
   - солнце уходит под горизонт плавно (например до -12°), по `smoothstep` вокруг `sunrise_hour/sunset_hour`.
3. В `procsky_atmo_ps` усилить/уточнить закатный градиент у горизонта (плавный переход, цвет и ширина регулируются параметрами).

Файлы: `env_timecycle.cpp`, `procsky_atmo_ps20b.fxc`.

## Проверка
- Карта: время 12 → ночные лампы OFF, время 21 → ON.
- Закат: плавная полоса у горизонта, без резкого почернения.
- Производительность: сравнить FPS с `r_deferred_sun_volumetrics 0/1`.
