# План исправления багов `light_deferred point` при смене пресетов качества

## Summary
Исправить артефакты теней point/spot при переключении `r_deferred_shadow_quality_pointspot` (застывшие старые тени, «ломаная»/смещённая проекция), чтобы смена пресета работала стабильно не только на `Ultra`.

## Current State Analysis

### Что подтверждено по коду
1. Пресет качества меняет только ConVar-резолюции (`r_deferred_rt_shadowpoint_res`, `r_deferred_rt_shadowspot_res`, LOD):
   - `game/client/deferred/deferred_client_common.cpp` (`ApplyPointSpotShadowQualityPreset`, `OnPointSpotShadowQualityChanged`).
2. Размеры shadow RT и `shadowData_general_t` (в т.ч. `iDPSM_Res_x/y`, `iPROJ_Res`) пересчитываются и коммитятся только в `InitDeferredRTs`:
   - `game/client/deferred/deferred_rt.cpp`.
3. `DefRTsOnModeChanged` запрещает реинициализацию в игре (`if (!engine->IsInGame())`), поэтому после смены пресета в активной игре часть параметров обновляется, а часть остаётся старой:
   - `game/client/deferred/deferred_rt.cpp`.
4. Шейдерные константы фильтрации/проекции теней берут размеры из `GetDeferredExt()->GetShadowData_General()`:
   - `materialsystem/stdshaders/lighting_helper.h` (`CommitShadowProjectionConstants_DPSM/Proj`).
5. Отсюда получается рассинхрон «текущие ConVar-резолюции ↔ ранее закоммиченные shadow constants/RT», который и даёт смещение/поломку теней при переключении пресета во время игры.

### Дополнительный риск, усиливающий артефакты
`CDeferredExtension` не зануляет `m_dataProj`/часть shadow-данных в конструкторе, что может оставлять мусор при чтении неактуальных индексов:
- `materialsystem/stdshaders/IDeferredExt.cpp`.

## Proposed Changes

### 1) Добавить отложенную «безопасную» переинициализацию deferred shadow RT после смены пресета
**Файлы:**
- `game/client/deferred/deferred_rt.h`
- `game/client/deferred/deferred_rt.cpp`
- `game/client/deferred/viewrender_deferred.cpp`

**Что делаем:**
1. Ввести флаг «pending refresh» для deferred RT (request/service API в `deferred_rt`).
2. В callback пресета только ставить запрос на refresh (а не пытаться пересоздавать RT в произвольном контексте).
3. Обслуживать pending refresh в начале `CDeferredViewRender::RenderView` на границе кадра (до основного deferred pass), где можно безопасно выполнить `InitDeferredRTs(false)`.
4. После успешной реинициализации сбрасывать pending-флаг.

**Почему:**
- Устраняет рассинхрон размеров shadow-констант и реальных RT сразу после смены пресета.
- Избегает небезопасного вызова реинициализации из callback ConVar.

### 2) Доработать callback смены качества point/spot
**Файл:**
- `game/client/deferred/deferred_client_common.cpp`

**Что делаем:**
1. Сохраняем текущую логику применения пресета к ConVar.
2. Добавляем запрос на deferred RT refresh только при реальном изменении значения качества.
3. Исключаем лишние повторные запросы при одинаковом значении.

**Почему:**
- Обновление остаётся централизованным через один пресет.
- Минимизируются лишние real-time realloc при повторной установке того же пресета.

### 3) Инициализировать shadow-структуры в deferred extension
**Файл:**
- `materialsystem/stdshaders/IDeferredExt.cpp`

**Что делаем:**
1. Явно зануляем/инициализируем `m_dataProj`, `m_dataOrtho`, `m_dataGeneral` в конструкторе `CDeferredExtension`.

**Почему:**
- Убирает дополнительный источник «мусорных» теневых данных при неинициализированных/временно неиспользуемых индексах.
- Снижает вероятность визуальных артефактов при переходных состояниях.

## Assumptions & Decisions
- Целевое поведение: смена пресета качества теней должна работать в текущей игровой сессии без необходимости перезапуска карты/клиента.
- Не трогаем `.vpc` файлы.
- Изменения ограничиваем клиентским deferred pipeline и stdshaders extension.
- Шейдер-комбо/материалы не меняем, пока баг устраняется корректной синхронизацией RT+constants.

## Verification Steps
1. Собрать клиентский проект (без изменений `.vpc`).
2. Проверить отсутствие новых compile errors/warnings в изменённых файлах.
3. Ручной сценарий в моде (`hl2rpm`):
   - Запустить карту с несколькими `light_deferred point` и динамикой объектов.
   - Переключить `r_deferred_shadow_quality_pointspot` по кругу `0 → 5 → 2 → 5`.
   - Убедиться, что тени не «застывают», не смещаются и не читаются из старых atlas-областей.
4. Дополнительно проверить spot lights тем же сценарием (т.к. пресет общий point/spot).
5. Проверить, что `Ultra` не деградировал относительно текущего состояния.
