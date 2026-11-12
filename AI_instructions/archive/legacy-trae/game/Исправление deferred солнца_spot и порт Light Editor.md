## Ответ: как добиться «как в Alien Swarm deferred»
Сделать 1-в-1 можно двумя путями:
- **Полная миграция на Swarm deferred стек** (client+шейдеры+IDeferredExt v001+convar’ы `deferred_*`). Это максимально близко по виду/поведению, но самый рискованный вариант: придётся согласовать интерфейс `IDeferredExt`, форматы коммитов, список RT, шейдерные комбо и все места вызовов.
- **Практичный путь (я выберу его): оставить твой текущий Mapbase deferred стек, но портировать из Swarm именно то, что даёт “красивые тени” и оптимизации**, при этом не ломая всю связку DLL/шейдеров. В итоге визуально получится очень близко к Swarm, а объём риска и регрессий сильно меньше.

## Почему сейчас не “как в Swarm”
- У Swarm и у твоего deferred разные версии/организация `IDeferredExt` и разные ожидания по коммиту данных. Если где-то у тебя сейчас подмешаны «чужие» части (например, шейдерная DLL/заголовки/конфиги), то это даёт симптомы уровня “нет теней от солнца”, “spot как point”.
- В твоём портированном `source-sdk-vs2022-deferred` уже виден конкретный баг global light: в `CDeferredLightGlobal::GetState()` не задаётся `w` у `Vector4D` → потенциально мусор/NaN → глобальный свет/тени могут выключаться. См. [CDefLightGlobal.cpp (vs2022)](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/source-sdk-vs2022-deferred/sp/src/game/shared/deferred/CDefLightGlobal.cpp#L107-L129).

## План работ после подтверждения (делаем ближе к Swarm)
### 1) Стабилизировать базу (исправить то, что явно ломает картинку)
- Исправить `light_deferred_global` в активной ветке: задать `diff/amb*.w=1`, `vecLight.w=0` как в рабочем варианте Mapbase: [CDefLightGlobal.cpp (Mapbase)](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/shared/deferred/CDefLightGlobal.cpp#L107-L129).
- Добавить минимальную диагностику, чтобы поймать spot→point (проверка, доходит ли `light_type` по сети и не затирается ли на клиенте).

### 2) Портировать “красивые тени” из Swarm (CSM + фильтрация)
- Сравнить и перенести различия из Swarm в:
  - генерацию/коммит данных каскадов (slope/normal scale, UV transform, depth resolution)
  - шейдеры shadow-пасса и выборку shadowmap в global light (PCF/смягчение, bias)
- Целевые точки:
  - CPU: [viewrender_deferred.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/viewrender_deferred.cpp), Swarm-референс: [swarm viewrender_deferred.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/swarm-deferred-master/src/game/client/deferred/viewrender_deferred.cpp)
  - Шейдеры: [defpass_shadow.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/materialsystem/stdshaders/defpass_shadow.cpp), [lighting_global.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/materialsystem/stdshaders/lighting_global.cpp)

### 3) Портировать оптимизации света (без смены API)
- Сравнить и перенести то, что реально даёт профит:
  - сортировки/батчинг активных источников
  - упаковку данных (кол-во констант/rows) и минимизацию переключений материалов
  - shadow/cookie лимиты и LOD-логики
- Основные точки: [clight_manager.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/clight_manager.cpp), [deferred_rt.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/deferred_rt.cpp).

### 4) Довести Light Editor до уровня Swarm (в игре: спавн/ползунки/перетаскивание/солнце)
- Код уже есть; нужно довезти контент и убедиться, что он компилится/подключён:
  - VGUI layouts: `resource/deferred/lighteditor_controls.res`, `resource/deferred/lighteditor_properties.res`
  - Материалы/текстуры editor-хелперов из Swarm build
- Проверить:
  - команда `r_deferred_light_editor_toggle` открывает UI
  - режим Add добавляет свет кликом (спавн в рантайме), выделение/drag работает
  - `Edit Global Light` меняет параметры солнца в рантайме (рендер реально берёт state editor’а: [GetActiveGlobalLightState](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/deferred/viewrender_deferred.cpp#L1498-L1513)).

### 5) Верификация
- Тестовая карта: 1 глобальный свет + 1 spot с тенями + 1 point.
- Проверяем:
  - тени от солнца стабильны и мягче (как в Swarm)
  - spot реально spot (конус + spot-shadow), не point
  - редактор света работает полностью.
