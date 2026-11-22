# HL2RPM — текущее состояние и передача работы

## Оптимизация и улучшение Deferred-неба, облаков и аудит deferred-рендера — 2026-11-23 (собрано, проверено в игре)

Выполнены оптимизация процедурного неба и объёмных облаков, улучшение их формы/освещения и аудит багов deferred-пайплайна (без правок `.vpc`):

- **ABI (`materialsystem/stdshaders/IDeferredExt.{h,cpp}`)**:
  - Версия интерфейса поднята до `DeferredExtensionVersion016`: добавлены `weatherData_t::vecCloudUVRect` (актуальный прямоугольник UV при `r_hl2rpm_renderscale`) и `CommitMainViewSkySkip` / `IsMainViewSkySkip`.
- **Процедурное небо (`skyatmo_proc.cpp`, `procsky_atmo_ps20b.fxc`, `deferred_rt.cpp`)**:
  - Расчёт покадрово-инвариантных величин (сумеречные коэффициенты/цвета, ambient, U-координата пропускания и пересечение земли солнцем, базисы ветра и луны, `vecCloudUVRect`) вынесен на CPU в `c17..c27` (важно: **регистры `c28..c31` зарезервированы движком в `common_ps_fxc.h`**, ранее `c29` перезаписывался `g_LinearFogColor` на гранях кубической карты скайбокса); удалена неиспользуемая цель `_rt_SkyAtmoLUT` (512×256 RGBA16F) и привязка `SKYLUT`.
  - Пропуск закрытых пикселей `r_deferred_sky_skip_covered` починен для масштаба `r_hl2rpm_renderscale < 1` через явный флаг `IsMainViewSkySkip()` (включается только в `ViewDrawComposite` при `VIEW_MAIN`, сохраняется и отключается во всех вторичных видах воды/стекла) и проверку `viewport.m_nWidth/Height <= pGBufferDepth->GetActualWidth/Height()`; `sunWithBloomCos` и сумеречные `pow()` убраны за ветвления, лунные моря переведены на плавный 2D value noise, перистые облака — на domain-warped полосы вдоль ветра.
- **Объёмные облака, resolve и god rays (`weather_noise.cpp`, `weather_render.cpp`, `weather_clouds.{cpp,_ps30.fxc}`, `weather_cloudresolve.{cpp,_ps30.fxc}`, `weather_godrays.{cpp,_ps30.fxc}`)**:
  - `CCloudNoiseRegen`: инвертированный Perlin-Worley заменён на выпуклый 4-октавный Perlin `(2,4,8,16)` + 4-октавный Worley `(4,8,16,24)` в `R` и предпакованные средние/мелкие детали Worley (`G`/`B`) + волокнистый Perlin (`A`).
  - Рендер-таргеты `g_tex_Clouds[0/1]` и `g_tex_CloudRaw` переведены на `CreateFrameSizedRT(..., 2, ...)` (1/2 от масштабированного кадра), `vecCloudUVRect` ограничивает выборки в небе, resolve и god rays; убран лишний `SHADER_SAMPLER3`, лишний `GetMatricesForView` и утечка ссылки `g_pMatCloudResolve` в `Shutdown()`.
  - `weather_clouds_ps30.fxc`: `SHAPE_PERIOD = 16000`, `DETAIL_PERIOD = 1600`, 1 выборка `SampleWeatherColumn` на шаг луча, 2× пропуск пустого пространства с шагом назад при попадании, 4-шаговый экспоненциальный световой марш, контрастная растяжка Worley-деталей `[0.30..0.70]→[0..1]`, перекалиброванный `CoverageThreshold` с учётом `HeightProfile` и эрозии (синхронизирован с `CloudShadow` в `lightingpass_global_ps30.fxc`), сбалансированное 3-октавное рассеяние + аналитический `skyAO` + закатное свечение снизу + модулированная пасмурная подложка + чёткие ряды высококучевых.
  - `weather_cloudresolve_ps30.fxc`: дилатация неба на 1 тексель в закрытые геометрией границы (`cur.a < 0`) убрала тёмные ореолы вокруг веток и блоков; клиппинг истории по дисперсии (AABB) + удержание резкого центра 85%.
- **Багфиксы и оптимизации deferred-рендера (`viewrender_deferred.cpp`, `deferred_postfx.{h,cpp}`, `common_shadowmapping_fxc.h`, `lightingpass_{global,point,spot}_ps30.fxc`, `volumpass_sun_ps30.fxc`, `weather_post_ps30.fxc`, `hl2rpm_mirror_ps30.fxc`, `deferred_ssao_ps30.fxc`, `hl2rpm_dof_ps30.fxc`, `composite_ps30.fxc`)**:
  - Добавлен zero-copy путь `DeferredPostFX_Draw(postView, pSceneRT, sw, sh)` при `r_hl2rpm_renderscale < 1` (без лишней копии кадра, блок свечения/блума от `sw×sh`, зерно в `vw×vh`), `DeferredPostFX_DrawDoF` и `DrawDeferredFXAA` используют `GetDefRT_SceneCopy()`.
  - Исправлены баги: неверный шаг PCF для кубического атласа 3×2 и `faceRes` в `PerformDualParaboloidShadow`; пропуск видов `func_reflective_glass` при `m_bCheapWater`; единое сглаженное состояние солнца `s_csmLightState` для `PerformLighting` и `COrthoShadowView` / `RenderCascadedShadows`; сброс `DEFERRED_RENDER_STAGE_INVALID` в `DrawViewModels`; перенос `WeatherRender_SetCloudTextureValid(false)` после `RenderVolumetrics`; вынос `dRdu`/`dRdv` выше раннего выхода в `deferred_ssao_ps30.fxc` (силуэты).
  - Оптимизации шейдеров: вынос аффинных проекций CSM/rainmap/SSR из циклов в `volumpass_sun_ps30.fxc`, `weather_post_ps30.fxc`, `hl2rpm_mirror_ps30.fxc`; предвычисленные таблицы `g_ssaoSpiral[12]` и `g_dofDisk[16]` (1 `sincos`); перенос `CloudShadow` после теней солнца, ранний выход `occ >= 0.99` в контактных тенях и `[branch]` вокруг `SpecularGGX` в `lightingpass_global_ps30.fxc`; перенос `matRotation`/смещений теней под `[branch]` и удаление лишних `normalize`/`dot` в `lightingpass_{point,spot}_ps30.fxc`; ранний выход `flMirror < 0.001` в `hl2rpm_mirror_ps30.fxc`; `[branch]` для detail/transmission и повторное использование `flAlbedoSample.a` в `composite_ps30.fxc`.
- **Проверки**:
  - `MSBuild` `client_episodic.vcxproj` (`client.dll`, MD5 `8BAB8A4D4057F6A5055D6399A6D24133`) и `game_shader_dx9_episodic.vcxproj` (`game_shader_dx9.dll`, MD5 `07EB5922BFBD0467072070902B3DF806`) — 0 ошибок, MD5 совпадают в `hl2rpm/bin`; скомпилированы через `ShaderCompile235 -ver 30` и развёрнуты 14 изменённых `.vcs`; `check_shader_combos.py`: **`checked 86 shaders, 0 mismatched`**.
  - Плейтест Tour 1 (`test_deferred`, `1600×900`, `renderscale 1.0`, `r_weather_clouds 3`, `r_hl2rpm_gpuprof 1`): exit `0x00000000`, `config.cfg restored`, 8 пресетов осмотрены визуально (швов скайбокса и тёмных ореолов нет). Замеры `[gpuprof]` (было → стало): `clouds`: `fair` `3.19–3.29` → `1.73 ms`, `cumulus` `2.50–2.76` → `1.78–2.02 ms`, `partlycloudy 17.8` `2.22–2.25` → `1.98 ms`, `stratocumulus` `2.65–2.67` → `2.08–2.11 ms`, `overcast` `5.09–5.16` → `2.11–2.26 ms` (53.1 → 64.4 fps), `thunderstorm` `1.76–1.80` → `1.07–1.10 ms`, `highclouds` `2.36–2.38` → `1.10–1.18 ms` (58.1 → 65.6 fps), `moon` `2.42–2.45` → `2.26–2.30 ms`; `sky3d`: `0.59–1.04` → `0.50–0.70 ms`; `volumetrics`: `1.47–1.76` → `0.68–1.38 ms`.
  - Плейтест Tour 2 (`test_deferred`, `1600×900`, `renderscale 0.75`, `r_weather_clouds 3`): exit `0x00000000`, `config.cfg restored`; `partlycloudy sun` (god rays) `14.63 ms avg (68.4 fps)`, `cumulus spin` `11.58 ms avg (86.3 fps)`, `night` `10.06 ms avg (99.4 fps)`; `[gpuprof]` `sky3d 0.29–0.49 ms` (пропуск неба подтверждён и при 0.75), `clouds 0.49–1.56 ms`, `postfx 0.99–1.17 ms`. Временные скриншоты из `hl2rpm/screenshots` удалены.

## Переключатель Forward / Deferred в настройках графики и fallback skybox для Forward — 2026-11-21 (собрано, проверено в игре)

Выполнена задача по переключению рендера в UI и работе текстурного скайбокса в режиме Forward:

- **Настройки графики (`game/client/hl2rpm_options_graphics.cpp`, `hl2rpm_extras/tools/add_loc_tokens.py`, `resource/hl2rpm_{russian,english}.txt`)**:
  - В начало вкладки **«Графика»** добавлен пункт `GFX_RENDERER` (**«Режим рендера»** / **«Renderer mode»**, `r_deferred`: `Deferred` = `1`, `Forward` = `0`) с описанием справа (`#HL2RPM_Gfx_Desc_Renderer`). Всего на вкладке 19 пунктов; `iPresetStart` в `GfxPageDesc_t` вынесен в описание страницы, чтобы выбор режима рендера не затрагивался пресетами качества.
  - `iRestartSetting` заменён на битовую маску `nRestartMask` (`GFX_RENDERER` и `GFX_LAMP_SHADOWS`): изменение любого из этих пунктов показывает обновлённую заметку `#HL2RPM_Gfx_RestartNote` («* Изменения режима рендера и теней от ламп вступят в силу после перезапуска игры.»); реплицируемый `r_deferred` применяется через `engine->ClientCmd_Unrestricted` перед `host_writeconfig`. Высота диалога `OptionsDialog` увеличена до `+185` (с зажимом по экрану).
- **Чтение `r_deferred` при старте (`game/shared/deferred/deferred_shared_common.{h,cpp}`, `cdeferred_manager_{client,server}.{h,cpp}`)**:
  - Поскольку `CDeferredManagerClient::Init()` и `CDeferredManagerServer::Init()` выполняются до запуска `cfg/config.cfg` движком, добавлен общий помощник `Deferred_ReadInitialEnabled()` (приоритет: `-nodeferred` → `+r_deferred` → `cfg/config.cfg` с точным разбором строки `r_deferred` → `true`). Это обеспечивает реальное переключение между Deferred и Forward при перезапуске после сохранения в меню и единое состояние клиента/сервера.
- **Текстурный скайбокс в Forward (`game/server/world.{h,cpp}`, `cdeferred_manager_server.cpp`, `game/shared/weather/env_weather.cpp`, `game/shared/movevars_shared.cpp`)**:
  - Умолчение `sv_skyname` заменено с несуществующего `sky_urb01` на `sky_day01_01`.
  - В `CWorld`: пустое значение `skyname` не затирает `sv_skyname`, а при отсутствии ключа `skyname` в `worldspawn` `CWorld::Spawn()` ставит `sky_day01_01`.
  - В `CEnvWeather::ApplyProceduralSky()`: замена на `sky_proc_atmo_01` выполняется только при `GetDeferredManager()->IsDeferredRenderingEnabled()`.
  - В `CDeferredManagerServer::LevelInitPreEntity()` (до отсечки `MapLoad_LoadGame`): в Forward-режиме `EnsureForwardSkybox()` проверяет `sv_skyname` и при пустом значении, `sky_proc_*`, `sky_urb01` или отсутствии `materials/skybox/<name>[_hdr]rt.vmt` прекэширует 6 граней и ставит `sky_day01_01`.
- **Проверки**:
  - `MSBuild` `client_episodic.vcxproj` и `server_episodic.vcxproj` (`Win32 / Release`): 0 ошибок, MD5 `client.dll` (`4CEBC4F5E67CD0CE1BED9B8F60E67B0E`) и `server.dll` (`07D963014F24CD844DDB7B3720342C13`) совпадают в `hl2rpm/bin`; `check_shader_combos.py`: **`checked 86 shaders, 0 mismatched`**. `.vpc` не тронуты.
  - Плейтесты: вкладка «Графика» проверена визуально на `1600×900` (RU, и в Deferred, и в Forward) и `1280×720` (EN) без обрезаний; Forward-режим проверен как с `+r_deferred 0` на `test_deferred` (`skyname = sky_proc_atmo_01` в BSP → `sky_day01_01`, 282.8 FPS), так и через реальное изменение `r_deferred "0"` в `config.cfg` на `ck_test` (`Skipping deferred lighting`, `sv_skyname = sky_day01_01`, 280.9 FPS), и на временной скомпилированной карте без `skyname` в `worldspawn` (`_ai_nosky_test` → `sky_day01_01`, 283.0 FPS, временный BSP удалён); контрольный прогон Deferred на `test_deferred` подтвердил сохранение `sky_proc_atmo_01` и облаков (56.0 FPS), exit `0x00000000`, `config.cfg` восстановлен.

## Удаление кастомных стоячих idle и боя кулаками у NPC — 2026-11-21 (собрано, проверено в игре)

По запросу пользователя удалены кастомные стоячие `idle` и система/анимации боя кулаками у NPC; оставлены только кастомные анимации маркеров (стул, диван, еда, кровать, земля, стена, разговор/обыск на корточках):

- **Библиотека анимаций (`hl2rpm_extras/tools/anims/`)**:
  - Удалены `rpm_anim/sets/stand_idle.py` и `rpm_anim/sets/fists.py`, из `sets/kimodo_clips.py` убрана `idle_bored`, из `rpm_anim/library.py` убраны `rpm_idle_*`, `rpm_pidle_*` и `rpm_fist_*` (сохранены `rpm_sit_chair`, `rpm_sit_couch`, `rpm_eat_chair`, `rpm_lie_bed`, `rpm_sit_ground`, `rpm_lean_wall`, `rpm_lie_ground`, `rpm_talk`, `rpm_search`).
  - Очищены устаревшие `meta/*.json` в `modelsrc/hl2rpm_anims/*`; `make_anims.py --skel male,female,police,combine` пересобрал все 4 библиотеки (`rpm_{male,female,police,combine}_anims.mdl` по 23 последовательности), хук-модели (`male_shared.mdl`, `female_shared.mdl`, `police_animations.mdl`, `combine_soldier_anims.mdl`), `scripts/actbusy.txt` и `scripts/hl2rpm_anim_seats.txt`.
- **Код игры, скрипты и Creation Kit**:
  - `game/server/hl2rpm_melee.cpp`: удалены `weapon_rpm_fists` из `s_Melee[]`, `CWeaponRPMFists`, `CRPMFistsSystem`, `RPMActivity`.
  - `game/client/hl2/c_weapon__stubs_hl2.cpp`: удалена клиентская заглушка `weapon_rpm_fists`.
  - `game/shared/hl2rpm_rpgdefs.cpp`: удалено правило `npc_fists`.
  - Удалён скрипт `hl2rpm/scripts/weapon_rpm_fists.txt`.
  - `hl2rpm_extras/tools/dialog_editor/dlgedit/{worlddata.py,rpgchar.py}`: убраны `weapon_rpm_fists` из `WEAPONS` и правило `npc_fists` из `RULES`; пересобран `HL2RPM Creation Kit.exe` (`build_editor.ps1`, до шага `Copy-Item README.md` — файл удалён ранее).
  - Актуализированы `AI_instructions/topics/animation.md` и `AI_instructions/skills/hl2rpm-dialogs/SKILL.md`.
- **Проверки**:
  - Проверка скомпилированных `.mdl` (`mdl_decompile`): во всех 4 библиотеках ровно по 23 последовательности маркеров, 0 `rpm_idle_*`/`rpm_pidle_*`/`rpm_fist_*`.
  - `MSBuild` `server_episodic.vcxproj` и `client_episodic.vcxproj` (`Win32 / Release`): 0 ошибок, MD5 `client.dll` (`388C01F3BC0E57C0A4E1273192714E69`) и `server.dll` (`CFD363654713FBBA8F6BFE27F8632458`) совпадают в `hl2rpm/bin`.
  - Плейтест `ck_test` (`1600×900`, `-nojoy`): exit `0x00000000`, `config.cfg restored`, 0 ошибок в хвосте `console.log`, визуально подтверждены штатные стоячие `idle` у NPC (`ck_talker`, `ck_hazmat`, солдат) и сохранённая работа маркеров (`ck_ground` / `rpm_sit_ground`, `ck_chair_1` / `rpm_eat_chair`, `ck_bed` / `rpm_lie_bed`).

## Удаление Video -> Advanced, перенос настроек, описания в Graphics/Effects и счётчик FPS — 2026-11-21 (собрано, проверено в игре)

Выполнены три задачи по настройкам и HUD:

1. **Аудит и удаление `Video -> Advanced`**:
   - Проверены 15 пунктов `OptionsSubVideoAdvancedDlg`: 7 устарели/отключены или дублируются deferred-рендером (`ShadowDetail` при `r_shadows 0`, `AntialiasingMode` MSAA при `r_deferred_disable_msaa 1`, `WaterDetail`, `ColorCorrection`, `Bloom`, `ShaderDetail`, `HDR`, необходимый для `SDK_Water_DX9_HDR`), а 7 реально работают и не были выведены в UI (`mat_picmip`, `r_rootlod`, `mat_trilinear`/`mat_forceaniso`, `mat_vsync`, `mat_queue_mode`, `mat_motion_blur_enabled`, `fov_desired`).
   - В `hl2rpm/resource/OptionsSubVideo.res` (UTF-8 без BOM, копия в `hl2rpm_extras/resource/OptionsSubVideo.res`) кнопка `AdvancedButton` скрыта и отключена (`visible 0`, `enabled 0`).
   - Рабочие настройки перенесены в `hl2rpm_options_graphics.cpp`: `mat_picmip` («Качество текстур»), `r_rootlod` («Детализация моделей»), `mat_trilinear`+`mat_forceaniso` («Фильтрация текстур»), `mat_vsync` и `mat_queue_mode` — во вкладку **«Графика»** (18 пунктов; первые 16 участвуют в пресетах через `nPresetSettings`), `mat_motion_blur_enabled` («Размытие в движении») — в **«Эффекты»**, `fov_desired` (75–90°) — в **«Звук и интерфейс»**; при изменении видеопеременных движка дополнительно вызывается `mat_savechanges`.
2. **Кастомный счётчик FPS (`game/client/hl2rpm_fps.cpp`, добавлен в `client_episodic.vcxproj`, без правок `.vpc`)**:
   - Элемент `CHudRPMFPS` (`HudRPMFPS` в `hl2rpm/scripts/HudLayout.res` и `hl2rpm_extras/scripts/HudLayout.res`) в стиле M6 (`RPMUI::Plate`, цвета текущей темы, цветовая индикация `good`/`accent`/`bad` по порогам 55/30 FPS), правый верхний угол, вывод среднего за 0.25 с FPS и времени кадра (`ms`) по `Plat_FloatTime()`.
   - Управляется `cl_rpm_fps` (`FCVAR_ARCHIVE`, по умолчанию `0`), переключатель «Счётчик FPS» добавлен во вкладку **«Эффекты»** (вне расчёта пресета качества).
3. **Подписи ко всем настройкам «Графика» и «Эффекты»**:
   - В `GfxPageDesc_t` добавлены `nPresetSettings` и `ppszDescs`; справа от выпадающих списков выводится `CGfxStyledLabel` (`DefaultSmall`, приглушённый цвет) с кратким описанием каждого из 18 пунктов «Графики» и 13 пунктов «Эффектов».
   - Окно `OptionsDialog` расширено (`+500` по ширине, `+165` по высоте, с ограничением по размеру экрана и центрированием); обновлены `hl2rpm_extras/tools/add_loc_tokens.py` (148 токенов, включая исправленную `HL2RPM_Fx_Hint` без упоминания удалённого GI) и файлы локализации `hl2rpm_{russian,english}.txt` (синхронизированы в `hl2rpm_extras/resource/`).
- **Проверки**:
  - `MSBuild` `client_episodic.vcxproj` (`Win32 / Release`): 0 ошибок, MD5 `client.dll` (`5E93DEEF941C50FC65AFD9B25B07892A`) совпадает в `hl2rpm/bin`.
  - `check_shader_combos.py`: **`checked 86 shaders, 0 mismatched`**.
  - Плейтест `ck_test` (`1600×900` и `1280×720`, EN и RU): `cl_rpm_fps 1/0` подтверждён визуально в правом верхнем углу (`FPS 43  23.3 ms` в паре с `[tour] 42.7 fps`), вкладки «Графика», «Эффекты» и «Звук и интерфейс» проверены визуально на обоих языках (0 обрезаний, 0 `KeyValues Error` на `OptionsSubVideo.res`), exit `0x00000000`, `config.cfg restored`.

## Полное удаление TAA (оставлен только FXAA) — 2026-11-21 (собрано, проверено в игре)

По запросу пользователя из-за мыла и расплывания в движении полностью удалена система TAA/TAAU (оставлены только FXAA и билинейный растяг `r_hl2rpm_renderscale`):

- **Удалены файлы и шейдеры**:
  - `game/client/deferred/deferred_taa.{h,cpp}`, `materialsystem/stdshaders/hl2rpm_taa.{cpp,_ps30.fxc}`, `materialsystem/stdshaders/hl2rpm_taau.{cpp,_ps30.fxc}`, их `.inc` и `.vcs` в `sp/src/materialsystem/stdshaders/` и `hl2rpm/shaders/fxc/`; убраны из `game_shader_dx9_episodic.vcxproj` и `hl2rpm_custom.txt` (`.vpc` не тронуты).
- **Клиент, шейдеры и UI**:
  - `IDeferredExt.{h,cpp}`: удалены `taaData_t`, `CommitTAAData` / `GetTAAData`, версия интерфейса поднята до `DeferredExtensionVersion015`.
  - `game/client/deferred/viewrender_deferred.cpp`: `r_deferred_aa` ограничен `0..1` (`FCVAR_ARCHIVE`, по умолчанию `1` — FXAA), убран джиттер (`DeferredTAA_JitterView`), трафаретная маска камеры в `DrawViewModels` и `c_baseplayer.cpp` (двухпроходный рендер вьюмоделей для прозрачных деталей сохранён), удалена цель `_rt_hl2rpm_upscalemask` из `deferred_rt.{h,cpp}`; при `r_hl2rpm_renderscale < 1` после `DrawDeferredFXAA` выполняется прямое билинейное растяжение `_rt_hl2rpm_scene` в задний буфер.
  - `cdeferred_manager_client.cpp`: `r_deferred_disable_msaa` и отключение MSAA перенесены сюда; `defpass_composite.cpp` передаёт `0.0f` в `c23.w` (без сдвига мипа); `lighting_global.cpp` отключает покадровый дизер контактных теней (`iDither == 1` → `-1.0f`, без TAA).
  - `hl2rpm_options_graphics.cpp`, `hl2rpm_extras/tools/add_loc_tokens.py`, `hl2rpm/resource/hl2rpm_{russian,english}.txt` (синхронизированы в `hl2rpm_extras/resource/`): из списка `Сглаживание` убран пункт TAA (оставлены `Выкл.` и `FXAA`, пресеты Low–Ultra обновлены на `1`), обновлены строки `HL2RPM_Gfx_RenderScale` и `HL2RPM_Gfx_Hint`.
- **Проверки**:
  - Сборка `game_shader_dx9_episodic.vcxproj` и `client_episodic.vcxproj` (`Win32 / Release`): 0 ошибок, MD5 `client.dll` (`FA2381390FF17898CEDE1111F28CB9BF`) и `game_shader_dx9.dll` (`43CB4B2CA1F1E496419E6EF199920B86`) совпадают в `hl2rpm/bin`.
  - `check_shader_combos.py`: **`checked 86 shaders, 0 mismatched`**.
  - Плейтест `ck_test` (`1600×900`, `r_hl2rpm_gpuprof 1`, покой и `+forward`, `r_deferred_aa 0/1`) и `test_deferred` (`1600×900` и `1280×720`): exit `0x00000000`, `config.cfg restored`, 0 ошибок и неизвестных команд в логе, `[gpuprof]` показывает `aa 0.54 ms` (`1600×900`) / `0.24 ms` (`1280×720`) при `r_deferred_aa 1` и `0.00 ms` при `r_deferred_aa 0`, картинка без шлейфов и мыла в движении.

## Фикс «ореола солнца» сквозь потолки и крыши (`volumpass_sun_ps30` / `weather_post_ps30`) — 2026-11-21 (собрано, проверено в игре)

- **Причина**: при взгляде снизу на перекрывающий солнце потолок/козырёк (когда камера стоит в освещённом солнцем воздухе или перед потолком есть освещённый участок дымки) проход объёмного света `volumpass_sun_ps30.fxc` применял к дымке перед потолком узкий прямой лепесток ореола солнца (`0.75 * HG(0.58) + 0.25 * HG(0.82)` с усилением `×6.5`), а в дымке/тумане (`misty`/`fog`) также `weather_post_ps30.fxc` применял на ближних дистанциях прямой солнечный лепесток тумана (`g = 0.62`). В результате прямо в центре тёмного потолка рисовался круглый светлый ореол по направлению на скрытое за потолком солнце.
- **Исправление**:
  - `materialsystem/stdshaders/volumpass_sun_ps30.fxc`: вынесена функция `PhaseHG(g, cosTheta)`; полная двухлепестковая фаза с узким ореолом (`0.75 * HG(0.58) + 0.25 * HG(0.82)`) оставлена только для открытого неба (`depth < 0.1` — просветы, кроны, края крыш), а на пикселях твёрдой геометрии (`depth >= 0.1` — потолки, стены, крыши) используется мягкий лепесток `HG(0.25)` (`phaseDust`), убирающий ореол с перекрытий при сохранении видимости объёмных лучей.
  - `materialsystem/stdshaders/weather_post_ps30.fxc`: прямой солнечный лепесток тумана смягчён (`g = 0.50`) и ограничен дальней дистанцией (`smoothstep(500.0, 2500.0, dist)`), чтобы ближние навесы/потолки в тумане не получали солнечного пятна.
- **Проверки**: `ShaderCompile.exe -ver 30` для `volumpass_sun_ps30.fxc` и `weather_post_ps30.fxc` (0 ошибок, MD5 совпадают в `hl2rpm/shaders/fxc/`), `check_shader_combos.py`: **`checked 88 shaders, 0 mismatched`**. `.vpc` и C++ не менялись. Плейтест `test_deferred` под потолком/козырьком (`setpos 80 1100 -80; setang -65 -125 0` в `misty`, `fog`, `fair`), на открытом небе с краем здания/кроной (`-480 460 0`, `misty`) и утренний луч за стеной (`7.5`, `misty`): ореол на потолке полностью исчез (подтверждено и при 7.5× усилении контраста), лучи на небе и за препятствиями сохранены; exit `0x00000000`, `config.cfg restored`.

## Фикс «залысин» теней на потолках под крышами и в просветах (`deferred_skyvis_ps30` / `deferred_ssao_ps30`) — 2026-11-21 (собрано, проверено в игре)

- **Причина**: после возвращения на штатный `SkyVis` без GI проявился баг в `deferred_skyvis_ps30.fxc` / `deferred_ssao_ps30.fxc`: на крыше шире диска `r_deferred_skyvis_area` (128 ед., и 512 ед. для `skyvis_wide`) в середине не попадалось ни одной точки земли ниже, поэтому `dLow` схлопывался к высоте самой крыши `dSelf`, а `openLow` (`vis.y`) прыгал с ~0 до `1.0`. Для потолков под крышами тоньше 48 ед. (и на некоторых участках пола/навесов при близкой разнице высот с `dLow`) проверка `(uvz.z - vis.w) < 48` пропускала это `1.0`, образуя резкое светлое квадратное пятно по центру потолка с тёмным кольцом по краям. Дополнительно у тонких козырьков (8–16 ед.) `below <= 24` давало `cover = 0` и полностью освещало нижнюю грань как улицу.
- **Исправление**:
  - `materialsystem/stdshaders/deferred_skyvis_ps30.fxc`: добавлен `dTop` (минимум глубины 4×4 в текселе), `openLow` для земли умножается на `hasLower = saturate(((dLow - dTop)*range - 24)/24)` (без пустоты `openVoid`), чтобы в центре сплошной крыши `vis.y` оставался `0.0` и не тянул светлое кольцо через билинейную фильтрацию.
  - `materialsystem/stdshaders/deferred_ssao_ps30.fxc`: в `vLow` и `vLowWide` добавлен множитель `saturate(((vis.w - dColumn)*range - 24)/24)` (нет земли ниже кровли над пикселем → нет открытого неба); для направленных вниз потолков (`normal.z < 0`) добавлен `ceilingCover` (`below > 2..10`), чтобы тонкие козырьки (8–16 ед.) всегда считались под крышей; смещение по нормали увеличено до `max(6.0, g_vecRainMap.z * 1.5)`.
- **Проверки**: `ShaderCompile.exe -ver 30` для `deferred_skyvis_ps30.fxc` и `deferred_ssao_ps30.fxc` (0 ошибок, MD5 совпадают в `hl2rpm/shaders/fxc/`), `check_shader_combos.py`: **`checked 88 shaders, 0 mismatched`**. Плейтест `test_deferred` (козырьки/потолки `-520 420 120`, `-420 750 120` и пол `-550 750 64` с `r_csm_pcss 6`, `r_deferred_skyvis_debug 2`), `city27_test` и `ck_test` (тонкий козырёк 8 ед., переулки, двор): засветы по центру потолков и на полу полностью ушли, exit `0x00000000`, `config.cfg restored`.

## Полное удаление GI (оставлена только видимость неба камеры для пыли и лучей) — 2026-11-21 (собрано, проверено в игре)

По запросу пользователя полностью удалены и старая CPU-система проб GI, и экспериментальный порт Zenteon TurboGI:

- **Удалены шейдеры и цели**:
  - Удалены `deferred_gi_ps30`, `deferred_giblur_ps30`, `deferred_giup_ps30`, `deferred_gi_blend_ps30` (и выборка `sGI` из `lightingpass_global_ps30.fxc` / `lighting_global.cpp`), а также все `turbogi_*` (`turbogi_common_fxc.h`, `turbogi_{depds,rad,calc,temporal,denoise,blend}_ps30.{fxc,inc,vcs}` в репо и в `hl2rpm/shaders/fxc/`, убраны из `hl2rpm_custom.txt`).
  - В `materialsystem/stdshaders/deferred_gi.cpp` сохранён только `DEFERRED_GI_UPLOAD` (`deferred_gi_upload_vs30/ps30`), т.к. его использует `deferred_lightreach.cpp`.
- **Очистка клиентского пайплайна и UI**:
  - `game/client/deferred/deferred_gi.{h,cpp}` сведены к `DeferredGI_UpdateCameraSky` (1 дешёвая трасса мира/кадр в полусфере для `DeferredGI_GetCameraSky()`, передаётся через `giData_t::vecParams2.x` при `bEnabled = false` для пылинок `weather_motes` и интерьерных лучей `volumpass_sun` — оба шейдера читают `vecParams2.x` напрямую) плюс `FCVAR_HIDDEN`-заглушки для старых архивных конваров `r_deferred_gi*` (0 предупреждений `Unknown command` при старом `config.cfg`).
  - `IDeferredExt.h/.cpp` поднят до `DeferredExtensionVersion014` (`giData_t` сокращён до `bEnabled` + `vecParams2`, убран `CommitTexture_GIScene`).
  - В `viewrender_deferred.cpp` убраны проходы `turbogi_dep` и `turbogi` (`DeferredGI_PrepareGBuffer` / `DeferredGI_PostOpaque`), в `PerformLighting` оставлен только вызов `DeferredGI_UpdateCameraSky`.
  - В `deferred_ssao.cpp` возвращён штатный расчёт SSAO + SkyVis (`bSSAO = r_deferred_ssao.GetBool()`).
  - В `hl2rpm_options_graphics.cpp` удалена строка `FX_GI` (`r_deferred_gi`, `#HL2RPM_Gfx_GI`) со вкладки «Эффекты» (теперь 11 пунктов и пересчитанные пресеты).
- **Проверки**:
  - `check_shader_combos.py`: **`checked 88 shaders, 0 mismatched`**. `.vpc` не тронуты.
  - Сборка `game_shader_dx9_episodic.vcxproj` и `client_episodic.vcxproj` (`Release|Win32`): 0 ошибок, MD5 совпадают с `hl2rpm/bin`.
  - Плейтест `c27_station` (`1280×720`, `r_hl2rpm_gpuprof 1`, `+forward`) + UI (`ui_clicks.ps1`): exit `0x00000000`, `config.cfg restored`, 0 ошибок в `console.log`, пылинки под навесом и SSAO + SkyVis работают, `turbogi*` в `[gpuprof]` отсутствуют.

## Фикс стоковых вьюмоделей на test_deferred + порт всех видов воды Portal 2 под Deferred и тестовая карта p2_water_test — 2026-11-20 (скомпилировано, проверено в игре)

Выполнены два запроса пользователя:

1. **Убраны стоковые вьюмодели HL2 (`hl2_c`) для `weapon_pistol` и `weapon_smg1` на `test_deferred` и картах с протагонистами Mapbase**:
   - **Причина**: `test_deferred.bsp` вызывает `!player SetProtagonist gordon` / `police`, наследующие блок `"BASE_hl2_c_arms"` из `hl2rpm/scripts/protagonists/protagonists_hl2_base.txt`. В `wpn_viewmodels_c` были прописаны `"weapon_pistol" "models/weapons/hl2_c/v_pistol.mdl"` и `"weapon_smg1" "models/weapons/hl2_c/v_smg1.mdl"`, которые подменяли наши порты из CS:GO стоковыми моделями из `shared_content_v7_0.vpk`.
   - **Исправление**: удалены обе строки из `hl2rpm/scripts/protagonists/protagonists_hl2_base.txt` (и добавлена автоматическая очистка в `hl2rpm_extras/tools/make_csgo_weapons.py:remove_old_stock_overrides()`); проверено отсутствие старых `v_pistol`/`v_smg1` в `hl2rpm/models/weapons/` и `hl2rpm/custom/`.
   - **Проверка (`run_game.ps1 -Map test_deferred`)**: проверены `SetProtagonist gordon` и `SetProtagonist police` с `weapon_pistol` и `weapon_smg1` — отображаются только наши HK USP Match и MP7 Альянса в пресете CS:GO «Классический» с корректными `c_arms`.

2. **Порт всех 8 типов воды/слизи Portal 2 (`E:\Steam\steamapps\common\Portal 2`) под deferred-рендер HL2RPM + тестовая карта `p2_water_test`**:
   - **Текущая вода Portal 2 / L4D2 (`FLOWMAP`) в `stdshaders`**:
     - `materialsystem/stdshaders/SDK_water_ps2x.fxc` (новый статик-комбо `FLOWMAP 0..1 [ps20b]`, сэмплы `s8..s10`, константы `c14..c18`; `SKIP` с `MULTITEXTURE`, `BASETEXTURE`, `BLURRY_REFRACT`, `CONVERT_TO_SRGB`), `water_ps2x_helper.h` (двухфазная `FlowUV` с фазовым шумом, мировые UV `x/-y`, непрозрачный режим `FLOWMAP && !REFRACT` с мягким `LightSampler`, слой `$flow_colortexture` + `$flow_color_tint` + ослабление Френеля по матовой альфе пены/слизи), `water.cpp` (параметры `$flowmap`, `$flow_*`, `$flow_colortexture`, `$color_flow_*`, `$flow_color_intensity`, `$flow_color_opacity`, `$flow_color_tint`, масштабирование `$flow_color_intensity` по `flWorld` времени суток — ночью не светится, но освещается лампами deferred).
     - **Критическая находка Source 2013**: на HDR-картах (`mat_hdr_level 2`) имя шейдера `"Water"` в VMT перехватывается стоковым `stdshader_dx9.dll` (`Water_DX9_HDR`) в обход `game_shader_dx9.dll` (`SDK_Water_DX9_HDR`), тогда как на LDR-картах попадало в `SDK_Water_DX90`. Использование `"SDK_Water"` в VMT гарантирует наш `SDK_Water_DX9_HDR` (с автоматическим fallback на `SDK_Water_DX90` для LDR) и полностью совместимо с `vbsp`, `viewrender_deferred.cpp` и `weather_render.cpp`.
   - **Уникальные материалы (`hl2rpm/materials/hl2rpm/water/p2_*.vmt`, текстовые копии в `hl2rpm_extras/materials/hl2rpm/water/`, lossless VTF 7.5→7.4 в `hl2rpm/materials/hl2rpm/water/p2_tex/`)**:
     1. `hl2rpm/water/p2_aperture_goo` (`nature/toxicslime002a`) — классическая вихревая токсичная слизь Aperture;
     2. `hl2rpm/water/p2_overgrown_slime` (`toxicslime_a1_intro5` / `a2_laser_over_goo`) — густая мшистая жёлто-зелёная слизь заросших камер;
     3. `hl2rpm/water/p2_old_aperture_slime` (`toxicslime_a3_03` / `a3_bomb_flings`) — тёмная ржаво-масляная жижа старых шахт Aperture;
     4. `hl2rpm/water/p2_underground_cave` (`liquids/water_underground_cave`) — полупрозрачная пещерная жижа (преломление мелководья + текущая слизь);
     5. `hl2rpm/water/p2_swamp_murky` (`liquids/water_swamp_m2`) — мутная янтарная болотная вода;
     6. `hl2rpm/water/p2_bts_industrial` (`water_bts_01` / `a1_intro3` / `a2_laser_intro`) — холодная оливково-серая индустриальная вода BTS;
     7. `hl2rpm/water/p2_glados_chamber` (`nature/water_glados_01`) — мелкая затопленная вода камер с сильным преломлением (`0.52`);
     8. `hl2rpm/water/p2_bottomless_pit` (`nature/water_bottomless_pit_01`) — тёмная турбулентная вода бездны с плотными вихрями (`scale 8`, `noise2`);
     - Подводные (`$bottommaterial`): `hl2rpm/water/p2_goo_beneath`, `hl2rpm/water/p2_swamp_beneath`.
   - **Генератор и тестовая карта**: `sp/src/hl2rpm_extras/tools/make_p2_water.py` → `E:\XBLAH's modding tool\content\Mapbase\hl2rpm\mapsrc\test\p2_water_test.vmf` → `E:\Steam\steamapps\sourcemods\hl2rpm\testmaps\maps\p2_water_test.bsp` (8 пронумерованных бассейнов по 2 рядам с общей ватерлинией `Z=0`, ступеньками для захода и ныряния `0..-112`, колоннами/бочками/подводными блоками, полунавесом с тёплыми deferred-прожекторами для проверки солнца/тени/ночи и подсвечиваемыми табличками в стиле Aperture).
   - **Проверки**:
     - `ShaderCompile.exe -ver 20b SDK_water_ps2x.fxc` (0 ошибок, установлен `hl2rpm/shaders/fxc/SDK_water_ps20b.vcs`), `MSBuild` `game_shader_dx9_episodic.vcxproj` (`Release|Win32`: 0 ошибок, MD5 совпадает в `hl2rpm/bin/game_shader_dx9.dll`), `check_shader_combos.py`: **`checked 92 shaders, 0 mismatched`**.
     - Компиляция `p2_water_test.bsp` (`compile_map.ps1 -Profile normal`, 17 с): 0 утечек, 0 ошибок.
     - `run_game.ps1 -Map p2_water_test` (`1600×900`, 14 кадров: обзор, все 8 бассейнов, пара `t=0` / `t=+2s` на `p2_aperture_goo` с `mean abs diff 9.33 / 255` по всей площади слизи, подводные `p2_goo_beneath` и `p2_swamp_beneath` с реальной глубины `Z=-40`, ночной тест `23:00` с прожекторами и табличками): exit `0x00000000`, `config.cfg restored`, 44–57 FPS, `r_deferred_list_materials` подтвердил `SDK_Water_DX9_HDR` на всех 10 материалах воды и `DEFERRED_BRUSH` на 8 табличках, 0 ошибок, все кадры осмотрены визуально.

## Замена стандартных SMG1 и USP Match на порты из CS:GO (MP7 с логотипом Альянса и голографическим прицелом + переработанный HK USP Match) — 2026-11-20 (скомпилировано, проверено в игре)

Полностью вырезаны и заменены стоковые `weapon_smg1` (`v_smg1`/`w_smg1`) и `weapon_pistol` (`v_pistol`/`w_pistol`) на модели из CS:GO (`E:\Steam\steamapps\common\csgo legacy\csgo\pak01_dir.vpk`), адаптированные под deferred PBR-рендерер, `c_arms` (`uses_hands = 1`), 3D-карточки инвентаря и звуки CS:GO. Старые переопределения текстур (`materials/models/weapons/{v_smg1,w_smg1,v_pistol,w_pistol}`) удалены.

- **MP7 (`weapon_smg1`, `v_smg1.mdl` / `w_smg1.mdl`, `16 834` треуг. = `15 782` базовых CS:GO `v_smg_mp7` после удаления торчащих целиков + `1 052` кастомного голографа и сложенных BUIS)**:
  - На оба борта верхнего ресивера (левый `PanelB_left` на игрока и правый `PanelC_right`) нанесён потёртый трафарет **логотипа Альянса (Combine Claw)** с маркировкой `CMB / C27 // OW`.
  - На верхнюю планку Пикатинни установлен **тактический голографический прицел** (бронекожух с двойной рамкой, QD-креплением, батарейным блоком, задней панелью `[-] NV [+]` со светящимся голубым LED, барабанами поправок, полупрозрачной антибликовой линзой `mp7_holo_glass` и аддитивной 65-MOA красной маркой EOTech `mp7_holo_reticle`, центрированной на `Z = −0.96, X = −0.06` под оптическую ось от бедра).
  - Материалы `materials/models/weapons/hl2rpm/mp7/`: `mp7_body`, `mp7_holo` (`DEFERRED_MODEL`, VTF v7.2 + DX-нормали с PBR gloss-маской в альфе, `$pbr_roughness 0`, `$pbr_glossmask 1`, `$selfillum` для LED), `mp7_holo_glass` и `mp7_holo_reticle` (`UnlitGeneric`, `$additive 1`, `$translucent 1`).
- **HK USP Match (`weapon_pistol`, `v_pistol.mdl` / `w_pistol.mdl`, `11 508` треуг. = `9 862` базовых CS:GO `v_pist_223` без глушителя/стоковых целиков/УСМ + `1 646` кастомных деталей Match, цветовая палитра оригинального HL2 `v_pistol_sheet.vtf`)**:
  - **Серебристый затвор (`usp_slide`, выделенный `DEFERRED_MODEL`, `$pbr_metalness 0.78`)**: шлифованная нержавеющая сталь в стиле оригинального HL2 (локально-контрастный ремап с сохранением тёмных задних насечек, потёртостей и тёмной лазерной гравировки **`"USP MATCH / 9mm x 19"`**).
  - **Полимерная рамка и рукоятка (`usp_body`, `DEFERRED_MODEL`, `$pbr_metalness 0.18`)**: матовый чёрно-графитовый полимер HK.
  - **Удлинённый ствол 6.02"** из полированной нержавеющей стали (`Z = 6.72..9.56`, дульная фаска 11°, тёмный канал, уплотнительное кольцо; аттачмент `muzzle` вынесен на `Z = 9.65`).
  - **Дульный груз-компенсатор (barrel weight / compensator)** из тёмной воронёной/графитовой оружейной стали (`usp_match_steel`, `$pbr_metalness 0.74`) в стиле насадки оригинального HL2 — с подствольной планкой, светло-стальными шестигранными болтами, двойными боковыми окнами, антибликовой верхней насечкой и зазором под откат затвора.
  - **Спортивный УСМ**: изогнутый матчевый спусковой крючок с блестящим винтом ограничения хода (на кости `Trigger`) + тёмный графитовый скелетонизированный матчевый курок со сквозным вырезом (на кости `Hammer`).
  - **Регулируемые матчевые прицельные приспособления** (на кости `Slide`): открытый прямоугольный целик с микрометрическими винтами вертикали/горизонтали и двумя зелёными светящимися точками + высокая подрезанная матчевая мушка со светящейся точкой (`usp_match_sights`).
- **Двухпроходный рендер вьюмоделей в deferred (`client.dll`, Win32/episodic/Release)**:
  - `game/client/deferred/viewrender_deferred.cpp` (`CDeferredViewRender::DrawViewModels`): модели из `opaqueViewModelList` рисуются с флагом `STUDIO_TWOPASS` в G-буфер и композит (`DeferredTAA_MarkCameraAttached(true)`), после чего при `DeferredTAA_MarkCameraAttached(false)` дорисовываются их прозрачные/аддитивные меши (`STUDIO_RENDER | STUDIO_TWOPASS | STUDIO_TRANSPARENCY`, если `IsTranslucent || IsTranslucentTwoPass`). Стекло и сетка голографа не портят G-буфер и не ставят маску TAA `0x80` поверх мира за стеклом.
  - `game/client/c_baseviewmodel.cpp`: `pWeapon->ViewModelDrawn(this)` защищён `&& !(flags & STUDIO_TRANSPARENCY)` от двойного вызова.
- **Инструментарий и ресурсы**:
  - Генератор: `sp/src/hl2rpm_extras/tools/make_csgo_weapons.py` + пакет `csgo_weapons/{csgo_core,mp7_builder,usp_match_builder}.py` (включая точный декодер CS:GO MDL v49 / `.ani` `STUDIO_FRAMEANIM` `Quaternion48S` + `Vector48`, ретаргетинг на `ValveBiped` `c_arms`, генерацию deferred VTF v7.2 и конвертацию звуков CS:GO в 16-bit mono WAV). Исходники SMD/QC/PNG: `E:\XBLAH's modding tool\content\Mapbase\hl2rpm\modelsrc\hl2rpm_weapons\{mp7,usp_match}\`.
  - Скрипты и звуки в моде: `scripts/weapon_smg1.txt`, `scripts/weapon_pistol.txt` (`uses_hands 1`, `viewmodel_fov 54` — пресет CS:GO «Классический»), `scripts/game_sounds_weapons.txt` (секция `make_csgo_weapons.py`), `sound/weapons/{hl2rpm_mp7,hl2rpm_usp}/`.
- **Проверки**:
  - `MSBuild` `client_episodic.vcxproj` (`Release|Win32`): 0 ошибок, `client.dll`/`client.pdb` обновлены в `hl2rpm/bin`; `check_shader_combos.py`: `92 shaders, 0 mismatched`.
  - `run_game.ps1` на `ck_test` (`1600×900`, 5 прогонов: горизонтальные ракурсы, солнце/тень, выстрелы, перезарядки, все `c_arms` — Citizen/HEV/CP/Soldier/Hazmat, `r_csm_pcss 8` G-буфер, `shot,spin` TAA за стеклом голографа, третье лицо, NPC солдат Альянса, дропнутые модели на полу и 3D-карточки инвентаря `rpm_menu inv` для MP7 и USP Match): exit `0x00000000`, `config.cfg restored`, 0 ошибок в `console.log`, `r_deferred_list_materials` подтвердил `DEFERRED_MODEL` для всех 6 непрозрачных (`mp7_body`, `mp7_holo`, `usp_slide`, `usp_body`, `usp_match_steel`, `usp_match_sights`) и `UnlitGeneric` translucent для стекла/сетки; все скриншоты проверены визуально.

## Сити-27: мировой атлас — 2026-11-17 (схема собрана, на согласовании)

- Главный результат этапа 1: `hl2rpm_map_developing/city27_world_atlas.png` (9108×7436), локальный просмотрщик `city27_world_atlas.html`, 18 увеличенных `city27_zone_details/*.png` и подробное пояснение `city27_world_plan.md`. `city27_scan_top.png` — новый Cycles-вид семи сканов сверху; `city27_blender_alignment.png` — сканы с игровыми клетками и теми же переходами. Прежний отвергнутый `city27_world_concept` больше не направление.
- Единый источник — `hl2rpm_extras/tools/world/world_layout.json` v2: **18 игровых BSP, Нексус, 25 переходов**, область 2740×2660 м, максимум 624×624 м при HU-пересчёте HL2RPM. Альтернативный `city27_plan.py/render_master.py` — ошибочный неутверждённый эксперимент, не использовать как основу. `render_world_atlas.py` перегенерирует атлас; workflow — `hl2rpm_extras/tools/world/README.md`.
- В атласе не заглушки: по OSM отрисованы 20 875 полигонов building и 7634 дорожных way (число контуров, не уникальных вручную подтверждённых домов), реальные пропорции XY сохраняются локально. Состав: 9 исходных районов + 9 предложений знаком «+». Пустошь отделена от города реальными полями/лесами/дорогами; `fields` специально перенесён из плотного центра Печеля к полям и фермам восточнее (`47.49112,19.37468`). Все 18 районов имеют OSM JSON в `hl2rpm_map_developing/osm_world`, Overpass дополнен прямым OSM API (`fetch_world_osm_api.py`).
- `city27_world_resolved.json` содержит локальные origin карт и **50 концов переходов в HU**; для каждого найдена ближайшая реальная улица/рельса/канал. Короткий пунктир в атласе — новый коридор сжатия, а не реальная улица. `city27_connector_audit.json` не доказывает проходимость; большие новые коридоры (каналы/ж/д/ворота) надо уточнять на greybox.
- Цитадель зафиксирована как предложение: мировая ось `(0,0)` / Blender примерно `(14.47,2.37)`, H=600 м, охраняемый радиус 200 м, общий большой скан sky 1:64. Формула и позиции `sky_camera`, размеры скана (примерно 16476×29600 HU) и честные оговорки о видимости — в `city27_world_plan.md`. Это единое направление/угловой размер, не доказанная видимость сквозь здания.
- `.blend` просмотрен без сохранения и не изменён: в 20 объектах нет имён/text objects `City-26`, нужные сканы уже `rpm_c27_*`/`rpm_example_c27_*`. Street View и точные фасады не исследовались: атлас — топографический/архитектурный референс, не доказательство высот и фасадов.
- Проверки: `world_check.py` — OK (18 карт, 25 переходов, лимиты/порядок/связность/Нексус); validator атласа — 18 PNG деталей, 25×2 endpoint ≤±16384 HU, все PNG читаются, все районы имеют OSM, все обязательные уличные/ж/д/водные связи нашли реальные опорные объекты; Python-код компилируется. VMF/BSP не создавались, сборка/игра не требуются и не запускались. **Следующий шаг только после одобрения пользователя:** greybox `c27_station`, затем по порядку из плана.

Обновлено **2026-11-12** (City-26 и оптимизация производительности, см. разделы ниже); до этого — 2026-11-10 после проверки файлов, git-истории, памяти и последней переписки предыдущего агента. Это точка входа для следующей модели; подробности — в [PROJECT.md](PROJECT.md) и темах.

## Свет ламп без «круга обрезки» и ck_test как двор City 17 — 2026-11-12 (собрано, проверено в игре, закоммичено и запушено 2026-11-12)

Запрос: доделать незавершённое от 4 ноября, «особенно culling источников света — видна круговая линия их обрезки, свет просто прерывается».

- **Свет** (подробно — [topics/renderer-core.md](topics/renderer-core.md), «спад света ламп без круга обрезки»): общая функция спада `DeferredLightFalloff` в шейдерах ламп — свет доходит до нуля к радиусу плавно при любой степени; лампы из классических `light`/`light_spot` (и скомпилированные worldlights) получают родное затухание движка 1/(c+l·d+q·d²) вместо `(1−d/r)^1` с яркостью «как на 100 ед.» по всему радиусу (откат — `r_deferred_convert_engine_falloff 0`, archive, перезагрузка карты); мягкий предел пика яркости до конуса — у пересвеченных прожекторов снова видна полутень; коробка выбора полноэкранного прохода точечной лампы расширена под её сферу-меш (страховка). Шейдеры `lightingpass_point/spot_ps30`, `volumpass_point/spot_ps30` (92 shaders, 0 mismatched), client.dll, server.dll. Цена — в пределах шума.
- **Изменение вида**: конвертированные лампы теперь гаснут как в vrad — пятна света под фонарями плотнее, между ними на картах с классическими лампами (вокзал) заметно темнее, чем было. Собственные `light_deferred*` выглядят как раньше. Если пользователь захочет прежнюю яркость между лампами — convar выше.
- **ck_test** (п.5 от 4 ноября): двор окружён домами City 17 — генератор `hl2rpm_extras/tools/make_ck_city17.py`, подробности в [topics/mapping.md](topics/mapping.md). Скомпилирован (normal, без утечек), осмотрен днём/ночью; NPC и всё из CK на местах; ~18–29 мс в окне 1600×900. Старый `maps/ck_test.bsp` от 8 ноября — в `map_backups`.
- **Hammer++** (п.1 от 4 ноября): build 8873 уже стоит (`hammerplusplus_dll.dll` от 27.10, прежняя — `.bak_8871`). П.2–4 (английская озвучка ГО, Fish Audio, частота общих фраз) были сделаны 4 ноября (см. ниже).
- Не сделано: 3D-скайбокс с Цитаделью для ck_test; дуга света у прожектора (−3596, −5292) на вокзале не объяснена (не спад, не объём, не тени — вероятно тень предмета). Список «дальше» оптимизации (R32F каскадов, кэш каскадов, кубы точечных ламп, TAAU в движении) не трогался.

## City-26 — 2026-11-12 (первый greybox собран и проверен, закоммичен и запушен 2026-11-12)

Запрос пользователя: один BSP по трём 3D-сканам центра Будапешта из `E:\Google Map 3D Exporter v3.0\city-26-prototype.fbx`; чистая ровная геометрия без дыр, dev-текстуры, вода, без интерьеров.

- Создана **новая** карта `city26_test`. Рабочая VMF — `E:\XBLAH's modding tool\content\Mapbase\hl2rpm\mapsrc\test\city26_test.vmf`; BSP установлен в `E:\Steam\steamapps\sourcemods\hl2rpm\testmaps\maps\city26_test.bsp`. Контрольная VMF — `hl2rpm_extras/mapsrc/city26_test.vmf` в git-корне.
- FBX прочитан отдельным фоновым Blender; пользовательская несохранённая сцена Blender и исходный FBX не менялись. Три меша: 1 177 990 вершин, 950 586 треугольников. В игру **не импортировались** повреждённые треугольники и Google-текстуры.
- Ручная регуляризация контуров по z-buffer проекциям: 24 корпуса/квартала, вокзал, открытые дворы и наружные проходы, улицы/тротуары, двухуровневая набережная, шесть лестниц и T-причалов, вода `hl2rpm/water/murky`, sky-оболочка, солнечный свет, спавн, контрольные точки. Интерьеров/NPC/фасадной детализации нет. Это **грубый геометрический прототип**, а не точное воспроизведение всех зданий скана.
- Генератор, планировка, manifest референса и проверки: `hl2rpm_extras/tools/city26/`; читать его `README.md` перед продолжением. XY: `(FBX.xy - [-1.5,-3.0])*1024`, игровой масштаб, **не подтверждённое 1:1**; высоты нормализованы, улица z=128, нижняя набережная 32, вода -128. Все координаты внутри ±16384.
- Финал: **877 brush, 5631 BSP-грань, 9400 плоскостей**, BSP 4 589 524 байта; `vbsp+vvis+vrad -both` normal, без leak и без переполнений, T-junction indices 15,6 %. VMF-валидатор: 877/877 замкнутых выпуклых объёмов, 0 ошибок. BSP contents: 47/47 проверок, включая 17 проходов во дворы.
- Игра: обзор/улицы/вода сверху и снизу; финальный 1080p прогон 13 шагов, exit 0, конфиг восстановлен. Ходьба **без noclip**: лестница подняла глаз z≈96.8→192.0, ворота пройдены x=6646→7127. Исправлены резкие стыки крыш и перекрытый соседним зданием проход. В финальном прогоне ~55–64 FPS (текущие сохранённые настройки, не отдельная сертификация пресета Ultra).
- Проверки/лог хранятся в `hl2rpm_extras/tools/city26/verification/`; итоговые фото — `C:\Users\mikeo\AppData\Local\Temp\opencode\city26\final`. VRAD пишет известный nonfatal assert `color_conversion.cpp (615)` при leaf ambient. Built cubemaps пока нет. При старте игры перед самой картой есть старое `map load failed: default not found or invalid`, затем City-26 успешно загружается.
- Дальнейшая полировка: индивидуальные силуэты/фасады, реальная кровля вокзала, убедительные границы/3D skybox, текстуры, пропы/зелень, навигация и наполнение. Это не выполнено в рамках первого greybox. **Не регенерировать поверх ручных правок пользователя** без сравнения; копия в `hl2rpm_extras/mapsrc` — снимок версии, а не автоматическая синхронизация.
- Существующие карты/код/DLL/шейдеры не менялись; коммит и пуш — вместе с остальной работой 2026-11-12.

## Оптимизация производительности — 2026-11-11/12 (собрано, проверено в игре, закоммичено и запушено 2026-11-12)

Запрос: 60+ FPS в 1920×1080 на «Ультра» на GTX 960M без видимых потерь; хуже всего на улице и на `rpm_trainstation_testoptimization`. Подробности, convar'ы и ловушки — [topics/renderer-passes.md](topics/renderer-passes.md), раздел «Производительность 2026-11-11/12».

- **Профайлер** `r_hl2rpm_gpuprof 1`: GPU-время каждого прохода (таймстемпы D3D9; устройство найдено через ресурс D3D в глобалах `shaderapidx9`) + `[cpuprof]` главного потока + `[svprof]` сервера. Сценарии и сводка: `hl2rpm_extras/tools/perf/` (`perf_run.ps1`, `perf_report.py`, `perf_*.txt`).
- **Без изменений картинки** (каждое — пиксельный A/B на уровне шума A/A, convar отката): теневые виды на списках листьев без vis на лампу; кэш карт теней неподвижных прожекторов; ранний выход неба на закрытых пикселях; фильтр тени ламп только где свет доходит; цикл каскадов 8 → 4; пропуск мёртвого поста движка; общие списки мира G-буфера и композита. CPU главного потока на вокзале ~40 → ~20 мс, GPU ~40 → ~31 мс.
- **Карта**: `hl2rpm\maps\rpm_trainstation_testoptimization.bsp` пересобрана vbsp+vvis (без vrad, как была; старая — `map_backups\rpm_trainstation_testoptimization_novis_20261108.bsp`).
- **Масштаб рендера + TAA-апскейл** (по выбору пользователя): `r_hl2rpm_renderscale` (по умолчанию **0.75**; меню «Графика» → «Масштаб рендера», в пресетах), 3D рисуется в `_rt_hl2rpm_scene`, все экранные deferred-цели — его размера, `hl2rpm_taau_ps30` собирает 1080p. Резкость ~94 % нативной; вода/оружие/HUD/дождь/ночь проверены.
- **Итог 1080p «Ультра» (с апскейлом 75 %)**: вокзал 20–24 мс (42–49 FPS; было ~35 мс / 28 FPS), интерьер `building_test` ~19 мс (52 FPS; было 27 мс), `water_test` ~17 мс (60 FPS). **60 FPS везде не достигнуто**: на большой карте упор в CPU рендера (~17–20 мс: каскады, проходы сцены дважды), на GPU самый дорогой проход — солнце (~2240 инструкций/пиксель).
- Ноутбук пользователя при замерах разряжался на зарядке (`Discharging True` при `PowerOnline True`) — провалы кадров в отдельных шагах.
- Не сделано / дальше: R32F только для каскадов с поиском блокеров, кэш каскадов в покое, CPU проходов сцены, кэш точечных ламп (кубы), проверка апскейла в движении (знак джиттера `r_deferred_taau_jitter_sign`), стекло/зеркала при масштабе.

## Главное

- Проект — рабочая база одиночной RPG и Creation Kit для наполнения мира. Последний фокус — CK, NPC/озвучка, демонстрационная карта и исправление света.
- Вся работа с 1 по 12 ноября (CK M0–M7, меню, костюмы, анимации, озвучка, Hammer++ в CK, перенос AI-инструкций, City-26, оптимизация, свет ламп, ck_test City 17) **закоммичена одним коммитом и запушена 2026-11-12** в `master` (см. `git log`; предыдущий — `4d0ba29` от 2026-10-31).
- Файлы памяти обрываются утром 4 ноября, но работа продолжалась **позже в тот же день**. Поэтому нельзя завершать обзор на озвучке/Hammer-интеграции.
- Пользовательский проект `.hl2dlg` и VMF `ck_test` имеют изменения **8 ноября**. Не перезаписывать их старым генератором/бэкапом без сравнения с текущими файлами.

## Где остановилась последняя задача

В последнем большом запросе 4 ноября пользователь просил:

1. Расставить новый распакованный билд Hammer++ по рабочим папкам.
2. Дополнить недостающие английские реплики и озвучку ГО до русской версии.
3. Пока использовать Fish Audio.
4. Убрать слишком частые/одновременные одинаковые общие фразы рабочих и гражданских.
5. Переделать `ck_test` под красивую демонстрацию возможностей: лаконичная архитектура City 17, пол из серой dev-плитки, сравнение с оригинальными постройками и визуальная проверка.
6. Исправить исчезающие/появляющиеся лампы и свет с обратной стороны толстой стены.

**Запрос не имеет подтверждённого общего завершения.** Связь с моделью оборвалась во время проверки рендера; затем пользователь объяснил, что 11–13 FPS были вызваны неработавшей зарядкой ноутбука, и попросил продолжать. Это исторический контекст, а не новая команда на реализацию в сессии переноса документации.

### Что удалось подтвердить

**Английская озвучка ГО.** В переписке 4 ноября есть результат генерации/применения пяти новых фраз и проверка сцены `scenes/dialogs/ck_guard/english/0.vcd` со звуком в игре. Предыдущий полный набор: 121 уникальная фраза / 195 мест; после дополнения отчёт инструмента — 126 фраз / 200 файлов. Это счётчики конкретного прогона, не пересчитанный текущий проект 8 ноября.

**Частота фраз NPC.** В `game/server/hl2rpm_audio.cpp` присутствуют общая память произнесённых текстов, речевой семафор, паузы между NPC и редкий общий idle:
- `rpm_bark_gap = 2.0`;
- `rpm_bark_repeat = 150` (для срочных — 20 с);
- `rpm_bark_idle_gap_min/max = 18/45`.

В логе прежнего прогона видны разнесённые приветствие и idle. Есть собранный `server.dll` от 4 ноября 14:40. Этого недостаточно для утверждения, что весь текущий набор реплик принят пользователем.

**Свет ламп — Light Reach.** Последняя новая система находится в `game/client/deferred/deferred_lightreach.cpp/.h`, с подключениями в менеджере ламп, RT, bounce и шейдере `lightingpass_point_ps30.fxc`:
- для точечной лампы трассируются свободные расстояния до BSP-стен и displacement по 16×16 направлениям на каждой из шести граней;
- карты размещаются в атласе 16×32 слота (512 ламп), читаются шейдером через `s15`;
- свет, его ambient и bounce ослабляются за первой стеной; границы досягаемости используются и для отсечения лампы;
- движущиеся/кратковременные лампы имеют отдельные ограничения; динамические пропы и двери в этих world-only трассах не участвуют;
- bounce плавно появляется/гаснет; есть обработка потери RT и очистка при смене карты;
- команды: `r_deferred_lightreach`, `_debug`, `_reset`; бюджеты `_budget_ms`, `_load_ms`, `_range`, `_fade`.

В старом выводе: **91 shaders, 0 mismatched**; DLL и `.vcs` созданы 4 ноября около 15:03–15:04. Один прогон дошёл до логов атласа, но был по таймауту; контролируемый A/B завершился с кодом 0 и сообщением «15 steps», **без сохранённой в этом output итоговой оценки кадров/FPS**. Пользователь объяснил аномальные 11–13 FPS питанием ноутбука. **Не считать исправление света визуально и по производительности окончательно подтверждённым.**

**Hammer++ и переделка карты.** В последних сообщениях нет общего отчёта о выполнении этих пунктов. По одной дате exe нельзя определить установленный билд. Текущий `ck_test.vmf` изменён 8 ноября, а проверенный BSP в `testmaps/maps` датирован 1 ноября. Это не доказывает отсутствие других BSP, но означает, что нельзя объявлять нынешнюю геометрию скомпилированной и проверенной. Нужна отдельная сверка с текущими пользовательскими файлами.

## Что было сделано перед последней незавершённой задачей

По датированным отчётам предыдущего агента, с подтверждённым наличием основных модулей:

- **30–31 октября:** редактор диалогов вырос в Creation Kit; слой мира, фракции, пакеты распорядка, навигация; план расширения M0–M7.
- **31 октября — 1 ноября:** предметы/списки лута/торговля/замки/переходы, общие условия/действия/события, квестовые цели и HUD-маркеры, FNV-подобный персонаж, музыка/барки/субтитры, интерфейс RPMUI, терминалы и мини-игра взлома, превью света/анимаций. Затем темы диалогов, кражи, мастера CK, 3D-карточка предмета, манекены, мебель и сидение/лежание игрока, единое экипированное оружие, HUD костюмов, язык. Отчёты заявляют сборку и проверки; в текущей сессии это не перепроверялось запуском игры.
- **2 ноября:** главное меню и музыка, Steam-ресурсы, импорт моделей, сложенные костюмы, рабочая/химзащитная экипировка, руки по костюму, Bink-интро, зомби в химзащите.
- **3 ноября:** Beretta 92 из ресурсов CS:S с переносом анимаций на ValveBiped; исправление `CUtlMap` с 16-битным индексом на карте с >65 535 рёбер; NPC уступают дорогу, подбирают оружие, реагируют на оружие гражданского, используют кулаки/жесты; библиотека анимаций четырёх скелетов, Kimodo-клипы, третье лицо.
- **Ночь 3–4 ноября:** Fish Audio — основной TTS, Qwen3-TTS/Colab — запасной; озвучка `ck_test`, радио/респиратор, темы и приветствия голосом конкретного NPC, экспорт и липсинк.
- **Утро 4 ноября:** AI-метки `rpm_claude_task` / `func_rpm_claude_area`; Hammer++ из CK; геометрия прямо из VMF и автообновление без BSP; список задач и снимки VMF; огромный `CLAUDE.md` разложен на 16 тем. Текущий exe CK датирован 4 ноября 04:22.

## Известные ограничения и вопросы

- Русские ударения Fish: проверка на слух `voice_gen/fish_stress_check.html` осталась без зафиксированного решения пользователя.
- Звуки UI/RPG: Fish предназначен для речи; в старой сессии были предложены синтез/другой SFX-инструмент. Не выдавать предложение за выполненную генерацию.
- Сохранения карты с длинным именем `rpm_trainstation_testoptimization` не работают из-за лимита 31 символ; это отдельно от исправленного переполнения карты рёбер.
- x64/MP-ветка и DXVK ранее исследованы и отложены/отклонены. Не повторять перенос без нового запроса; см. `topics/renderer-passes.md`.
- Записи о завершённом M0–M7 не доказывают, что весь мод или пользовательская игра закончены: это этап инструментов и демонстраций.

## Перенос инструкций — 2026-11-10

Выполнено в текущей сессии:
- создана общая база `AI_instructions`, независимая от модели и механизма skills;
- перенесены 16 тем, 4 skills со скриптами, импортированы 31 файл памяти; собраны проектные справки и исторические планы;
- добавлены общие `AGENTS.md` и входы Claude/Gemini/Copilot, описан прямой способ чтения для любого клиента;
- сохранены исходные данные и полный резервный архив; старые клиентские пути skills ведут к общей базе;
- сверен статус не только с памятью, но и с более поздней перепиской/исходниками.

Проверки переноса прошли: ссылки, контрольные суммы, синтаксис, чтение VMF/BSP, построение снимков VMF и доступность через старые/новые пути. Игровой код и ресурсы из контрольного снимка не изменились. `dialog_check.py` для текущего ГО дал 0 ошибок и одно предупреждение; это чтение старой утилитой, не изменение/полная проверка диалога. Подробности — `MIGRATION.md`.

Сессия меняет организацию инструкций. Повторной сборки/запуска игры и выполнения старого запроса про карту/лампы в ней не было. Результаты проверки структуры и сохранности записаны в [MIGRATION.md](MIGRATION.md).

## Источники и степень уверенности

- Текущие исходники и `git status/log` проверены 10 ноября.
- [history/MEMORY.md](history/MEMORY.md), датированные статусы CK, анимаций, Beretta, озвучки и Hammer описывают более ранние результаты от имени предыдущего агента.
- Продолжение восстановлено из переписки `687673d6-217f-430a-8587-89acd53b9a64`, сообщения 4 ноября. Полные JSONL-чаты (сотни МБ, возможны секреты и логи) в базу не копировались.
- Последний A/B output: прежняя временная папка сессии, `tasks/bu4q0bn27.output`: exit 0, восстановленный `config.cfg`, 15 шагов; без итогового визуального заключения.
- Даты файлов — ориентир поиска изменений, а не доказательство успешного теста или автора правки.
