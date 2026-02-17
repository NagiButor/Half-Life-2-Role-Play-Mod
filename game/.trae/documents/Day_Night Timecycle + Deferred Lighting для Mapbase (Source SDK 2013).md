## Про 3D skybox и “текстуры неба” (skyname)
- В Source есть две разные вещи:
  1) **2D skybox** (cube textures по `skyname`: up/dn/lf/rt/ft/bk).
  2) **3D skybox** (геометрия/модели, рендерятся через `sky_camera`).
- В режиме процедурного неба:
  - **2D skybox-текстуры больше не будут определять картинку неба** (мы их “замещаем” процедурным фоном Hosek–Wilkie).
  - **3D skybox-геометрия продолжит рисоваться** как раньше (дальние здания/горы), поверх процедурного фона.
- Практически:
  - `skyname` можно оставить любым (как fallback), но визуально он будет скрыт процедурным небом.
  - Я добавлю флаг/настройку, чтобы было ясно и управляемо: например `r_procedural_sky 0/1` и/или ключ у `env_procedural_sky` “OverrideSkyboxTextures”.

## Как это будет выглядеть в кадре
1) Процедурный фон (атмосфера, градиенты, рассеяние, солнце/луна).
2) 3D skybox (то, что вы построили в отдельной зоне + `sky_camera`).
3) Основной мир.

## План работ (в вашем порядке)
### Этап 1 — Deferred shading (G-Buffer)
- `r_deferred 0/1`.
- G-Buffer prepass для непрозрачной геометрии.

### Этап 2 — Deferred lighting (все источники)
- Directional (global light) + skylight.
- Spot/projector.
- Point (“лампочки”).

### Этап 3 — Тени в реальном времени от всего
- Солнце: 1 shadow map, опционально CSM.
- Spot: depth shadows через существующую систему.
- Point: cube shadow maps.

### Этап 4 — Убрать baked-вклад и ускорить компиляцию
- В deferred режиме lightmaps игнорируются.
- Добавить VRAD-флаг `-nolightmaps` + инструкция, как прописать его в Hammer (в VRAD parameters в Expert Compile).

### Этап 5 — Timecycle + Hosek–Wilkie
- Движение солнца по куполу и синхронизация `env_global_light(_deferred)` + `env_sun` + `env_procedural_sky`.
- Консольные команды скорости/паузы.
- FGD обновления.

Если подтверждаете — начинаю с Этапов 1–3 (deferred + свет + тени).