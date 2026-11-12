## Цель (как ты попросил)
- Не «допиливать наш прототип», а **сделать порт deferred-системы из `source-sdk-vs2022-deferred`** в твой текущий SP Mapbase.
- Параллельно **встроить Hosek–Wilkie** и сделать timecycle (утро/день/вечер/ночь) поверх уже работающего deferred.
- В конце — **инструкция для чайника**, как собрать/закинуть/запустить/настроить Hammer.

## Часть 1 — Порт deferred-системы из `source-sdk-vs2022-deferred`

### 1) Инвентаризация референса
- Выделить минимальный набор модулей deferred из рефа:
  - `mp/src/game/client/deferred/*` (рендер-пайплайн, менеджер света, RT, flashlight deferred)
  - `mp/src/game/shared/deferred/*` (сущности/структуры/общие флаги)
  - `mp/src/game/server/deferred/*` (серверные сущности + возможная конвертация старых lights)
  - связанные изменения в `client/viewrender.cpp` (в рефе есть интеграция/вызовы)
  - `deferred.fgd` (сущности для Hammer)
- Зафиксировать список добавляемых файлов и точек интеграции.

### 2) Подготовка к порту под SP Mapbase
- Сопоставить различия:
  - `mp` vs `sp` ветки (названия проектов, условия компиляции, отличающиеся сущности/классы)
  - Mapbase-специфика (макросы `MAPBASE`, отличия в `c_env_global_light`, projected textures).
- Выбрать стратегию:
  - Портируем deferred как отдельную подсистему `client/deferred/` и подключаем её из `viewrender.cpp` как в рефе.

### 3) Реальный перенос кода
- Добавить в твой `sp/src/game/*`:
  - `game/client/deferred/*`
  - `game/shared/deferred/*`
  - `game/server/deferred/*`
- Прописать их в VPC/VCXPROJ (client/server/shared) чтобы они реально компилились.
- Интегрировать входные точки:
  - заменить/добавить в `viewrender` создание GBuffer view + skybox view + deferred lighting + post-lighting как в рефе (не как наш упрощённый проход).
- Перенести и включить менеджер deferred render targets (RT’шки как в рефе: GBuffer, depth/resolve, volumetrics RT, shadow RT).

### 4) Сущности и свет
- Добавить сущности из рефа:
  - `light_deferred` (point/spot)
  - `light_deferred_global` (солнце/небо)
- Добавить (или адаптировать) серверную «конвертацию старых lights» как в рефе (опционально):
  - чтобы существующие карты хоть как-то выглядели.
- Volumetric lighting:
  - включить pipeline volumetrics из рефа (volume prepass + volume materials + blend RT) и флаг `Enable volumetrics` на светильнике.

### 5) Шейдеры/материалы deferred из рефа
- Перенести полный набор deferred-шейдеров/материалов, который ожидает реф (а не только наши 3 .fxc).
- Собрать их через твой `ShaderCompile235` (он у тебя уже есть).
- Проверить, что нужные `.vcs` попадают в `hl2rpm\shaders\fxc`.

### 6) FGD и Hammer
- Подключить `deferred.fgd` из рефа к твоему FGD набору.
- Убедиться, что в Hammer появляются `light_deferred`, `light_deferred_global` и их ключи (в т.ч. volumetrics).

### 7) Проверка работоспособности
- Тестовая карта (минимальная):
  - `light_deferred_global` + 2-3 `light_deferred` (один с volumetrics)
  - прозрачности/воды помечаем как known-bad (как в README рефа).
- Отладочные команды рефа `r_deferred_*` (статы/визleaf cull) — проверяем, что работают.

## Часть 2 — Hosek–Wilkie timecycle (утро/день/вечер/ночь)

### 1) Подключить библиотеку
- Вынести `ArHosekSkyModel.c/.h` и data headers из:
  - `sp/src/game/HosekWilkie_SkylightModel_C_Source.1.4a/...`
- Добавить C++ обёртку (инициализация стейтов, выборка sky radiance по theta/gamma).

### 2) Система времени
- Реализовать `env_timecycle` (server+client):
  - время суток, скорость, широта/дата, turbidity, albedo земли
  - автосмена фаз: ночь реально тёмная, плавные рассвет/закат.

### 3) Рендер неба
- Сделать один из двух путей (выберу по совместимости с твоим билдом):
  - A) шейдер неба, который вычисляет Hosek прямо в пикселе;
  - B) генерация sky cubemap в RT с обновлением по времени.

### 4) Связь с освещением
- Привязать к deferred global light:
  - направление солнца и цвет/интенсивность из Hosek
- Привязать к тонмапу/экспозиции:
  - ночью ограничить экспозицию так, чтобы было темно, днём — адекватно.

### 5) FGD для timecycle
- Добавить `env_timecycle` в FGD (чтобы ставить в Hammer и настраивать).

## Итоговая “инструкция для чайника” (что ты просишь в конце)
Сделаю отдельным блоком после реализации:
- какие проекты собирать (client/server/shader dll)
- куда выходят `client.dll/server.dll/game_shader_dx9.dll`
- как собрать шейдеры через `ShaderCompile235` (одной командой)
- какие папки/файлы должны оказаться в `hl2rpm\bin` и `hl2rpm\shaders\fxc`
- какие FGD подключить в Hammer и какие энтити ставить:
  - минимальный сет: `light_deferred_global`, `light_deferred`, (опц.) `env_timecycle`
- какие консольные команды для проверки (`r_deferred_*`, debug overlays)

## Границы ожиданий (честно)
- Как и в README рефа: вода/alpha/translucency/спекуляр могут быть частично поломаны — я это зафиксирую, и сделаю “компат-режим”, если нужно.
- Но базовый deferred + shadows + volumetrics + timecycle (Hosek) будут работать на тест-карте и на твоих картах после замены света на deferred-сущности.

Если подтверждаешь — выхожу из plan mode и начинаю с порта `client/deferred/*` + `shared/deferred/*` и сборки RT/шейдеров.