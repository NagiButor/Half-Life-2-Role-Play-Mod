## Диагноз
- **Godrays** сейчас рисуются прямо в sky-пикселе как синусоидальные «спицы» по углу вокруг солнца, поэтому получаются статичные линии без ощущения volumetric glow и без окклюзии геометрией.
- **Звёзды** сейчас на регулярной сферической сетке, поэтому выглядят «слишком ровно»; плотность/порог тоже завышены.
- **Переход день↔ночь/закаты** в небе сейчас завязан на простой фактор `sunDir.z`, что даёт резкие переключения и слабую работу с горизонтом.
- **Light_deferred 20→6** может стартовать включённым, если на момент Activate нет `env_timecycle` (или время ещё не инициализировано) — текущая логика оставляет `DEFLIGHT_ENABLED` как в spawnflags.

## 1) Нормальный volumetric glow вокруг солнца (без «спиц»)
- **Убрать/обнулить текущие sky-godrays**: оставить только мягкий sun disk + bloom в `SkyAtmoProc`, а параметры `$godrays*` по умолчанию сделать 0 (или удалить из VMT), чтобы не ломали картинку.
- **Сделать screen-space sun glow в существующем volumetrics пайплайне** (как у `light_deferred`):
  - Добавить новый quarter-res RT pass в `CLightingManager::RenderVolumetrics()` перед blur/blend (используем уже существующий `_rt_VolumAccum_00` как аккумулятор).
  - Новый fullscreen-шейдер `SUN_GODRAYS`:
    - вход: depth (`GetTexture_Depth()`), sun direction (из `GetGlobalLight()->GetState().vecLight`), матрицы/параметры view.
    - вычисление: 
      - найти screen position солнца (проекция направления в clip-space)
      - сделать короткий радиальный sample/blur по направлению к солнцу, **с окклюзией по depth** (классический screen-space godrays)
      - ограничить эффект только когда камера смотрит близко к солнцу (`dot(viewDir,sunDir)`), чтобы это было «небольшое свечение».
  - Параметры в material/ConVar: интенсивность, радиус, кол-во сэмплов (8–16), decay.
- Файлы (планируемые):
  - добавить FXC + stdshader для нового пасса (в `materialsystem/stdshaders/`)
  - правка вызова в [clight_manager.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/clight_manager.cpp#L1493-L1521)

## 2) Звёзды: меньше, естественнее, без ряби
- Сохранить стабильность (чтобы не рябило), но уйти от «ровной решётки»:
  - перейти на **jittered Poisson-like** точки: для каждой ячейки генерировать случайный оффсет внутри ячейки (hash(cell) → offset), плюс редкий «кластер» вторым слоем.
  - увеличить порог появления (звёзд меньше), добавить вариативность размера/яркости.
  - добавить лёгкое подавление около горизонта и вблизи луны/солнца (если нужно).
- Настройка будет через параметры VMT по умолчанию (`$starsintensity`, и добавлю новый `$starsdensity`/`$starsamount`), чтобы можно было легко тюнить без перекомпиляции.
- Файл: [procsky_atmo_ps20b.fxc](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/materialsystem/stdshaders/procsky_atmo_ps20b.fxc)

## 3) Плавный day/night + красивые закаты у горизонта
- В `procsky_atmo_ps` заменить «резкий night-фактор» на **smoothstep/twilight** кривые по высоте солнца.
- Добавить явный компонент заката:
  - вычислить `horizon = smoothstep(...)` по `rayDir.z`
  - `sunNearHorizon = smoothstep(...)` по `sunDir.z`
  - тёплый tint (оранж/розовый) усиливать у горизонта при низком солнце
  - добавить мягкий halo вокруг солнца, который переходит в “afterglow” в сумерках.
- При необходимости добавлю параметры VMT: `$twilightwidth`, `$sunsetintensity`, `$horizonfalloff`.

## 4) Light_deferred: корректное включение по времени 20→6
- Исправить поведение при отсутствии/позднем появлении `env_timecycle`:
  - если заданы `enable_from_hour/enable_to_hour` и `GetTimecycle()==NULL`, **принудительно выключать DEFLIGHT_ENABLED** и продолжать Think до появления таймцикла.
  - инициализировать `m_bLastTimeEnabled` так, чтобы первое реальное вычисление окна гарантированно применялось.
- Сделать команды `sv_timecycle_set_time/sv_timecycle_set_speed` более «неубиваемыми»:
  - если `env_timecycle` не найден, автоматически создать его на сервере (CreateEntityByName) и Spawn/Activate, затем применить команду.
- Файлы: [CDefLight.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/shared/deferred/CDefLight.cpp), [env_timecycle.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/shared/timecycle/env_timecycle.cpp)

## Проверка
- Карта с `env_timecycle`, `light_deferred_global`, несколькими `light_deferred` (enable 20→6).
- Скрипт проверки:
  - поставить время 12.0 → night-lamps OFF
  - поставить время 21.0 → night-lamps ON
  - покрутить скорость времени и убедиться, что переключение происходит автоматически.
- Визуально:
  - солнце: маленький мягкий volumetric glow при взгляде на него, без “спиц”, с затуханием при окклюзии
  - ночь: звёзды реже и естественнее
  - рассвет/закат: плавная полоса у горизонта и мягкий градиент день↔ночь.

## Что потребуется от тебя после правок
- Пересборка `game_shader_dx9.dll` (новые/изменённые stdshaders) и `client.dll` (если добавлю новый deferred pass на клиенте) + перезапуск игры.