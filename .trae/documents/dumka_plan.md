# План: Черные круги на осях point-света в deferred

- Статус: анализ, реализация еще не начата
- Версия: 8
- Последнее обновление: учтены результаты твоих тестов cvar (visleaf/legacy)

## 1. Причина бага

### Подтверждено
- В нашем коде уже есть прямые следы борьбы с этим же классом артефактов: в point shadow math есть комментарии про black circles/black dots/flickering рядом с bias-правками ([common_shadowmapping_fxc.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/materialsystem/stdshaders/common_shadowmapping_fxc.h#L759-L771)).
- В point lighting pass используется normal-offset перед shadow compare; на grazing-углах и дальности это типично даёт нестабильный self-shadowing ([lightingpass_point_ps30.fxc](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/materialsystem/stdshaders/lightingpass_point_ps30.fxc#L193-L210)).
- Наблюдаемый «прыжковый» характер артефакта по граням совпадает с возможными переходами world/fullscreen pass и cull-логикой ([clight_manager.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/clight_manager.cpp#L650-L678), [clight_manager.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/clight_manager.cpp#L480-L483)).
- По твоему тесту `r_deferred_light_visleaf_cull 0/1` изменений нет: visleaf cull не является причиной этого артефакта в целевой сцене.
- По твоему тесту `r_deferred_shadowpoint_legacy 1` (DPSM) ломает рендер теней, поэтому рабочим путем для фикса считаем только cube shadowmap (`r_deferred_shadowpoint_legacy 0`).

### Подтверждено по внешним материалам
- В индустриальной практике это классифицируется как shadow acne/self-shadowing из-за дискретизации shadow map; универсального «одного bias» не существует, обычно нужен slope-aware и normal-aware bias.
- Публичные реализации в движках подтверждают те же выводы:
- постоянный bias сам по себе нестабилен и часто даёт peter-panning;
- receiver-plane bias даёт хорошие результаты не всегда и может ломаться на границах/дегенератах;
- для point/cube path обычно лучше комбинация из консервативного depth bias + normal offset + аккуратного PCF.

### Вероятно
- Корневая причина твоих «черных кругов» — недостабилизированный receiver bias именно в cube point-shadow path при отдельных углах и дистанции.
- Дополнительно размер/вид круга меняется из-за соотношения: shadow resolution, kernel radius и angle-dependent bias.

### Нужно проверить
- Влияет ли сильнее всего именно bias (а не pass switching), если менять только shadow quality/resolution.
- Как меняется артефакт при варьировании только параметров bias/normal-offset в cube path, без переключения на DPSM.

### Простыми словами
- Это почти наверняка не «сломанные материалы», а ошибка точности/смещения в сравнении глубин тени.
- В одних ракурсах смещение слишком маленькое и поверхность самозатеняется пятнами.
- В других ракурсах или дистанциях условия меняются, и круги плавают по размеру/граням.

## 2. Лучшие способы исправления

### Вариант 1
- Подход: сделать bias зависящим от угла и разрешения карты тени, а normal-offset стабилизировать клампами.
- Плюсы: наиболее практичный и проверяемый путь без архитектурной перестройки.
- Минусы: нужна аккуратная калибровка по сценам.
- Риски: слишком большой bias даст отлипание теней.
- Объем вмешательства: средний.

### Вариант 2
- Подход: внедрить receiver-plane/adaptive bias для выборок с оффсетами (PCF) в point path.
- Плюсы: потенциально лучшее качество при больших kernels.
- Минусы: высокая сложность и риск артефактов на геометрических границах в deferred.
- Риски: нестабильность на краях и при сильных depth discontinuities.
- Объем вмешательства: средний-высокий.

### Вариант 3
- Подход: оставить текущий bias, но лечить проявление через pass-гистерезис в cube path без опоры на visleaf/legacy fallback.
- Плюсы: может снизить «скачки» видимости без смены shadow backend.
- Минусы: лечит симптом, а не первопричину black circles.
- Риски: ограниченный эффект, если первично доминирует bias ошибка.
- Объем вмешательства: низкий-средний.

### Рекомендуемый путь
- Основной: **Вариант 1** как базовый фикс, опираясь на практику движков и статьи по shadow acne.
- Частично взять из Варианта 2 только безопасные идеи: bias в единицах texel и аккуратную зависимость от slope, без тяжелого полного receiver-plane переписывания на первом шаге.
- Из Варианта 3 оставить только pass-гистерезис как вторичный шаг, без использования legacy DPSM.

## 3. Вопросы и уточнения
- Блокирующих вопросов нет: можно начинать реализацию по рекомендуемому пути после `/vpered`.

## 4. Пошаговый план выполнения
1. Сделать baseline-прогон на текущей тестовой сцене и зафиксировать 3-4 ракурса с кругами.
2. Ввести параметризованный point bias в shader-коде с зависимостью от угла и texel-scale.
3. Ограничить normal-offset клампами, чтобы убрать acne без явного peter-panning.
4. Добавить диагностический cvar для тонкой настройки bias в рантайме.
5. Прогнать сравнение только в cube режиме (`r_deferred_shadowpoint_legacy 0`) и проверить чувствительность к `quality/res`.
6. Если останутся «прыжки», добавить гистерезис в пороги world/fullscreen pass для point lights.
7. Собрать `client_episodic.vcxproj` и скомпилировать только измененные deferred point шейдеры через `ShaderCompile235`.
8. Скопировать готовые результаты в `hl2rpm` и повторно проверить фикс на тех же ракурсах.

## 5. Что не делается до `/vpered`
- Никаких правок кода, кроме этого плана.
- Никакой компиляции.
- Никакой сборки и тестового прогона.

## Что найдено в интернете и что применяем
- Применяем:
- angle/slope-aware bias вместо «одной константы»;
- нормал-оффсет с клампами;
- bias в масштабе texel-size;
- разделение «качество/стабильность» через диагностический runtime-переключатель.
- Не применяем на первом шаге:
- полный receiver-plane bias как обязательный дефолт (слишком рискован в deferred на границах);
- DPSM/legacy path как рабочий fallback для этого бага, так как у тебя он ломает рендер теней;
- полную смену техники теней (VSM/новый shadow backend), так как это уже отдельный большой проект.

## Внешние источники
- LearnOpenGL: Point Shadows — базовые практики для cube point shadows и bias  
  https://learnopengl.com/Advanced-Lighting/Shadows/Point-Shadows
- LearnOpenGL: Shadow Mapping — slope-dependent bias и acne/peter-panning tradeoff  
  https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping
- DigitalRune: Shadow Acne — практический вывод «depth bias + slope-scaled normal offset»  
  https://digitalrune.github.io/DigitalRune-Documentation/html/3f4d959e-9c98-4a97-8d85-7a73c26145d7.htm
- Unity Shadow Library — реальный код receiver plane bias и замечание про артефакты на edges/intersections  
  https://github.com/TwoTailsGames/Unity-Built-in-Shaders/blob/master/CGIncludes/UnityShadowLibrary.cginc
- Bevy issue #16075 — подтверждение проблемы подбора bias и курс на oriented/adaptive bias  
  https://github.com/bevyengine/bevy/issues/16075
- Bevy issue #3628 — практические заметки по point/cube shadows, PCF и bias-стратегиям  
  https://github.com/bevyengine/bevy/issues/3628
- NdotL notes on shadow bias — разбор, где receiver-plane может деградировать, и почему normal offset часто стабильнее  
  https://ndotl.wordpress.com/2014/12/19/notes-on-shadow-bias/
- GameDev.net discussion — практические замечания по seam/bias и техникам point shadows  
  https://www.gamedev.net/forums/topic/637498-point-light-shadow-mapping/

## Что изменилось в этой версии
- Зафиксированы твои результаты тестов: visleaf cull не влияет, legacy DPSM непригоден из-за поломки теней.
- План сузился до единственного целевого пути: фикс только в cube shadowmap path.
- Уточнены шаги проверки: без переключения на legacy, фокус на bias/normal-offset и quality/res.
