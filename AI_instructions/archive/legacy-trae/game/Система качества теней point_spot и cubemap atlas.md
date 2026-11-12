# Система качества теней point/spot и cubemap-atlas

## 1) Что реализовано
- Добавлены пресеты качества для point и spot теней: Very Low, Low, Medium, High, Very High, Ultra.
- Пресеты переключаются в рантайме через `r_deferred_shadow_quality_pointspot` без перезапуска.
- Для point источников добавлен A/B режим:
  - `r_deferred_shadowpoint_legacy 1` — старая dual paraboloid реализация.
  - `r_deferred_shadowpoint_legacy 0` — новая cubemap-atlas реализация (6 граней в атласе 3x2).
- Добавлен профилирующий вывод `r_deferred_shadow_profile_pointspot`.
- UI панели теней расширен: под CSM добавлен новый комбобокс качества point/spot.
- CSM пресеты расширены до 6 уровней (добавлен Very High).

## 2) Техническая спецификация

### 2.1 Пресеты качества point/spot
Базовая таблица разрешений (основной RT и LOD):

- Very Low: 128 / 64 / 32
- Low: 256 / 128 / 64
- Medium: 512 / 256 / 128
- High: 1024 / 512 / 256
- Very High: 1536 / 768 / 384
- Ultra: 2048 / 1024 / 512

Где:
- первый параметр — `r_deferred_rt_shadowspot_res` и базовый `r_deferred_rt_shadowpoint_res`;
- второй и третий — LOD1/LOD2 для adaptive shadow map.

### 2.2 Новая point-теневая схема
- Рендерится 6 перспективных проходов (FOV=90) в один атлас 3x2.
- Для нового режима размеры point shadow RT:
  - ширина = `3 * faceRes`
  - высота = `2 * faceRes`
- В legacy-режиме сохраняется прежняя схема dual paraboloid (ширина = `faceRes`, высота = `2 * faceRes`).
- В пиксельном шейдере выбор схемы (legacy/atlas) делается динамически через знак shadow scalar.

### 2.3 Соответствие плотности CSM
Плотность CSM на каскаде 0 определяется как:

`texelDensityCSM = 2048 / projectionSizeCascade0`

Пресеты CSM (cascade0):
- Very Low: 1024
- Low: 512
- Medium: 256
- High: 128
- Very High: 96
- Ultra: 64

Относительная плотность между пресетами сохранена и перенесена на point/spot через таблицу разрешений.
Ultra для point/spot установлен в 2048 (faceRes), чтобы визуально соответствовать Ultra CSM по резкости и детализации.

## 3) Консольные переменные и команды

### Качество
- `r_csm_quality` — качество CSM (0..5).
- `r_deferred_shadow_quality_pointspot` — качество point/spot (0..5).

### A/B тест point теней
- `r_deferred_shadowpoint_legacy 1` — dual paraboloid.
- `r_deferred_shadowpoint_legacy 0` — cubemap-atlas.

### Профилирование
- `r_deferred_shadow_profile_pointspot`
  - выводит активный пресет CSM и point/spot;
  - активный point shadow mode;
  - размеры RT и texels/light;
  - количество активных shadowed point/spot;
  - суммарный бюджет texels за кадр.

## 4) Рекомендации по пресетам
- Very Low/Low: слабые GPU, много динамических источников, тестовые сборки.
- Medium: сбалансированный профиль для массовых сцен.
- High: основной режим для геймплея с заметными динамическими тенями.
- Very High: повышенная детализация без полного Ultra-бюджета.
- Ultra: финальный визуальный режим, максимальная чёткость и стабильность теней.

## 5) Практический сценарий настройки
1. Выбрать `CSM Quality` и `Point/Spot Shadow Quality` в UI панели `deferred_shadow_settings`.
2. Для сравнения старой и новой point схемы переключать:
   - `r_deferred_shadowpoint_legacy 1`
   - `r_deferred_shadowpoint_legacy 0`
3. После каждой смены смотреть `r_deferred_shadow_profile_pointspot`.
4. Подобрать пресет по соотношению визуала и texel-бюджета для целевой сцены.
