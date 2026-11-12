# motion-bricks.cpp для HL2RPM: сборка, интерфейс, данные (2026-11-03)

motion-bricks.cpp (https://github.com/localai-org/motion-bricks.cpp, коммит `60a5f4c`, 2026-10-19) — порт NVIDIA MotionBricks на C++23/GGML. Он генерирует движение 34-суставного скелета Unitree G1 (30 FPS). Проект собран **нативно под Windows** (MSVC v143 + Ninja, только CPU), работает, тесты проходят. Примеры для всех 15 стилей лежат в `hl2rpm_out\`. Позы проверены прямой кинематикой и картинками.

Короткие ответы на вопросы:

- **Сборка**: MSVC 19.44 (VS 2022), CMake 3.29 и Ninja 1.12 из Strawberry. **В исходники правок не вносилось.** Единственное отличие от репозитория — версия подмодуля ggml: закреплённый коммит недоступен (подробности в §1). ctest: 12 из 12.
- **WSL** не установлен (`wsl -l -v`: «подсистема не установлена»). clang-cl в VS тоже нет: в `VC\Tools\Llvm\x64\bin` только clang-format и clang-tidy. Понадобился только MSVC.
- **Vulkan пропущен**: для сборки ggml-vulkan нужен Vulkan SDK (glslc и заголовки), а его нет. Сборка с `-DMOTIONBRICKS_ENABLE_VULKAN=OFF`.
- **Скорость на CPU** (i7-6700HQ, 4 ядра / 8 потоков, ggml выбрал вариант `haswell` с AVX2): модель загружается за 0,45–0,9 с. Один план (кусок 24–64 кадра) — медиана ~100 мс на 4–8 потоках, ~260 мс на одном. При перепланировании каждые 16 кадров 6-секундный клип строится за 1,2–1,4 с, то есть примерно в 4–5 раз быстрее реального времени. Первый прогон шёл в 2–3 раза медленнее (130–400 мс на план): ноутбук, видимо, был горячим или занятым. Вставка-переход на 64 кадра — 140–180 мс.

---

## 1. Как собрано

```bat
cd /d E:\SourceModding\HL2RPMSOURCE
git clone --recursive https://github.com/localai-org/motion-bricks.cpp
rem ^ Подмодуль ggml не скачался: "upload-pack: not our ref 0a249dd4d2f4dfcd962123a3d881f73f93588ccf".
rem   Этого коммита нет ни в ggml-org/ggml, ни в mudler/ggml, ни в richiejp/llama.cpp
rem   (repo richiejp/ggml и localai-org/ggml не существуют). Судя по дате (2026-09-16),
rem   это локальный коммит автора поверх ggml. Взята ближайшая публичная версия перед ним:
cd motion-bricks.cpp\ggml && git checkout v0.24.0 && cd ..
rem   (456172ec, 2026-09-14). Изначальный пин проекта был v0.20.2 = 8c63e70.

E:\Programs\Python\python.exe scripts\download_gguf_weights.py
rem   ~733 МБ с HF LocalAI-io/MotionBricks-G1-GGML@cc2a476..., SHA-256 проверены, ~12 мин (~1 МБ/с).
rem   Файлы лежат в generated\g1-f32\ (pose/root/vq-decoder/support.gguf) и generated\styles\ (15 .mbstyle).

hl2rpm_tools\build_windows.bat
```

`hl2rpm_tools\build_windows.bat` делает следующее:

```bat
call "E:\Programs\Microsoft Visual Studio\Community 2022\VC\Auxiliary\Build\vcvars64.bat"
E:\Strawberry\c\bin\cmake.exe -S . -B build\release -G Ninja -DCMAKE_MAKE_PROGRAM=E:/Strawberry/c/bin/ninja.exe ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
  -DMOTIONBRICKS_ENABLE_VULKAN=OFF -DMOTIONBRICKS_DOWNLOAD_MODELS=OFF -DMOTIONBRICKS_ENABLE_PHYSICS=OFF
E:\Strawberry\c\bin\cmake.exe --build build\release
```

Проверка: `cd build\release && E:\Strawberry\c\bin\ctest.exe` даёт **12 из 12** за 11 с, включая тесты с весами `motionbricks-inference-model` и `motionbricks-batch-model`. Тесты с весами регистрируются, только если веса уже лежат на месте во время cmake-configure.

- **Патчи исходников: нет.** Windows-поддержка уже есть в апстриме (коммит `ee0cf5d`, «Support building and running on Windows»). Предупреждения MSVC (C4244, C4996, C4005 `MB_OK` из winuser.h) безвредны. Предупреждения `import numpy, safetensors` при configure относятся к необязательным Python-проверкам и тоже безвредны.
- **Результат** в `build\release\bin\`: `motionbricks.dll` (C ABI v1), `ggml.dll`, `ggml-base.dll`, 9 вариантов `ggml-cpu-*.dll` (x64, sse42, sandybridge, haswell, skylakex, cannonlake, cascadelake, icelake, alderlake; лучший выбирается при загрузке), `motionbricks-cli.exe` и тесты. Для работы нужен MSVC redist: msvcp140, vcruntime140(_1), **vcomp140** (OpenMP). В системе он есть.
- `motionbricks-cli.exe abi` → `ABI 1`. `motionbricks-cli.exe inspect generated\g1-f32` выводит 183 148 382 параметра, 34 сустава и иерархию.
- Ничего не закоммичено. В рабочем дереве клона появились новые файлы `hl2rpm_tools\`, `hl2rpm_out\`, `HL2RPM_NOTES.md`. `build\` и `generated\` игнорируются `.gitignore`. Указатель подмодуля ggml сдвинут на v0.24.0, поэтому `git submodule status` показывает `+`.

## 2. Драйвер (Python + ctypes)

Файлы в `hl2rpm_tools\`. Нужен только numpy; для картинок — PIL. Оба уже стоят у Python 3.14.

| Файл | Что делает |
|---|---|
| `mbffi.py` | ctypes-обёртка над `motionbricks.dll`: Library / Model / Style / Command / Agent / Motion и `infer_between()` (stateless inference). Ошибки C ABI превращаются в `MBError` с текстом из библиотеки. |
| `mbstyle.py` | Минимальный читатель GGUF v3 (без пакета `gguf`) для `.mbstyle` и numpy-утилиты: кватернионы, FK, курс, контекст из кадров стиля. |
| `mbgen.py` | **Генератор по сценарию** (JSON → `.npz`, по желанию ещё `.json`). |
| `make_samples.py` | Весь набор примеров, `g1_skeleton.json`, `styles.json`, `timing.json`, оси вращения суставов. |
| `preview.py` | FK, PNG-превью (вид сбоку, спереди, высоты стоп, путь сверху), числовая сводка, сводные листы `--sheet`. |
| `build_windows.bat` | Сборка (см. §1). |

Запуск:

```bat
E:\Programs\Python\python.exe hl2rpm_tools\mbgen.py my_script.json -o hl2rpm_out\my_clip --json
E:\Programs\Python\python.exe hl2rpm_tools\mbgen.py --inline "{\"segments\":[{\"style\":\"walk_zombie\",\"move_deg\":0,\"seconds\":5}]}" -o hl2rpm_out\zombie5
E:\Programs\Python\python.exe hl2rpm_tools\preview.py hl2rpm_out\my_clip.npz
```

Сценарий:

```json
{
  "name": "walk_turn_stop",
  "initial_style": "walk",
  "seed": 1,
  "replan": 16,
  "argmax": false,
  "tail": false,
  "segments": [
    {"style": "walk",  "move_deg": 0,   "seconds": 3},
    {"style": "walk",  "move_deg": 90,  "facing_deg": 90, "seconds": 3},
    {"style": "idle",  "move": [0, 0],  "speed": 0, "seconds": 2},
    {"style": "walk",  "world_target": [2.0, 4.0, 180], "seconds": 4}
  ]
}
```

- `initial_style` / `initial_frame`: первые 4 кадра этого стиля задают начальный контекст. Драйвер **переносит их в начало координат** (x = z = 0, курс 0, взгляд по +Z) и передаёт через `mb_agent_set_context`. Так любой клип начинается в одной и той же точке и в одном направлении.
- `move` [x, z] или `move_deg` — направление движения в мире. `facing` / `facing_deg` — куда смотреть; по умолчанию туда же, куда идём, а при остановке — прежний взгляд. Отсчёт курса: 0° = +Z (вперёд от старта), **+90° = +X = влево от персонажа** (против часовой стрелки, если смотреть сверху).
- `speed` — м/с; `null` или отрицательное значение = скорость стиля, 0 = стоять на месте. `world_target` [x, z, курс°] — дойти до точки вместо движения по направлению. `seed` у сегмента фиксирует зерно; по умолчанию зерно растёт на 1 с каждым планом, как в демо.
- **Склейка**: каждый план — это 24–64 кадра. Из него берутся первые `min(replan, кадры−4, остаток)` кадров, затем `mb_agent_advance(k)`, затем следующий план. Новый план начинается с кадра `k` прежнего: библиотека сама смешивает первые 4 кадра с контекстом (веса генерации 0.3 / 0.43 / 0.57 / 0.7). Скачков на швах нет: максимальный шаг сустава за кадр на швах 10,8 см, внутри плана 14,0 см (walk).

Содержимое выходного `.npz` (`.json` содержит то же в виде списков):

| ключ | форма | смысл |
|---|---|---|
| `root_translations` | [F,3] f32 | мировая позиция сустава 0 (pelvis_skel), метры |
| `local_rotations_xyzw` | [F,34,4] f32 | локальные (относительно родителя) кватернионы XYZW; у корня — мировой |
| `fps` | 30 | |
| `joint_names`, `joint_parents`, `neutral_joint_positions` [34,3] | | скелет из загруженной модели |
| `segment_index` [F], `plan_frame_starts`, `timings_ms` | | какой сегмент сценария, где начинался каждый план, время каждого плана |
| `script` | str | исходный JSON сценария |

Чтение в Python: `d = np.load("hl2rpm_out/walk_walk.npz"); d["local_rotations_xyzw"]`. FK — `mbstyle.forward_kinematics(roots, rots, parents, neutral)`.

### Как работает команда в C API (по `src/agent.cpp`)

- Цель ставится на 1 с вперёд пружиной (полупериод 0,8 с): `текущая позиция + 2·speed·направление`. Курс догоняет `facing` пружиной с полупериодом 0,17 с. Из стиля по зерну берутся 4 подряд идущих кадра-образца — это целевая поза в конце плана.
- Нулевое направление заменяется на `0.1·facing`. Если перемещение не больше 0,1 м, оно обнуляется. У стиля `idle` скорость 0, поэтому без явного `speed` он никуда не идёт: в `walk_idle` пройдено 0,03 м.
- `mb_agent_set_context(roots, rots, frames ≥ 4, 34)` — можно начать с любой своей позы G1, например с конца другого клипа.
- Перепланирование раз в 16 кадров повторяет демо и апстрим. Без перепланирования персонаж замедляется к концу каждого плана.

## 3. Скелет (`hl2rpm_out\g1_skeleton.json`)

Источник — сама модель (`mb_model_get_joint_name/parent`, `mb_model_get_neutral_joint_position`, тензоры `support.gguf`). Отдельный MuJoCo XML не нужен: в C++ используются только эти смещения (`encode_context` / FK в `src/motion_rep.cpp`).

- **Оси**: правая система, **+Y вверх, +Z вперёд (курс 0), +X — левая сторона персонажа**. Проверено: левое бедро в нейтрали на x = +0.064, и так же адаптер MuJoCo в `sonic_physics.cpp`: mujoco = (z, x, y). Единицы — **метры**, 30 FPS.
- **Кватернионы XYZW**, **локальные относительно родителя**; у корня — мировой поворот.
- **Все вращения равны единице** — это нейтральная поза, и все кадры суставов совпадают с мировыми осями. Человек стоит прямо, лицом в +Z, ноги прямые, плечи опущены вниз, **локти согнуты на 90°, предплечья смотрят вперёд**, носки вперёд. Это нулевая поза робота G1, см. `hl2rpm_out\preview_neutral_pose.png`. Таз в нейтрали на 0,787 м над подошвой (`toe_base`). Высота до плеч ~1,07 м. **Головы и шеи нет**: на торсе нет суставов.
- **FK**: `pos[j] = pos[parent] + R_world[parent] · rest_offset[j]`, `R_world[j] = R_world[parent] · R_local[j]`, `rest_offset[j] = neutral[j] − neutral[parent]`, `pos[0] = root_translation`.
- **4 виртуальных конечных сустава** (7, 14 — `*_toe_base`; 25, 33 — `*_hand_roll_skel`) — это концы цепочек, а не тела робота. Их локальные вращения **не несут смысла** (листья скелета; сеть обучали на них с «глобальной единицей»), брать стоит только их позиции.
- Каждый физический сустав G1 — **шарнир с одной степенью свободы**. В `g1_skeleton.json` у каждого сустава есть поле `observed_rotation`: главная ось вращения в данных примеров (6 578 кадров), доля энергии вдоль этой оси (1.0 = чистый шарнир) и диапазон углов. Pitch ≈ X, yaw ≈ Y, roll ≈ Z в rest-кадре, у большинства суставов доля 0,93–1,00. У hip_roll ось наклонена: (∓0.55, 0.1, 0.83), доля 0,79.

| # | сустав (* = виртуальный) | родитель | rest-смещение от родителя, м (x, y, z) | наблюдаемая ось; доля; диапазон |
|---|---|---|---|---|
| 0 | pelvis_skel | -1 | 0.0000, 0.0000, 0.0000 | — |
| 1 | left_hip_pitch_skel | 0 | 0.0645, -0.1027, -0.0000 | (1.00, -0.00, -0.00) 1.00, -92.3..33.5° |
| 2 | left_hip_roll_skel | 1 | 0.0520, -0.0305, -0.0000 | (-0.55, 0.11, 0.83) 0.79, -6.2..43.6° |
| 3 | left_hip_yaw_skel | 2 | 0.0000, -0.1241, 0.0250 | (-0.01, 1.00, 0.01) 0.99, -28.4..33.0° |
| 4 | left_knee_skel | 3 | 0.0021, -0.1773, -0.0783 | (1.00, 0.00, -0.00) 1.00, 7.9..148.9° |
| 5 | left_ankle_pitch_skel | 4 | -0.0001, -0.3000, 0.0000 | (1.00, -0.01, -0.01) 1.00, -57.2..32.8° |
| 6 | left_ankle_roll_skel | 5 | 0.0000, -0.0176, 0.0000 | (0.01, 0.02, 1.00) 0.95, -16.2..14.9° |
| 7 | left_toe_base * | 6 | 0.0000, -0.0350, 0.1400 | (не использовать) |
| 8 | right_hip_pitch_skel | 0 | -0.0645, -0.1027, -0.0000 | (1.00, -0.00, 0.00) 1.00, -93.9..24.2° |
| 9 | right_hip_roll_skel | 8 | -0.0520, -0.0305, 0.0000 | (0.54, 0.09, 0.83) 0.80, -46.5..8.1° |
| 10 | right_hip_yaw_skel | 9 | 0.0000, -0.1241, 0.0250 | (-0.00, 1.00, 0.03) 0.99, -39.0..8.8° |
| 11 | right_knee_skel | 10 | -0.0021, -0.1773, -0.0783 | (1.00, 0.00, -0.00) 1.00, 8.1..147.8° |
| 12 | right_ankle_pitch_skel | 11 | 0.0001, -0.3000, 0.0000 | (1.00, -0.00, 0.00) 1.00, -56.0..32.4° |
| 13 | right_ankle_roll_skel | 12 | 0.0000, -0.0176, 0.0000 | (-0.01, 0.01, 1.00) 0.96, -14.7..16.2° |
| 14 | right_toe_base * | 13 | 0.0000, -0.0350, 0.1400 | (не использовать) |
| 15 | waist_yaw_skel | 0 | 0.0000, 0.0000, 0.0000 | (0.00, 1.00, -0.01) 0.98, -27.7..21.7° |
| 16 | waist_roll_skel | 15 | -0.0000, 0.0440, -0.0040 | (0.00, -0.04, 1.00) 0.97, -30.8..22.2° |
| 17 | waist_pitch_skel | 16 | 0.0000, 0.0000, 0.0000 | (1.00, -0.00, -0.00) 1.00, -7.7..30.7° |
| 18 | left_shoulder_pitch_skel | 17 | 0.1002, 0.2478, 0.0040 | (0.97, 0.14, -0.18) 0.93, -117.3..50.2° |
| 19 | left_shoulder_roll_skel | 18 | 0.0380, -0.0138, 0.0000 | (0.01, -0.01, 1.00) 0.99, -20.1..89.1° |
| 20 | left_shoulder_yaw_skel | 19 | 0.0062, -0.1032, 0.0000 | (0.01, 1.00, -0.01) 1.00, -78.6..40.5° |
| 21 | left_elbow_skel | 20 | 0.0000, -0.0805, 0.0158 | (1.00, -0.00, -0.00) 1.00, -53.9..87.3° |
| 22 | left_wrist_roll_skel | 21 | 0.0019, -0.0100, 0.1000 | (0.03, -0.01, 1.00) 0.99, -103.9..60.0° |
| 23 | left_wrist_pitch_skel | 22 | 0.0000, 0.0000, 0.0380 | (1.00, 0.00, -0.00) 1.00, -25.9..35.0° |
| 24 | left_wrist_yaw_skel | 23 | 0.0000, 0.0000, 0.0460 | (-0.01, 0.99, -0.12) 0.87, -38.1..71.1° |
| 25 | left_hand_roll_skel * | 24 | 0.0000, 0.0000, 0.1000 | (не использовать) |
| 26 | right_shoulder_pitch_skel | 17 | -0.1002, 0.2478, 0.0040 | (0.97, -0.14, 0.18) 0.91, -111.5..51.9° |
| 27 | right_shoulder_roll_skel | 26 | -0.0380, -0.0138, 0.0000 | (0.01, -0.00, 1.00) 0.99, -101.9..16.9° |
| 28 | right_shoulder_yaw_skel | 27 | -0.0062, -0.1032, 0.0000 | (-0.00, 1.00, -0.01) 1.00, -26.9..78.9° |
| 29 | right_elbow_skel | 28 | 0.0000, -0.0805, 0.0158 | (1.00, -0.00, 0.00) 1.00, -53.1..98.8° |
| 30 | right_wrist_roll_skel | 29 | -0.0019, -0.0100, 0.1000 | (-0.01, 0.00, 1.00) 0.99, -62.1..87.3° |
| 31 | right_wrist_pitch_skel | 30 | 0.0000, 0.0000, 0.0380 | (1.00, 0.00, 0.01) 1.00, -25.8..50.7° |
| 32 | right_wrist_yaw_skel | 31 | 0.0000, 0.0000, 0.0460 | (-0.01, 1.00, -0.01) 0.93, -69.8..63.1° |
| 33 | right_hand_roll_skel * | 32 | 0.0000, 0.0000, 0.1000 | (не использовать) |

### Заметки для переноса на ValveBiped (выведено из осей, в Source не проверялось)

- Перевод осей в мир Source (Z вверх, X вперёд, Y влево): `src = (z, x, y)`. Это циклическая перестановка с det = +1, поэтому у кватерниона `(qx', qy', qz', w') = (qz, qx, qy, w)`. Масштаб корня лучше считать не через 39.37, а через отношение высоты таза ValveBiped к 0,787 м: G1 ростом ~1,3 м.
- Соответствие суставов: `pelvis_skel`→Pelvis; hip pitch·roll·yaw (1–3 / 8–10)→Thigh (свёртка трёх шарниров; направление — от 2 к 4); knee→Calf; ankle pitch·roll→Foot; toe_base (позиция)→Toe0; waist yaw/roll/pitch→Spine…Spine4 (распределить); shoulder pitch·roll·yaw→UpperArm; elbow→Forearm; wrist roll·pitch·yaw→Hand. Head, Neck, Clavicle и пальцы нужно сделать процедурно.
- Нейтраль G1 (руки вниз, предплечья вперёд) не похожа на rest-позу ValveBiped. Переносить нужно **мировые повороты / направления костей относительно своих rest-поз**, а не копировать локальные кватернионы.
- Стопы касаются пола: у большинства стоячих стилей нижняя точка стопы в среднем от −0,4 до +0,7 см. Под пол уходят носки у ползков (до −3,5 см) и у глубокого приседа `idle_walk_stealth` (до −4,6 см). Есть небольшое проскальзывание опорной стопы: медиана 2–10 см/с у шагов (подробно в `summary.json`). Для игры стоит добавить фиксацию стоп через IK.

## 4. Стили (`hl2rpm_out\styles.json`, листы `preview_sheet_*.png`)

Стиль — это короткий образец поз (4–90 кадров) плюс скорость. Контроллер берёт из образца 4 кадра как целевую позу через ~1 с. **Походку сеть генерирует сама.** Например, у `walk` и `slow_walk` один и тот же образец (стойка, разница поворотов 3.6e-7), они отличаются только скоростью: 1.0 и 0.3 м/с. Скорости и маски длительности взяты из `scripts/convert_styles.py`.

| стиль | скорость, м/с | кадров образца | длины планов | как выглядит (по картинкам) |
|---|---|---|---|---|
| idle | 0.0 | 30 | 24–44 | спокойная стойка; без явной скорости не идёт |
| slow_walk | 0.3 | 30 | 24–44 | образец как у walk; медленная обычная ходьба |
| walk | 1.0 | 30 | 24–44 | обычная ходьба (~0,94 м/с в примере) |
| stealth_walk | 1.0 | 20 | 24–44 | полуприсед, руки вперёд у груди, крадущийся шаг |
| walk_stealth | 0.7 | 20 | 24–44 | глубокий присед (таз 0,46–0,52 м), «гусиный» крадущийся шаг |
| injured_walk | 0.5 | 8 | 24–44 | согнувшись, хромая, рука отставлена |
| walk_scared | 0.6 | 90 | 24–44 | ссутулившись, руки прижаты, осторожно |
| walk_zombie | 0.6 | 90 | 24–44 | руки вытянуты вперёд, шаркающий шаг |
| walk_gun | 0.6 | 76 | 24–44 | широкая стойка, обе руки вытянуты вперёд (прицеливание) |
| walk_boxing | 1.0 | 10 | 24–64 | боксёрская стойка, кулаки у лица, широкие ноги |
| walk_happy_dance | 1.0 | 20 | 24–44 | пританцовывая, руки вверх |
| walk_left / walk_right | 0.2 | 5 | 24–36 | зеркальная пара: одна рука отведена в сторону, корпус наклонён/развёрнут. При движении по +Z идут медленно вперёд и сносятся вбок (walk_left — вправо от персонажа на 0,38 м за 6 с, walk_right — влево на 0,54 м). Что именно значат имена, в репозитории не сказано |
| hand_crawling | 0.5 | 30 | 24–44 | **на четвереньках** (таз 0,34 м) |
| elbow_crawling | 0.8 | 5 | 24–64 | **ползком на локтях, лёжа на животе** (таз 0,18–0,20 м) |

- **Не-локомоционные клипы**: сидения, лежания в покое, подъёма предметов, жестов и разных вариантов idle **в наборе нет**. С `speed 0` каждый стиль даёт «стойку в своём стиле» с лёгким покачиванием (листы `preview_sheet_idle.png`, клипы `idle_<style>`). Это фактически 15 вариантов idle. `idle_elbow_crawling` — лёжа на животе, но не неподвижно: за 6 с смещение 0,28 м, поворот на 50°. `idle_hand_crawling` — на четвереньках.
- У лежачих поз ось +Z таза смотрит в пол, поэтому «курс» корня в них не определён. В `walk_elbow_crawling` он качается на ±70°, хотя путь почти прямой. Для лёжа курс корня брать нельзя.
- **Поворот на месте** (`turn_in_place_*`, взгляд 0 → 90 → 0 → −90 → 0 → 180°): корпус поворачивается, а стопы почти не отрываются (носки ≤ 1,2 см) — это **разворот на опорных стопах**, а не переступание. В игре это будет выглядеть как скольжение стоп. Ровно противоположная цель (180°) неоднозначна: направление поворота выбирается округлением (`std::remainder`).
- **Шаг → остановка** (`walk_then_stop_*`): 3 с ходьбы, потом `speed 0` (стиль walk или idle). Персонаж делает 1–2 шага и встаёт в стойку. Так же останавливает демо: стиль idle и скорость 0.

### «Умные примитивы» и ключевые кадры в API порта

- **В порте нет готовых примитивов** вроде «сесть / лечь / взять». Контроллер (`mb_agent_*`) умеет только ставить позы стиля в цель для локомоции: направление, курс, скорость, точка в мире. Авторы прямо пишут (DEMO.md): «does not feed arbitrary Kimodo poses into the model as target keyframes… earlier out-of-distribution full-pose target experiments were unstable». Общий API для разреженных ключевых кадров отложен (IMPLEMENTATION.md, «Decisions to revisit»).
- **«Stateless inference with caller-supplied poses»** (`inference.h`, `mb_model_infer`) — это **вставка перехода между двумя четырёхкадровыми границами**. Поведение такое:
  - Вход: 4 исходных кадра (контекст, «откуда») и 4 целевых («куда»). Формат — `roots[4,3]` + локальные XYZW `[4,34,4]` на каждую границу (`mb_inference_request_set_boundary_poses`; FK и признаки библиотека считает сама), либо сырые признаки (`set_features`: 8 слотов × [x, высота, z, cos, sin] / [скорость курса, vx, vz, высота] / 303 значения позы).
  - Обе границы должны быть **в одном каноническом кадре**: первый исходный корень в x = z = 0, курс 0. Обратно в мир их ставит вызывающий.
  - Выход: клип длиной **24–64 кадра с шагом 4**. Длину выбирает модель или маска `MB_INFERENCE_DURATIONS` (11 вариантов). Цель встаёт на **последние 4 кадра** клипа. Ключевых кадров в середине нет; длинная последовательность собирается цепочкой вызовов.
  - Маски по 8 слотам для каждой группы признаков позволяют частичные ограничения: например, «дойти в точку и любой позой» или «только поза».
  - Ограничения **не жёсткие**: позы не прибиваются, шов не смешивается, в мир клип не ставится.
  - Проверено: **стоя → лёжа ползком** и обратно, 64 кадра (`inbetween_*.npz`, `preview_inbetween_*.png`). Переход выглядит естественно (опускается через колени на живот). Ошибка суставов на целевых 4 кадрах — в среднем 4–5 см (максимум 13 см), на исходных — 0,7–4,5 см.
  - Так можно делать «встать → сесть» и подобное, **если есть целевая поза в форме G1** (например, перенести позу сидения из ValveBiped обратно на G1). Это вне обучающего распределения, и качество не гарантировано (авторы предупреждают).

## 5. Примеры (`hl2rpm_out\`, 25 МБ)

- `idle_<style>.npz/.json` (6 с, speed 0) и `walk_<style>.npz/.json` (6 с, движение +Z со скоростью стиля) — для **всех 15 стилей**.
- `turn_in_place_walk|idle` (11,5 с), `walk_then_stop_walk|idle` (6 с), `inbetween_stand_to_crawl|crawl_to_stand` (64 кадра, только .npz; в них также лежат запрошенные `requested_*`).
- `exemplars\style_<name>.npz` — сами образцы стилей (канонизированы); `exemplars\neutral_pose.npz` — поза из единичных вращений.
- `g1_skeleton.json`, `styles.json`, `summary.json` (числа по каждому клипу), `timing.json`.
- Генерация идёт с фиксированными зёрнами и детерминирована на одном бэкенде: повторный прогон дал те же числа. Воспроизведение: `python hl2rpm_tools\make_samples.py --json`, затем `preview.py` (команды в docstring).
- Скорость (в `timing.json`, второй прогон): план 75–135 мс, медиана 103 мс; 6-секундный клип в 3,7–6,5 раза быстрее реального времени. Потоки (6 с walk, 12 планов): 8 потоков 1,22–1,40 с, 4 потока 1,23–1,31 с, 2 потока 1,85–1,94 с, 1 поток 2,8–3,05 с. Больше четырёх потоков не помогает.

## 6. Проверка данных

`preview.py` считает FK в numpy и рисует `preview_<clip>.png`: 10 кадров сбоку и спереди, график высот носков, лодыжек и таза, путь сверху со стрелками взгляда. Сводные листы: `preview_sheet_style_exemplars.png`, `preview_sheet_idle.png`, `preview_sheet_walk.png`. Всё просмотрено глазами:

- **walk**: чередование ног, фазы опоры и переноса, руки качаются. Носки касаются пола (0 ± 0,7 см) и поднимаются на 8–10 см в переносе. Таз 0,72–0,78 м. За 6 с пройдено 5,5 м по прямой.
- **idle**: стоит, стопы на полу (−0,9…0 см), смещение 2,8 см за 6 с.
- Стили на картинках узнаются: зомби с вытянутыми руками, прицеливание, бокс, присед, четвереньки, ползок. Левая и правая стороны не перепутаны (синий = левый = +X).
- Нормы кватернионов отклоняются от 1 не больше чем на 1.2e-7. Шаг корня за кадр ≤ 5 см. На швах планов нет скачков.

## 7. Лицензия (NVIDIA Open Model License — веса и стили; код порта — Apache-2.0)

Текст лицензии — `scripts\hf\MotionBricks-G1-GGML\UPSTREAM_LICENSE` («Last Modified: October 24, 2025»), NOTICE — там же. Ключевые пункты дословно:

- 1.1: *"'Derivative Model' means any modification of, or works based on or derived from, the Model, **excluding outputs**."*
- 1.3: *"'Model' means the machine learning model, software, and any checkpoints, weights, algorithms, parameters, configuration files, and documentation that NVIDIA makes available under this Agreement."*
- 2.2: *"…grants You a perpetual, worldwide, non-exclusive, no-charge, royalty-free, revocable license to publicly perform, publicly display, reproduce, use, create derivative works of, make, have made, sell, offer for sale, distribute and import the Model."*
- 2.3: *"Your use of the Model must be in accordance with NVIDIA's Trustworthy AI terms, which can be found at https://www.nvidia.com/en-us/agreements/trustworthy-ai/terms/."* (Сами эти условия я не загружал — их стоит прочитать.)
- 2.4: *"NVIDIA owns the original Model and NVIDIA's Derivative Models. You own Your Derivative Models. **NVIDIA makes no claim of ownership to outputs. You are responsible for outputs and their subsequent uses.**"*
- 3 (Redistribution, применяется к *"copies of the Model or Derivative Models"*): *"a. You must include a copy of this Agreement. b. You must include the following attribution notice… 'Licensed by NVIDIA Corporation under the NVIDIA Open Model License.'"*
- 2.1: лицензия прекращается при патентном или авторском иске против модели или при обходе встроенных защитных мер. *"NVIDIA may update this Agreement at any time to comply with applicable law"*.
- 5: товарные знаки NVIDIA — только *"for reasonable and customary use in describing the origin of the Model"*.
- 8 (Indemnity) распространяется в том числе на *"outputs"*. 11 — экспортный и санкционный контроль.
- NOTICE и карточка HF: *"Licensed by NVIDIA Corporation under the NVIDIA Open Model License."*; *"This GGUF conversion grants no additional rights."*

Что из этого следует для мода (это не юридическая консультация):

1. Сгенерированные и перенесённые на ValveBiped анимации (`.smd` / `.mdl`) — это **outputs**. По 1.1 они не Derivative Model, по 2.4 NVIDIA на них не претендует. Условия раздела 3 (копия лицензии, атрибуция) по тексту относятся к распространению **Model / Derivative Models**, а не outputs. Бесплатный мод в Steam при этом прямо не ограничен: п. 2.2 разрешает даже продажу.
2. Если в мод попадут **сами веса или `.mbstyle`** (например, для генерации прямо в игре) — это распространение Model. Тогда обязательно приложить копию соглашения и строку атрибуции. При распространении `motionbricks.dll` / ggml — ещё Apache-2.0 (`LICENSE`) и MIT ggml.
3. Остаются обязательства «за выходные данные»: 2.4 (ответственность за outputs), 8 (возмещение NVIDIA претензий из-за outputs), 2.3 (условия Trustworthy AI), 11. Лицензия **отзывная** (2.2).
4. Добровольная строка в титрах (например, «Часть анимаций создана с помощью NVIDIA MotionBricks (NVIDIA Open Model License), порт motion-bricks.cpp») не требуется, но и не вредит. Нельзя подавать её как одобрение NVIDIA (п. 5).
