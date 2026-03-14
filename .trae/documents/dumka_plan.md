# План: Качество теней (CSM/point/spot), артефакты point, мягкий дизеринг

- Статус: анализ, реализация еще не начата
- Версия: 10
- Последнее обновление: 2026-03-14 (выбран “лучший вариант” решения для каждого пункта)

## 1. Причина проблем

### Подтверждено (что сейчас в коде)
- **Point light_deferred (omni)**
  - Разрешение одной “грани” берется из `r_deferred_rt_shadowpoint_res` (меняется пресетом `r_deferred_shadow_quality_pointspot`): [deferred_client_common.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/deferred_client_common.cpp#L8-L56).
  - При `r_deferred_shadowpoint_legacy 0` используется **cube atlas 3×2**: итоговый RT `res_x = faceRes*3`, `res_y = faceRes*2`: [deferred_rt.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/deferred_rt.cpp#L470-L507), [clight_manager.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/clight_manager.cpp#L1774-L1779).
  - Таблица пресетов point/spot (0..5): `{128,256,512,1024,1536,2048}`: [deferred_client_common.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/deferred_client_common.cpp#L26-L49).
- **Spot light_deferred (конус/проектор)**
  - Разрешение карты тени берется из `r_deferred_rt_shadowspot_res` (тот же пресет `r_deferred_shadow_quality_pointspot`): [deferred_client_common.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/deferred_client_common.cpp#L8-L56).
  - Итоговый RT квадратный `res×res`: [deferred_rt.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/deferred_rt.cpp#L374-L405).
- **CSM (солнце)**
  - `r_csm_quality` меняет **покрытие** (projection size) и bias-таблицы; per-cascade resolution сейчас всегда **2048** для всех пресетов: [cascade_t.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/cascade_t.cpp#L65-L93).
  - Атлас CSM при композитинге фиксированный **8192×4096** (8 каскадов как 4×2 по 2048): [deferred_global_common.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/materialsystem/stdshaders/deferred_global_common.h#L117-L127), [cascade_t.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/cascade_t.cpp#L87-L91).
- Все “деферред” тени сейчас используют **depth-stencil** путь фильтрации (SHADOWMAPPING_METHOD = `SHADOWMAPPING_DEPTH_STENCIL__5X5_GAUSSIAN`): [deferred_global_common.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/materialsystem/stdshaders/deferred_global_common.h#L194-L209), реализация PCF/gauss: [common_shadowmapping_fxc.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/materialsystem/stdshaders/common_shadowmapping_fxc.h#L28-L103).

### Вероятно (почему “слоистые” края только у point)
- **Регулярный (детерминированный) PCF-узор** в сочетании с большим kernel radius визуально дает “ступени/полосы” на границе тени. Это классическая проблема: один и тот же набор оффсетов для всех пикселей → бэндинг; лечится джиттером/ротейтом набора семплов (см. “Внешние источники”). 
- **Cube-atlas ограничения**: point path принудительно клампит UV внутри тайла, чтобы PCF не перетекал на соседнюю грань (`Clamp UVs within tile...`). При больших оффсетах этот clamp может проявляться как заметные “параллельные линии” при пересечении seam/границы фейса: [common_shadowmapping_fxc.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/materialsystem/stdshaders/common_shadowmapping_fxc.h#L732-L741).
- **Точность depth (DST16 vs DST24)**: на системах где shadow depth формат = 16-bit, при больших радиусах point света (большое far/near) возрастает квантизация и риск “полос”. В нашем коде near для point проекции фиксирован `zNear=5`, far≈`radius`: [common_shadowmapping_fxc.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/materialsystem/stdshaders/common_shadowmapping_fxc.h#L708-L714). Формат depth выбирает materialsystem, с фоллбеком на DST16: [cdeferred_manager_client.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/cdeferred_manager_client.cpp#L153-L166).

### Подтверждено по внешним материалам (какие классы решений обычно применяют)
- Избавление от banding при мягких тенях делают через **jittered/rotated sampling**: базовая идея — заменить низкочастотные “ступени” на высокочастотный шум, который глаз хуже замечает, и который хорошо фильтруется соседними пикселями (PCF + джиттер). Это описано в GPU Gems и в классических туториалах: NVIDIA GPU Gems 2 (PCF + jitter) и OpenGL Tutorial (banding при широком kernel и решение через rotated/stratified Poisson). 
- Для точности depth shadow maps важны **tight near/far** и выбор глубины (16/24/32 бит): чем меньше отношение far/near, тем выше полезная точность depth — это прямой фактор acne/peter-panning/banding: Microsoft Learn про shadow depth maps.
- Для “очень мягкого” дизеринга удобны дешевые генераторы шума на пиксель, в т.ч. **interleaved gradient noise (IGN)** и вращение выборок (Vogel/Poisson), чтобы убрать регулярную структуру: GameDev.net (CHSS/IGN) и др.

### Простыми словами
- Сейчас CSM-качество в основном регулирует **покрытие** (насколько близко/далеко и насколько мелко), а point/spot — **размер карты**.
- “Слоистость” у point почти всегда сигналит про сочетание **регулярного PCF-узора** и/или **cube seam/clamp**, а также может усиливаться недостаточной точностью depth при больших радиусах.
- “Очень мягкий дизеринг” — это обычно **микро-джиттер** координат семплинга или ротация паттерна, чтобы убрать полосы без заметного шума.

## 2. Лучшие и самые эффективные способы исправления

### Проблема 1: какое сейчас разрешение теней (point atlas / spot)
- Текущее состояние:
  - Point cube atlas: `atlas = (faceRes*3) × (faceRes*2)`, `faceRes = r_deferred_rt_shadowpoint_res`.
  - Spot: `res×res`, `res = r_deferred_rt_shadowspot_res`.
  - По дефолту `r_deferred_shadow_quality_pointspot=5` ⇒ `faceRes=res=2048` ⇒ point atlas `6144×4096`, spot `2048×2048`.
- Лучший вариант решения (практичный): добавить **явный вывод текущих размеров** в статистику/отладку и в UI теней.
  - Почему это лучший: решает “какое сейчас разрешение” на 100%, без вмешательства в рендер.
  - Где показывать: в `deferred_shadow_settings` и/или в `r_deferred_light_stats`.

### Проблема 2: “слоистые” края у point (а у CSM/spot нормально)
#### Лучший вариант решения (самый эффективный под наш текущий пайплайн)
- Подход: **micro-jitter (дизеринг) на семплинге тени + “tile-safe” guard-band** в cube-atlas path.
  - Суть: регулярный узор PCF (полосы) заменяем стохастикой, но делаем её безопасной для 3×2 атласа (не вылезать за границу тайла).
  - Почему это лучший: стандартный приём против banding на мягких тенях (PCF + jitter/rotated sampling) и минимально инвазивен для Source 2013/D3D9.
  - Источники:
    - GPU Gems 2 (PCF + jitter, “banding → high-frequency noise”): https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-17-efficient-soft-edged-shadows-using
    - OpenGL Tutorial (banding при широком kernel, rotated/stratified Poisson): http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-16-shadow-mapping/
    - GameDev.net форум (rotated poisson + rotation texture / screen-space seed): https://www.gamedev.net/forums/topic/443212-dithering-shadow-maps/3937949/
  - Привязка к нашему коду: clamp “внутри тайла” уже есть тут: [common_shadowmapping_fxc.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/materialsystem/stdshaders/common_shadowmapping_fxc.h#L732-L741). Дизеринг обязан учитывать этот clamp (расширяем edgeBias на |jitter|).
  - Резервный рычаг (если останется квантизация): tight near/far + depth precision (DST24), см.: https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps

### Проблема 3: “очень мягкий дизеринг” для всех теней (CSM/point/spot)
#### Лучший вариант решения (универсальный и дешёвый)
- Подход: **единый micro-jitter в координатах семплинга** (в “тексельных” единицах shadowmap), привязанный к world-space, и применяемый одинаково для CSM/spot/point.
- Почему это лучший: даёт одинаковый “анти-бэндинг” эффект везде, не требует переписывать наш текущий 5×5 Gaussian PCF.
- Рекомендации по качеству:
  - Амплитуда на старте: ~0.20–0.35 текселя.
  - Сид шума: worldPos (или worldPos + cascadeIndex) для стабильности при движении камеры.
  - Источники:
    - GPU Gems (dithered sample selection): https://developer.nvidia.com/gpugems/gpugems/part-ii-lighting-and-shadows/chapter-11-shadow-map-antialiasing
    - DigitalRune (world-space stable jitter): https://digitalrune.github.io/DigitalRune-Documentation/html/bed07eb7-0d10-40f3-93e8-c823a787b6a7.htm
    - IGN как дешёвый per-pixel random для теней: https://www.gamedev.net/tutorials/programming/graphics/contact-hardening-soft-shadows-made-fast-r4906/

### Проблема 4: посчитать “оптимальную точность” для пресетов (CSM + point/spot)
#### Лучший вариант решения (реально применимый в Source 2013 без архитектурного переписывания)
- Подход: оформить “оптимальность” как **целевую world-units-per-texel** и выдать **таблицу по пресетам**, совпадающую с реальным поведением движка:
  - CSM = coverage (ProjectionSize) при фиксированных 2048;
  - point/spot = resolution пресета.
- Формулы:
  - CSM: `unitsPerTexel = ProjectionSize / 2048` (ProjectionSize — полный размер каскада): [cascade_t.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/cascade_t.cpp#L55-L59).
  - Point: `unitsPerTexel ≈ (2*Radius) / faceRes`.
  - Spot: `unitsPerTexel ≈ (2*Far*tan(FOV/2)) / res`.
- Таблица “точности”:
  - CSM, каскад 0: q0=0.50, q1=0.25, q2=0.125, q3=0.0625, q4=0.046875, q5=0.03125 (u/tex).
  - Point/Spot res (факт): `{128, 256, 512, 1024, 1536, 2048}`: [deferred_client_common.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/deferred_client_common.cpp#L26-L34).
  - Point “порог” для far ≈1 u/tex: `RadiusMax ≈ faceRes/2` → q0~64, q1~128, q2~256, q3~512, q4~768, q5~1024.
  - Spot “порог” зависит от FOV. Для FOV=60°: `FarMax ≈ res/1.154` → q0~111, q1~222, q2~444, q3~888, q4~1331, q5~1775.
- Почему это корректный критерий: масштаб проекции и отношение near/far напрямую управляют точностью depth и артефактами (acne/peter-panning/banding): https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps

## 3. Вопросы и уточнения (не блокирующие)
- Нужен ли дизеринг строго статичный (без “плавания”) или допускается очень легкое “шевеление” при движении камеры?
- Приоритет: сначала убрать “слоистость” point любой ценой, или сначала единый дизеринг для всех теней (чтобы сразу было видно эффект)?

## 4. Пошаговый план выполнения (после `/vpered`)
1. Зафиксировать тестовую сцену/ракурс со “слоистостью” point и проверить зависимость от `r_deferred_shadow_quality_pointspot` и радиуса point света.
2. Быстро локализовать природу артефакта:
   - повернуть/сместить point свет так, чтобы shadow edge прошел через seam куба (проверка на seam/clamp);
   - принудительно проверить depth формат (DST16/DST24) и влияние near/far (временный хак/лог).
3. Внедрить единый micro-jitter (dither) в sampling:
   - добавить дешёвый генератор шума (world-space hash/IGN);
   - применить к CSM/spot/point перед `PerformShadowMapping`;
   - для point cube atlas расширить clamp на величину |jitter|, чтобы не ловить bleeding.
4. Добавить cvar-интенсивность дизеринга и дефолт “очень мягкий”; обновить vgui-панель теней при необходимости.
5. Пересчитать “точность” пресетов и предложить финальную таблицу:
   - вывести в документ/лог: CSM units/texel по каскадам для каждого `r_csm_quality`;
   - вывести: point/spot units/texel формулы и примеры (радиус 256/512/1024);
   - при необходимости изменить таблицы пресетов (point/spot в [deferred_client_common.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/deferred_client_common.cpp) и CSM в [cascade_t.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/cascade_t.cpp)).
6. Скомпилировать только затронутые шейдеры через ShaderCompile235 и положить результаты в `hl2rpm`.
7. Прогнать проверку: CSM + spot + point на 2–3 сценах, убедиться что шум не заметен, а “слои” ушли/сильно уменьшились.

## 5. Что не делается до `/vpered`
- Никаких правок кода, кроме этого плана.
- Никакой компиляции.
- Никакой сборки и тестового прогона.

## Внешние источники (подборки решений под наши пункты)
- Microsoft Learn (tight near/far, depth precision, bias-техники):  
  https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps
- NVIDIA GPU Gems 2, Chapter 17 (PCF + jitter, “banding → high-frequency noise”):  
  https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-17-efficient-soft-edged-shadows-using
- NVIDIA GPU Gems, Chapter 11 (dithered sampling, выбор поднаборов семплов по экранной позиции):  
  https://developer.nvidia.com/gpugems/gpugems/part-ii-lighting-and-shadows/chapter-11-shadow-map-antialiasing
- OpenGL Tutorial #16 (banding при широком kernel, rotated/stratified Poisson):  
  http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-16-shadow-mapping/
- GameDev.net (Contact-hardening soft shadows, IGN как способ убрать banding при малом числе семплов):  
  https://www.gamedev.net/tutorials/programming/graphics/contact-hardening-soft-shadows-made-fast-r4906/
- GameDev.net форум (rotated poisson + rotation texture / VPOS для стабильности дизеринга):  
  https://www.gamedev.net/forums/topic/443212-dithering-shadow-maps/3937949/
- DigitalRune docs (jittered PCF, рекомендация делать паттерн стабильным в world space):  
  https://digitalrune.github.io/DigitalRune-Documentation/html/bed07eb7-0d10-40f3-93e8-c823a787b6a7.htm

## Что изменилось в этой версии
- Для каждого из 4 пунктов выбран один “лучший” вариант решения и обоснован внешними источниками.
- Добавлена таблица “точности” по текущим пресетам (CSM + point/spot) и понятные пороговые оценки.
