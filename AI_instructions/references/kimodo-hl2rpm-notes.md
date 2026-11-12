# kimodo.cpp для HL2RPM: сборка, интерфейс, скелет SOMA, примеры (2026-11-03)

kimodo.cpp (https://github.com/localai-org/kimodo.cpp, коммит `75ed52d`, 2026-10-29) — порт NVIDIA Kimodo (text-to-motion, диффузия) на C++23/GGML от авторов motion-bricks.cpp. Текст → эмбеддинг LLM2Vec (Llama-3-8B, 4096 чисел) → двухступенчатый трансформер-денойзер (корень, затем тело), DDIM → локальные повороты 30 суставов скелета SOMA + путь корня, 30 FPS. Собран **нативно под Windows, только CPU**. Работает. Веса: `soma-rp-v1.1` и `soma-seed-v1.1` (F32, по 1,08 ГБ), текстовый кодировщик Q4_K_M (4,8 ГБ).

Короткие ответы:

- **Сборка**: MSVC 19.44 (VS 2022) + CMake 3.29 + Ninja 1.12 (Strawberry), Release, Vulkan выключен. **В исходники upstream правок нет.** Подмодуль ggml скачался на своём пине (`8c63e70` = v0.20.2), в отличие от motion-bricks. Единственная тонкость сборки — `-DBUILD_SHARED_LIBS=ON` при первом configure (иначе `kimodo.lib` статическая и `kimodo.dll` нет, см. §1).
- **Скорость** (i7-6700HQ, 8 потоков, AVX2): денойзер F32 упирается в вычисления (~45 GFLOPS): **один шаг DDIM — ~6,3 с для 150 кадров, ~8,9 с для 240**; при 100 шагах (как в демо upstream) 5-секундный клип — ~10,5 мин, 8-секундный — ~14,5 мин. Текст: ~1,8–3 с на подсказку, если кодировщик держать в памяти (загрузка 38–53 с, ~5 ГБ ОЗУ), или 14–20 с на подсказку в потоковом режиме (~1,3 ГБ). Поэтому эмбеддинги кэшируются.
- **Клипы** (§5–6): из 10 подсказок с первой попытки годятся 5 (сидя по-турецки, боксёрская стойка, скучающий idle, разговор с жестами, присед с поиском в коробке) и ещё 1 как статичная поза (сидит расслабленно); частично — 4 (сесть-и-встать: садится, но не встаёт; лечь-и-встать: лежит, но не встаёт; у стены: наклон есть, руки не скрещены; джеб+удар: удары есть, но стороны зеркальные). Стопы на полу, скольжения и дрожания почти нет. **Другая формулировка исправила 2 из 4** («лёжа → садится, спускает ноги, встаёт»; «руки скрещены на груди»), другой seed — 0 из 4. Модель SEED на тех же подсказках ведёт себя иначе (встаёт с пола, но игнорирует «кровать»).
- **API управления** (§4): публичный C API — только «подсказка или эмбеддинг → клип» (seed, кадры, шаги, два CFG-веса). Ограничений (путь корня, ключевые позы, конечности, контакты) **нет ни в C, ни в C++ API**. В C++ API есть цепочка подсказок с переходами (`generate_text_sequence`). Внутри есть общий «условный» сэмплер (наблюдаемые признаки + маска); наш шим `kimodo_hl2rpm.dll` открывает его наружу: продолжение клипа новой подсказкой, старт с любой позы SOMA, последовательности из кэшированных эмбеддингов, ключевая поза в конце клипа — всё проверено и работает (§6).
- **Лицензии** (§7): SOMA RP/SEED — NVIDIA Open Model License (коммерческое использование разрешено; outputs — не Derivative Model, NVIDIA на них не претендует; обязательства по копии лицензии/NOTICE — только при распространении самой модели). Текстовый кодировщик — Meta Llama 3 Community License (+ запрет улучшать другие LLM её outputs; «Built with Meta Llama 3» — при распространении Llama Materials или продукта, который их использует). В мод уходят только анимации.
- Ничего не закоммичено. Новое в клоне: `hl2rpm_tools\`, `hl2rpm_out\` (14 МБ), `hl2rpm_cache\` (эмбеддинги), `hl2rpm_licenses\`, `HL2RPM_NOTES.md`; веса и `build\` игнорируются `.gitignore` upstream. Подмодуль ggml на своём пине.

---

## 1. Как собрано

```bat
cd /d E:\SourceModding\HL2RPMSOURCE
git clone --recursive https://github.com/localai-org/kimodo.cpp
rem   ggml: 8c63e70982c95ceb862e3a1073a2c1beef75d60a (v0.20.2) -- скачался сам, менять не пришлось.
cd kimodo.cpp
E:\Programs\Python\python.exe scripts\download_gguf_weights.py --model soma-rp-v1.1 --model soma-seed-v1.1 --text-quantization q4_k_m
rem   Скрипт сам проверяет SHA-256 по MANIFEST.json каждого репозитория HF. Я качал параллельно двумя
rem   процессами (модели движения с --motion-only + только текст через его же download_and_verify):
rem   ~6-7 МБ/с, ~7,4 ГБ за ~25 мин.
hl2rpm_tools\build_windows.bat
hl2rpm_tools\build_ext.bat
```

Файлы весов (в `.gitignore` upstream): `models\kimodo-soma-rp-v1.1-f32.gguf`, `models\kimodo-soma-seed-v1.1-f32.gguf` (по 1 133 166 784 байт), `Llama-3-Kimodo-Q4_K_M.gguf` (5 058 327 904), `tokenizer.gguf` (7 295 054). Манифесты, NOTICE и лицензии скопированы в `hl2rpm_licenses\`.

`hl2rpm_tools\build_windows.bat`:

```bat
call "E:\Programs\Microsoft Visual Studio\Community 2022\VC\Auxiliary\Build\vcvars64.bat"
E:\Strawberry\c\bin\cmake.exe -S . -B build\release -G Ninja -DCMAKE_MAKE_PROGRAM=E:/Strawberry/c/bin/ninja.exe ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
  -DKIMODO_ENABLE_VULKAN=OFF -DKIMODO_BUILD_TESTS=ON -DBUILD_SHARED_LIBS=ON
E:\Strawberry\c\bin\cmake.exe --build build\release
```

- **Правок исходников: нет.** C++23 (`std::expected`) MSVC 19.44 собирает с `/std:c++latest`. Всё POSIX-специфичное (`setenv`) в upstream уже под `#if defined(__unix__)`. Предупреждения C4251 (dll-интерфейс у `std::unique_ptr` в `kimodo::model`), C4996 (`getenv`), C4244 безвредны.
- **Ловушка сборки**: верхний `CMakeLists.txt` объявляет `add_library(kimodo ...)` без `SHARED`, а `BUILD_SHARED_LIBS ON` (для ggml) ставит в кэш только *после* этого. При первом configure получается статическая `kimodo.lib`, `kimodo.dll` нет; при повторном configure — уже DLL. Флаг `-DBUILD_SHARED_LIBS=ON` в первом configure это убирает. (Инструкция upstream для Windows, `docs/how_to_setup_with_unreal_in_windows.md`, говорит о `kimodo.dll` — вероятно, по той же причине у них configure был не первым.)
- ggml собран одним вариантом CPU-бэкенда `/arch:AVX2` (GGML_AVX2, FMA, F16C, BMI2), `GGML_NATIVE=OFF`, `GGML_BACKEND_DL=OFF`, OpenMP (vcomp140). В отличие от motion-bricks, вариантов `ggml-cpu-*.dll` нет — одна `ggml-cpu.dll`.
- **Результат** в `build\release\`: `kimodo.dll` (C ABI 1), `kimodo_hl2rpm.dll` (наш шим, §2), `kmd-generate.exe`, `kmd-encode.exe`, `kmd-inspect.exe`, `kmd-sample-embedding.exe`, `kmd-quantize-text.exe`, `kmd-pack-text.exe`, тесты; в `build\release\bin\`: `ggml.dll`, `ggml-base.dll`, `ggml-cpu.dll`. Нужен MSVC redist (msvcp140, vcruntime140(_1), vcomp140) — в системе есть.
- `kmd-inspect.exe models\kimodo-soma-rp-v1.1-f32.gguf` → `Kimodo motion GGUF: valid (414 F32 tensors)`. Метаданные: skeleton `soma30`, motion_dim 369, hidden 1024, 16 слоёв × (корень + тело), 8 голов, FFN 2048, 50 текстовых токенов, 1000 базовых шагов диффузии, 283 282 523 параметра, `kimodo.model_identity = nvidia/Kimodo-SOMA-RP-v1.1@6c9233af…`, `kimodo.license = nvidia-open-model-license`, `kimodo.commercial_use = 1`.

### Тесты

`cd build\release && ctest` (с `bin` в PATH): **4 из 14 проходят** (`kimodo-capi-test`, `kimodo-diffusion-test`, `kimodo-converter-test`, `kimodo-quantization-metrics-test`), **10 падают из-за отсутствующих данных, а не кода**: им нужны `models\kimodo-smplx-rp-v1-f32.gguf` (SMPL-X-чекпойнт под закрытой лицензией NVIDIA, upstream его не публикует), `fixtures\*` (снимаются из PyTorch-эталона в docker-контейнере upstream) и BF16-бандл текста `generated\llm2vec-text-bundle`. Оговорка: тесты используют `assert`, а в Release он отключён (`NDEBUG`), так что `kimodo-capi-test` в Release почти ничего не проверяет.

Вместо этого проверено своё (`hl2rpm_tools\test_parity.py`, всё — да):

- `kimodo-generate-smoke.exe <soma-rp|soma-seed> 30` — оба кода выхода 0 (2 кадра, 1 шаг, 30 суставов).
- `kimodo_generate_embedding` (upstream `kimodo.dll`) и `khl_generate` (шим) с одним эмбеддингом и seed дают **побитово одинаковые** повороты и корень; повтор в том же процессе тоже побитово тот же — генерация на CPU детерминирована.
- `kmd-encode` в потоковом и резидентном режимах дают побитово один и тот же эмбеддинг.
- Последовательность: upstream `kmd-generate.exe ... --sequence 3 2 7 OUT 12 p1.txt 12 p2.txt` (текст кодирует сам) и `khl_generate_sequence` с эмбеддингами из нашего кэша — **побитово одинаковые** 24 кадра (то есть и кэш эмбеддингов, и склейка совпадают с upstream).
- `khl_decode(raw)` восстанавливает повороты/корень результата с ошибкой ≤ 1,3e-10; Python-копия построения условия перехода (`kmrep.transition_condition`) совпадает с C++ (`prepare_sequence_transition`) до 4e-7 (маска — точно).

## 2. Драйвер (Python + ctypes), `hl2rpm_tools\`

Нужен только numpy (картинки — PIL), оба есть у Python 3.14. Стиль — как у motion-bricks.

| Файл | Что делает |
|---|---|
| `kmffi.py` | ctypes над двумя DLL. `CApi`/`CApiModel` — upstream `kimodo.dll` (C ABI 1: `kimodo_model_load`, `kimodo_generate`, `kimodo_generate_embedding`). `Shim`/`TextEncoder`/`MotionModel` — наш `kimodo_hl2rpm.dll` (кодирование текста, генерация, последовательности, продолжение, условный сэмплер, декодирование, сырое представление). Ошибки → `KimodoError` с текстом из библиотеки. |
| `ext\khl_capi.cpp`, `build_ext.bat` | Шим `kimodo_hl2rpm.dll` (C ABI `khl_*`, версия 1): собирается `cl` из **неизменённых** `src\*.cpp` upstream + один свой файл, линкуется с ggml из `build\release`. Ничего нового в алгоритмах: только открывает внутренние функции upstream (`llm_text_encoder::encode`, `sample_motion_from_noise`, `sample_motion_sequence_from_noise`, `prepare_sequence_transition`, `sample_motion_from_noise_conditioned`, `decode_motion`). |
| `kmrep.py` | numpy: скелет SOMA30, кватернионы, FK, разметка сырого представления, копия построения условия перехода (`condition_rows`, `transition_condition`), `pose_to_raw` (любая поза SOMA → строки «предыдущего клипа» для продолжения). |
| `kmgen.py` | **Генератор** (подсказка / список подсказок / продолжение / сценарий JSON → `.npz` + `.json`), с кэшем эмбеддингов. |
| `clips_hl2rpm.json`, `clips_variants.json`, `clips_seed_model.json`, `run_phase2.sh` | Сценарии: 10 запрошенных клипов; seed 2 и другие формулировки; те же подсказки на модели SEED; второй этап прогона (50 шагов, варианты, SEED, опыты управления). |
| `build_windows.bat` | Сборка upstream (§1). |
| `report.py` | Таблица (markdown) и лист сбоку для набора клипов. |
| `preview.py` | FK, PNG (сбоку, спереди, графики высот и контактов, путь сверху), числовая сводка `*.summary.json`, лист `--sheet`, поза с единичными поворотами `--rest`. |
| `make_skeleton.py` | `hl2rpm_out\soma30_skeleton.json` и `soma30_vs_valvebiped.png`. |
| `test_parity.py` | Проверки C API ↔ шим (§1). |
| `test_control.py` | Опыты управления (§4): продолжение, старт с позы, последовательность, конечная ключевая поза. |

Запуск:

```bat
cd /d E:\SourceModding\HL2RPMSOURCE\kimodo.cpp
set PY=E:\Programs\Python\python.exe
%PY% hl2rpm_tools\kmgen.py --prompt "a person sits down on a chair" --seconds 6 --seed 1 -o hl2rpm_out\sit
%PY% hl2rpm_tools\kmgen.py --prompt "a person waves" --seconds 4 --seeds 1 2 3 --steps 50 -o hl2rpm_out\wave
%PY% hl2rpm_tools\kmgen.py --segment "a person walks forward" 3 --segment "a person sits down on a chair" 3 -o hl2rpm_out\walk_sit
%PY% hl2rpm_tools\kmgen.py --continue-from hl2rpm_out\walk_sit_s1.npz --prompt "a person stands up" --seconds 3 -o hl2rpm_out\stand
%PY% hl2rpm_tools\kmgen.py --script hl2rpm_tools\clips_hl2rpm.json [--only lean_wall jab_punch] [--encode-only]
%PY% hl2rpm_tools\preview.py hl2rpm_out\sit_s1.npz [--sheet hl2rpm_out\sheet.png]
```

Параметры: `--seconds` (кадры = секунды × 30), `--seed`/`--seeds`, `--steps` (DDIM, 1..1000; по умолчанию 100, как в демо upstream), `--text-cfg`, `--constraint-cfg` (оба по умолчанию 2.0, как в upstream), `--transition` (кадров перекрытия для последовательностей/продолжения, по умолчанию 5, как в upstream), `--model rp|seed|<путь>`, `--text-quant q4_k_m|q8_0` (скачан только Q4_K_M; Q8_0 upstream рекомендует, 7,6 ГБ — для 16 ГБ ОЗУ резидентно тесно, потоково годится), `--api shim|capi`. Все пути CLI (одна подсказка, `--segment`, `--continue-from`, `--api capi`, `--seeds`) проверены короткими прогонами.

- **Кэш эмбеддингов**: `hl2rpm_cache\embeddings\<ключ>.npy` (4096 float32) + `index.json` (подсказка, модель текста, время кодирования). Ключ = sha256(имя файла текстовой модели + размер + подсказка). Повторная генерация с другим seed/шагами/моделью движения 8B-кодировщик не загружает. Если не хватает ≥ 3 эмбеддингов, кодировщик грузится целиком (`KIMODO_TEXT_LAYER_CHUNK=32`: ~5 ГБ, 38–53 с загрузки, 1,8–3 с на подсказку), иначе потоково (~1,3 ГБ, 14–20 с на подсказку). Кодировщик выгружается до загрузки модели движения: пик ОЗУ — ~5 ГБ на кодировании, ~1,3–1,5 ГБ на генерации.
- **Путь генерации**: по умолчанию через шим (он один возвращает сырое представление, а значит контакты стоп, и умеет последовательности). Для одной подсказки он побитово совпадает с upstream C API (`--api capi` — через `kimodo.dll`, без `raw`/контактов).
- **Время**: в `.json` клипа — `timing.motion_s` (только диффузия+декодирование), `timing.text` (по каждой подсказке: из кэша или нет, секунды кодирования, загрузки кодировщика).
- **Потоки**: `KIMODO_THREADS` (по умолчанию все логические, 8). 4 потока медленнее на 25–45 %. Поля `threads/device/backend_dir` в `kimodo_runtime_options` upstream `capi.cpp` проверяет только по `size` и игнорирует — управлять можно только переменными окружения (`KIMODO_THREADS`, `KIMODO_BACKEND=cpu`, `KIMODO_PROFILE=1`, `KIMODO_TEXT_LAYER_CHUNK`, `KIMODO_TEXT_RESIDENT_LIMIT_MIB`, `KIMODO_MOTION_*`).

Выходной `.npz` (`.json` рядом — метаданные):

| ключ | форма | смысл |
|---|---|---|
| `root_translations` | [F,3] f32 | мировая позиция сустава 0 (Hips), метры, пол y = 0 |
| `local_rotations_xyzw` | [F,30,4] f32 | локальные (к родителю) кватернионы XYZW; у Hips — мировой |
| `fps` | 30 | |
| `joint_names`, `joint_parents`, `rest_offsets` [30,3] | | скелет из загруженной модели (§3) |
| `hips_right_left`, `end_effectors` | | (26, 22) и (24, 28, 13, 19): LeftFoot, RightFoot, LeftHand, RightHand |
| `raw` | [F,369] f32 | сырое (ненормированное) представление (§3) — только через шим |
| `raw_positions` | [F,30,3] | позиции суставов, которые предсказала сама сеть (не FK) |
| `foot_contacts` | [F,4] | контакты ~0/1: LeftFoot (пятка), LeftToeBase, RightFoot, RightToeBase |
| `segment_index` [F], `meta` (JSON) | | номер подсказки для кадра; подсказка(и), seed, шаги, CFG, время |

## 3. Скелет SOMA30 (`hl2rpm_out\soma30_skeleton.json`, `soma30_rest_pose.png`, `soma30_vs_valvebiped.png`)

Источник — `src\skeleton.hpp` upstream (имена и родители из Apache-2.0 `kimodo/skeleton/definitions.py` NVIDIA, смещения — из их `joints.p`); шим отдаёт ту же таблицу из загруженной модели (`khl_joint_info`), в `.npz` пишется она. NVIDIA-шный Python расширяет 30 суставов до 77 (расслабленные пальцы) — здесь этого нет, сеть предсказывает именно 30.

- **Оси**: правая система, **+Y вверх, +Z вперёд (курс 0), +X — левая сторона персонажа** (как у G1 в motion-bricks). **Метры**, **30 FPS**, пол y = 0.
- **Кватернионы XYZW**, **локальные к родителю**; у Hips — мировой. Корневая трансляция = мировая позиция Hips. FK: `R_w[j] = R_w[parent]·R_l[j]`, `p[j] = p[parent] + R_w[parent]·offset[j]`.
- **Единичные повороты = T-поза** (проверено картинкой `soma30_rest_pose.png`): руки горизонтально вдоль ±X, ладони вниз (большой палец смотрит вперёд), ноги прямо вниз, стопы и носки вперёд (+Z), лицо +Z. Hips на 0,989 м выше нижнего сустава (LeftToeBase), сустав головы на 1,587 м, размах рук по суставам кистей 1,447 м. В стоячих клипах Hips на 0,98–1,01 м, нижний сустав носка — на 2–3 см над полом (суставы внутри стопы).
- **Курс** = `atan2(dz, −dx)` для вектора RightLeg − LeftLeg: 0 = лицом в +Z, плюс = поворот к +X (влево). Каждый клип без условия начинается в x = z ≈ 0 с курсом ≈ 0.
- **Листья** (Jaw, LeftEye, RightEye, Left/RightHandThumbEnd, Left/RightHandMiddleEnd, Left/RightToeBase): поворот у них в выходе есть, но ниже них в SOMA30 ничего нет, поэтому в FK он невидим. Значимы только их позиции. **Сгиба пальцев ног, пальцев рук, челюсти и глаз модель не даёт.**
- **Сырое представление** (D = 9 + 12·30 = 369, из `motion_decode.cpp`/`sequence.cpp` и проверено на клипах): `[0:5]` корень x, y(=высота Hips), z, cos курса, sin курса; `[5:95]` позиции суставов (x, z относительно корня, y абсолютная; слот 0 = Hips); `[95:275]` мировые повороты в 6D = первые два **столбца** матрицы; `[275:365]` скорости суставов, м/с (≈ прямая разность позиций × 30, корреляция 0,999); `[365:369]` **контакты стоп ~0/1: LeftFoot (пятка), LeftToeBase, RightFoot, RightToeBase**. Порядок контактов я установил по клипам: каналы 0–1 «включены», когда левая стопа неподвижна (скорость 2–3 см/с) и правая движется (0,15–0,27 м/с); в каждой паре канал пятки выключается раньше канала носка, а в сидячих клипах с приподнятой пяткой включён только канал носка. C API порта контакты (и всё сырое представление) не отдаёт; постобработки upstream Python (MotionCorrection, чистка скольжения стоп) в порте нет (`reference/README.md`, `docs/IMPLEMENTATION.md`).
- Позиции, которые предсказывает сама сеть (`raw_positions`), и FK из поворотов совпадают для тела в пределах ~0,5–0,9 см. Исключение — кончики пальцев: у `*HandMiddleEnd` постоянный сдвиг ~5–6 см, у `*HandThumbEnd` ~2–3 см (в каждом кадре одинаковый, то есть это расхождение длин/формы кисти между смещениями `skeleton.hpp` и данными обучения, а не шум). Повороты листьев в клипах почти постоянны (Jaw ~0,2°, глаза ~11°, ThumbEnd ~25–27°, ToeBase 2–7°). Для переноса берите повороты (FK).

### Соответствие ValveBiped (в `soma30_skeleton.json` → `valvebiped_mapping`)

Позиции ValveBiped взяты из `models/humans/group01/male_07.mdl` (bind-поза из `pose_to_bone`, читалка MDL из Creation Kit, только чтение; в JSON — `valvebiped_reference`, в осях SOMA, метры). **Bind-поза ValveBiped — A-поза, руки на ~50° вниз**; у SOMA единичная поза — T-поза. Длины близки: бедро+голень 0,854 м (SOMA) против 0,873 (ValveBiped), плечо 0,287 против 0,297, таз 0,98–1,0 м у обоих; торс ValveBiped от таза до Neck1 длиннее на ~0,12 м.

| ValveBiped.Bip01_ | SOMA | чей мировой поворот брать | примечание |
|---|---|---|---|
| Pelvis | Hips | Hips | трансляция корня = позиция Hips (масштаб, перестановка осей) |
| Spine | Spine1 | Spine1 | |
| Spine1 | Spine2 | Spine2 | |
| Spine2 | Chest | Chest | Chest у SOMA тянется до Neck1 (0,26 м) — это Spine2+Spine4 ValveBiped |
| Spine4 | Chest | Chest | промежуточного сустава нет: Spine4 = мировой поворот Chest (локально единица к Spine2) |
| Neck1 | Neck1 | Neck2 | у SOMA два сустава шеи; Neck1 ValveBiped тянется до Head1 |
| Head1 | Head | Head | Jaw/глаза — листья |
| L/R_Clavicle | Left/RightShoulder | то же | родитель у SOMA — Chest, у ValveBiped — Spine4 |
| L/R_UpperArm | Left/RightArm | то же | |
| L/R_Forearm | Left/RightForeArm | то же | |
| L/R_Hand | Left/RightHand | то же | пальцы ValveBiped — статично/процедурно (у SOMA только кончики большого и среднего) |
| L/R_Thigh | Left/RightLeg | то же | «Leg» у SOMA = бедро |
| L/R_Calf | Left/RightShin | то же | |
| L/R_Foot | Left/RightFoot | то же | |
| L/R_Toe0 | Left/RightToeBase | Left/RightFoot | ToeBase — лист, сгиба носка нет: Toe0 едет со стопой |

Перенос (выведено из осей и поз; в Source не проверялось): переносить **мировые повороты относительно своих rest-поз** (`R_w_soma[t]·R_w_soma_rest⁻¹` поверх мировых поворотов bind-позы ValveBiped) или нацеливать кости по направлениям сегментов SOMA; локальные кватернионы не копировать — разные rest-позы и оси костей (у Source ось X кости — вдоль кости). Оси SOMA → модельное пространство Source (X вперёд, Y влево, Z вверх): `src = (z, x, y)`; это циклическая перестановка (det +1), кватернион: `(qx', qy', qz', w') = (qz, qx, qy, w)`. Масштаб: метры × 39,37 → дюймы (таз SOMA ~1,0 м ≈ таз male_07 38,6 дюйма).

## 4. Что API даёт для управления (по коду)

**Публичный C API upstream** (`include/kimodo/kimodo_capi.h`, ABI 1, реализация `src/capi.cpp`):

- `kimodo_generate(model, prompt, options)` и `kimodo_generate_embedding(model, {float[4096]}, options)`. `kimodo_generation_options`: `seed` (mt19937_64 → нормальный шум [кадры × 369]), `frames` (1..10000), `diffusion_steps` (1..1000, косинусное расписание на 1000 базовых шагов, DDIM η = 0), `text_cfg_weight`, `constraint_cfg_weight`.
- **Ограничений нет**: ни пути корня, ни ключевых поз, ни целей для кистей/стоп, ни контактов, ни начальных кадров, ни продолжения. Результат — только локальные повороты [T,J,4] и позиции корня [T,3] (контакты и прочее сырое представление не отдаются).
- `constraint_cfg_weight` **в обычной генерации ни на что не влияет**: CFG разделённый, три ветви [текст, ограничение, без условия], а без ограничений ветви «ограничение» и «без условия» получают один и тот же вход (нулевая маска, нулевой текст, курс 0) — `out = u + w_t·(t − u) + w_c·(c − u)` с `c = u` (`run_separated_cfg_denoiser_conditioned`, `src/denoiser.cpp`).
- `kimodo_runtime_options.threads/device/backend_dir` игнорируются (проверяется только `size`), управление — переменными окружения (§2).
- README прямо: «General constraint input, 77-joint SOMA expansion, skinned-mesh GLB export, and motion-denoiser quantisation are not implemented yet.»

**C++ API upstream** (`include/kimodo/kimodo.hpp`) добавляет одно: `model::generate_text_sequence(segments, transition_frames, steps, seed, text_cfg, constraint_cfg)` — 1..16 подсказок, каждая 2..300 кадров, перекрытие 1..60 кадров (меньше длины каждого следующего куска). Это перенос `_multiprompt` upstream (`src/sequence.cpp`): кусок i+1 генерируется на (кадры + перекрытие) кадров **с условием** на последние `transition_frames` кадров куска i — **корень (5 значений) + позиции всех 30 суставов + мировые 6D-повороты четырёх конечностей (LeftFoot, RightFoot, LeftHand, RightHand)**, в системе координат, сдвинутой к первому кадру перекрытия, с начальным курсом; затем кусок сдвигается обратно в мир, перекрытие **линейно смешивается**, кадры склеиваются. Подсказки — только текстом (нет варианта с эмбеддингами). `kmd-generate.exe ... --sequence TRANSITION STEPS SEED OUT FRAMES PROMPT.txt ...` и режим `--server` (демо) — то же с CFG 2/2.

**Внутри** (`src/denoiser.hpp`, не экспортируется): `sample_motion_from_noise_conditioned(weights, noise, embedding, observed[T,369], observed_mask[T,369], first_heading, ...)` — общий механизм «наблюдаемых признаков с маской» (у трансформеров на входе [движение, маска], отдельная CFG-ветвь ограничения). Upstream использует его только для переходов последовательности; кодирования ограничений upstream Python (путь корня, полные ключевые позы, конечности) в порте нет.

**Наш шим `kimodo_hl2rpm.dll` открывает** (`ext\khl_capi.cpp`, Python — `kmffi.MotionModel`):

- `khl_text_encode` — эмбеддинг 4096 (для кэша); `khl_generate` — одна подсказка из эмбеддинга (= `kimodo_generate_embedding` побитово);
- `khl_generate_sequence` — последовательность из эмбеддингов (тот же порядок шума и склейка, что у `generate_text_sequence`);
- `khl_generate_continuation(previous_raw, embedding, frames, ...)` — **продолжение**: ровно шаг перехода `_multiprompt`, но между вызовами: `previous_raw` — `raw` прошлого клипа или **любая поза SOMA**, переведённая в строки представления `kmrep.pose_to_raw` (условие читает из строки только корень x/z, слот Hips и 6D-повороты; позиции, курс и повороты конечностей оно пересчитывает само через FK). Возвращает прошлое + новое, перекрытие смешано;
- `khl_generate_conditioned(embedding, observed_raw, mask, first_heading, ...)` — низкоуровневый условный сэмплер с произвольной маской (экспериментально, см. §6);
- `khl_decode`, `khl_prepare_transition` (для проверки своих условий против upstream), сырое представление в каждом результате.

## 5. Десять клипов (`hl2rpm_out\<имя>_s1.npz/.json/.png/.summary.json`)

Модель `soma-rp-v1.1`, seed 1, **100 шагов DDIM**, CFG 2/2, текст Q4_K_M, сценарий `hl2rpm_tools\clips_hl2rpm.json`. Картинки: `<имя>_s1.png` (8 кадров сбоку и спереди, графики), общий лист сбоку — `sheet_batch1_side.png`, числа — `batch1_summary.md` и `*.summary.json`. Я просмотрел все картинки и проверил спорные места числами.

**Время** (8 потоков): текст — загрузка кодировщика 52,6 с (резидентно) + **1,8–3,0 с на подсказку** (10 подсказок за 77,5 с вместе с загрузкой); движение — **485 с** (4 с, 120 кадров), **568–593 с** (5 с), **677–713 с** (6 с), **865–899 с** (8 с, 240 кадров). Все 10 клипов — 6865 с (1 ч 54 мин). Текст — меньше 1 % времени; кэш эмбеддингов экономит в основном загрузку кодировщика (~50 с) и 5 ГБ ОЗУ.

| клип (сек) | что получилось | годится? |
|---|---|---|
| walk_sit_stand (8) | 2 шага (0,45 м), с 2,8 по 4,2 с садится (Hips 0,99 → 0,62 м — высота стула ~45 см правдоподобна), **дальше сидит до конца, не встаёт**; в последних кадрах руки вперёд | подход + посадка — да; «встаёт» — нет |
| sit_relaxed (6) | **сидит с первого кадра** (посадки нет), Hips 0,56 м, руки на бёдрах, стопы на полу; почти неподвижен — «перенос веса» не виден (Hips сместился на 3 см за 6 с) | да, как статичный idle сидя (мало жизни) |
| lie_bed_getup (8) | лежит на спине **с первого кадра на высоте кровати** (Hips 0,46–0,52 м, стопы ~0,5 м над полом) неподвижно до ~5 с; потом перекатывается на правый бок, левая нога перекрещивает правую, руки вверх; **не садится и не встаёт** | первые ~4,6 с — как «лежит на кровати»; подъём — нет |
| sit_crosslegged (5) | стоит → шаг назад правой → за ~1,5 с садится на пол (Hips 0,13–0,14 м), ноги скрещены, руки на коленях | **да** |
| lean_wall (6) | поза «прислонился спиной»: корпус отклонён назад, стопы вынесены вперёд, Hips 0,91 м, статично; **руки не скрещены** — висят вдоль тела; нижний сустав носка 6–9 мм (ниже всех остальных клипов) | частично (наклон — да, руки — нет) |
| jab_punch (4) | боксёрская стойка, но **левша** (правая нога впереди); 1,1–1,4 с — удар правой (передней) рукой, 1,6–2,0 с — удар левой с разворотом корпуса на −75° и отрывом левой стопы на ~0,18 м, затем стойка до конца. То есть «джеб + удар», **но с зеркальными сторонами** и преувеличенным вторым ударом | частично |
| boxing_stance (5) | защита у лица, широкая стойка, колени согнуты, «пружинит» коленями (Hips 0,87–0,93 м, ~3 качания за 5 с), стопы не отрываются (не подпрыгивает) | **да** |
| idle_bored (6) | стоит, правой рукой трёт шею/затылок, поворачивает корпус на ±50°, стопы на месте (скольжение ≤ 1 см/с) | **да** |
| talk_gesture (6) | стоит, предплечья вперёд, жестикулирует обеими руками, ноги на месте; кисти вращаются быстро (до 12–20°/кадр) и чуть неровно от кадра к кадру | да (кистям может понадобиться сглаживание) |
| crouch_search_box (6) | за ~1 с приседает на носки (пятки подняты на ~0,1 м, Hips 0,32–0,34 м), руки работают у пола перед собой, на ~5 с встаёт | **да** (кончики пальцев FK уходят на 7 см «под пол», из них ~5–6 см — сдвиг кончиков, §3) |

Общие проверки по всем 10: суставы стоп не уходят под пол (нижний сустав носка ≥ 0,6 см, обычно 1,5–3 см); скольжение опорных носков в стоячих клипах 0,4–3 см/с; рывков нет (шаг Hips ≤ 3,8 см/кадр, нормы кватернионов отличаются от 1 не больше чем на 1,2e-7); пересечение ног — только в подъёме с кровати (и намеренно в позе по-турецки). Контакты стоп из сети согласуются с картинкой (сидя на носках включены только каналы носков). Что видно сразу: **клип не обязан начинаться стоя** — «сидит»/«лежит» дают позу с первого кадра; **длинные составные подсказки выполняются наполовину** (сел, но не встал; лёг, но не встал); детали вроде «скрещённых рук» и «левой/правой» модель может игнорировать или зеркалить.

## 6. Шаги, seed, формулировка, модель SEED, управление

### 50 или 100 шагов

`walk_sit_stand`, seed 1, 50 шагов (`hl2rpm_out\steps\walk_sit_stand_50st_s1.*`) — то же движение, что при 100: так же садится на 3–4 с и не встаёт, Hips 0,60 м против 0,62; расхождение поз относительно корня в среднем 1,9 см (в мире 20 см — путь короче: 0,18 м против 0,45). Время 497 с против 865 с. **Для черновых прогонов 50 шагов достаточно** (на одном примере; гарантий нет), чистовые — 100, как в демо upstream.

### Другой seed и другая формулировка (`hl2rpm_out\variants\`, 100 шагов)

| подсказка | seed 2 (та же формулировка) | другая формулировка, seed 1 |
|---|---|---|
| walk_sit_stand | идёт 0,5 м, садится на ~4,5 с (Hips 0,59), **не встаёт** | «…walks forward to a chair, sits down on it, then gets up from the chair and stands»: идёт 1,5 м, садится (0,56), **не встаёт**; к концу руки вперёд, как перед подъёмом. В 8 с подъём не помещается ни разу (3 из 3) — решение: последовательность (ниже) |
| lie_bed_getup | лежит 4,6 с, садится, поворачивается на 120° и **снова ложится** | «…is lying on their back on a bed, then sits up, swings their legs off the bed and stands up»: **работает** — лежит до ~3,4 с на высоте кровати (0,5 м), садится, разворачиваясь (~200°), ставит ноги на пол (~5,5 с) и встаёт к 7–8 с (Hips 1,02). Годится (кровать ~0,4 м подразумевается) |
| lean_wall | стоит почти прямо, корпус чуть назад, руки висят — хуже seed 1 | «…stands leaning back against a wall with their arms folded across their chest»: **руки скрещены на груди**, корпус отклонён назад, стопы вынесены вперёд, Hips 0,89, статично. **Годится** |
| jab_punch | то же, что seed 1, только раньше: стойка левши, удар правой (0,7 с), удар левой с разворотом −69° (1,1 с) | «a boxer in an orthodox stance throws a quick jab with the left hand, then a straight right punch»: стойка **всё равно левши** (правая нога впереди), **один** удар правой (1,2–1,4 с), джеба левой нет — хуже |

Вывод: **seed меняет в основном время и детали, а не смысл** (сесть-не-встать, не-скрестить-руки, удары зеркально — повторяется). **Формулировка меняет смысл**: подробное перечисление фаз («sits up, swings their legs off the bed and stands up») и конкретная поза («arms folded across their chest») сработали; «left/right», «orthodox» модель не слушает.

### Модель SEED (`soma-seed-v1.1`, `hl2rpm_out\seed_model\`, те же подсказки, seed 1, 100 шагов)

SEED обучена на открытом наборе BONES-SEED, RP — на закрытом Rigplay (`docs/QUANTIZATION.md`). Архитектура и размер одинаковые, скорость та же (888 / 867 / 700 / 523 с).

| подсказка | RP | SEED |
|---|---|---|
| walk_sit_stand | 2 шага, сел, не встал | **не идёт**: сразу садится (0,5–1 с, Hips 0,64), сидит до ~7,5 с, в конце наклоняется вперёд, но встать не успевает |
| lie_bed_getup | лежит на «кровати» (0,5 м), потом перекат, не встаёт | лежит **на полу** (Hips 0,065 — кровать проигнорирована) до ~4,5 с, потом **садится и встаёт**, к ~7,5 с стоит. Опора руками о пол (суставы кистей ≥ 4 см над полом) |
| lean_wall | наклон назад, руки висят | то же, но ещё и ноги скрещены в лодыжках; руки не скрещены |
| jab_punch | стойка левши, правой, затем левой с разворотом | из нейтральной стойки шагает в стойку и бьёт **левой (0,8 с), правой (1,2 с), левой (1,5–1,9 с)**, дальше «боксирует» на месте — порядок как в подсказке, но движение суетливее: скольжение носков ~5 см/с, контакты часто мигают, ускорения выше (рывков-выбросов при этом нет) |

Итого SEED не лучше и не хуже в целом — **другая**: на этих подсказках она чаще делает обратимые действия до конца (встать с пола), но игнорирует детали обстановки («on a bed», «walks up to»). Для набора анимаций стоит пробовать обе модели и несколько seed.

### Опыты управления через шим (`hl2rpm_tools\test_control.py`, RP, seed 1, **50 шагов**, `hl2rpm_out\control_*`, лист `sheet_control_side.png`)

| опыт | что сделано | результат |
|---|---|---|
| **продолжение** (`control_continue_s1`, 221 с) | `walk_sit_stand_s1` (240 кадров, кончается сидя) + `khl_generate_continuation` «a person waves with their right hand» на 90 кадров, перекрытие 5 | продолжает **с той же позы и места**: остаётся сидеть и машет правой рукой (8,5–9,7 с). Шва нет: наибольший шаг сустава у шва 7,7 см/кадр против 7,4 см (99-й перцентиль) в остальном клипе; первые 236 кадров прошлого клипа не изменены, кадры 236–239 смешаны |
| **старт с чужой позы** (`control_from_pose_s1`, 256 с) | поза сидя из `sit_relaxed_s1` (кадр 90), взятая **только из поворотов и корня** (`kmrep.pose_to_raw`), 6 одинаковых кадров + «a person stands up from the chair and walks forward» на 120 кадров | начинает ровно с этой позы (на 6-м кадре расхождение суставов 1,55 см), сидит до ~2,5 с, к ~3,6 с встаёт и идёт (Hips 0,57 → 1,01 м). Так можно начинать с любой позы SOMA — например, перенесённой обратно из ValveBiped |
| **последовательность** (`control_sequence_s1`, 638 с) | 3 × 3 с: «walks forward» / «sits down on a chair» / «stands up from the chair», перекрытие 5 | швы гладкие (у шва 18,6 и 1,9 см/кадр при обычных 18,8), **но смысл не тот**: быстрая ходьба (6 м за 3,3 с) по инерции, затем садится **на пол** (Hips 0,14, обхватив колени), третий кусок «встать со стула» игнорирует — сидит до конца |
| **конечная ключевая поза** (`control_keyframe_s1`, 286 с), экспериментально | `khl_generate_conditioned`: последние 5 из 150 кадров — поза сидя из `sit_relaxed_s1`, развёрнутая на 180° и поставленная в 2 м впереди; маска — как у перехода (корень + позиции всех суставов + повороты кистей/стоп); «a person walks forward and sits down on a chair» | **сработало**: идёт 2 м, разворачивается (курс 0 → −180° за ~1 с) и садится ровно в заданную позу — Hips в конце (−0,005; 0,573; 1,996) при цели (0; 0,56; 2,0), расхождение суставов на 5 последних кадрах 2,1 см. Разворот быстрый, ноги на развороте сближаются до 3,5 см |

Выводы по управлению:

- **Продолжение и старт с заданной позы работают** и дают бесшовные клипы — это внутренний механизм переходов upstream, только вызванный между вызовами. Нужен шим; публичное API так не умеет.
- **Ключевая поза в конце (и, вероятно, в любом месте клипа) тоже работает** через низкоуровневый сэмплер с маской того же вида, что у переходов. Это мой эксперимент: такой маской upstream пользуется только для переходов, кодирование ограничений upstream Python (путь корня, ключевые позы, конечности) в порте не перенесено, поэтому другие маски (только путь корня, только кисть) — не проверены и могут быть вне распределения.
- **Цепочка подсказок не заменяет хорошую формулировку**: переход переносит позу и скорость, а смысл куска зависит от контекста (после быстрой ходьбы «сесть на стул» стало «сесть на пол»).

Для набора анимаций HL2RPM практичный рецепт по итогам: (1) статичные/циклические позы (сидеть, стоять у стены, бокс, idle) — одной подсказкой; (2) «действие + возврат» (встать с кровати, сесть-встать) — подробной подсказкой с перечислением фаз, или двумя клипами: первая часть + `--continue-from` с подсказкой второй части, или ключевой позой; (3) несколько seed и обе модели, выбирать глазами; (4) 50 шагов для перебора, 100 для чистовика.

## 7. Лицензии (это не юридическая консультация)

Копии лежат в `hl2rpm_licenses\`: `NVIDIA_Open_Model_License_2025-10-24.txt` (текст со страницы nvidia.com, снят 2026-11-03), `Llama-3-Kimodo-GGML__LICENSE-META-LLAMA-3.txt`, NOTICE и манифесты HF. Код порта — Apache-2.0 (`LICENSE`, `NOTICE`), ggml — MIT.

**Модели движения SOMA RP/SEED v1.1 — NVIDIA Open Model License** («Last Modified: October 24, 2025»; в GGUF: `kimodo.license = nvidia-open-model-license`, `kimodo.commercial_use = 1`; README порта: коммерческое использование разрешено, в отличие от SMPL-X RP v1 — тот под внутренней исследовательской лицензией, и порт его не публикует). Дословно:

- Преамбула: *"Models are commercially usable. You are free to create and distribute Derivative Models. NVIDIA does not claim ownership to any outputs generated using the Models or Derivative Models."*
- Определения: *"'Derivative Model' means all (a) modifications to the Model, (b) works based on the Model, and (c) any other derivative works of the Model. An output is not a Derivative Model."*; *"'Model' means the machine learning model, software, checkpoints, learnt weights, algorithms, parameters, configuration files and documentation shared under this Agreement."*
- 2.2: *"…a perpetual, worldwide, non-exclusive, no-charge, royalty-free, revocable (as stated in Section 2.1) license to publicly perform, publicly display, reproduce, use, create derivative works of, make, have made, sell, offer for sale, distribute (through multiple tiers of distribution) and import the Model."*
- 2.3: *"Use of the Models under the Agreement must be consistent with NVIDIA's Trustworthy AI terms…"* (условия Trustworthy AI, Last Modified June 27, 2024: запрет использования в нарушение закона — незаконная слежка, биометрия без согласия, травля, намеренный обман).
- 2.4: *"NVIDIA claims no ownership rights in outputs. You are responsible for outputs and their subsequent uses."*
- 3.1 (распространение **модели**): *"If you distribute the Model, You must give any other recipients of the Model a copy of this Agreement and include the following attribution notice within a 'Notice' text file with such copies: 'Licensed by NVIDIA Corporation under the NVIDIA Open Model License'"*.
- 2.1: лицензия прекращается при патентном/авторском иске о модели и при обходе её «Guardrail»; *"NVIDIA may update this Agreement to comply with legal and regulatory requirements at any time and You agree to either comply with any updated license or cease Your copying, use, and distribution of the Model…"*
- Indemnity: *"You will indemnify and hold harmless NVIDIA from and against any claim by any third party arising out of or related to your use or distribution of the Model, Derivative Models or outputs."* Плюс экспортный/санкционный контроль (Trade and Compliance), право Делавэра.
- NOTICE на HF: *"Licensed by NVIDIA Corporation under the NVIDIA Open Model License. Upstream model: nvidia/Kimodo-SOMA-RP-v1.1. This GGUF conversion grants no additional rights."*

**Текстовый кодировщик Llama-3-Kimodo-GGML — Meta Llama 3 Community License** (+ MIT-адаптеры LLM2Vec McGill). Дословно о главном:

- 1.a: *"…a non-exclusive, worldwide, non-transferable and royalty-free limited license … to use, reproduce, distribute, copy, create derivative works of, and make modifications to the Llama Materials."*
- 1.b.i: *"If you distribute or make available the Llama Materials (or any derivative works thereof), or a product or service that uses any of them, including another AI model, you shall (A) provide a copy of this Agreement with any such Llama Materials; and (B) prominently display 'Built with Meta Llama 3' on a related website, user interface, blogpost, about page, or product documentation."*
- 1.b.iii: в копиях Llama Materials — NOTICE *"Meta Llama 3 is licensed under the Meta Llama 3 Community License, Copyright © Meta Platforms, Inc. All Rights Reserved."*
- 1.b.iv: соблюдение законов и Acceptable Use Policy (https://llama.meta.com/llama3/use-policy).
- 1.b.v: *"You will not use the Llama Materials or any output or results of the Llama Materials to improve any other large language model (excluding Meta Llama 3 or derivative works thereof)."*
- 2: отдельная лицензия нужна при > 700 млн MAU; 3–4: «как есть», без ответственности Meta; 5.c: иск против Meta о Llama — лицензия прекращается, плюс *"You will indemnify and hold harmless Meta … arising out of or related to your use or distribution of the Llama Materials."*; 7: право Калифорнии.

Что из этого следует для мода:

1. В мод попадают только **сгенерированные и перенесённые анимации** (`.smd`/`.mdl`). Для SOMA это **outputs**: по определению не Derivative Model, NVIDIA на них не претендует, условие 3.1 (копия лицензии, NOTICE) касается распространения самой модели. Продажа/бесплатное распространение мода этим не ограничены. Остаются: ответственность за outputs (2.4), возмещение NVIDIA претензий третьих лиц из-за outputs (Indemnity), Trustworthy AI (2.3), экспортный контроль; лицензия отзывная.
2. **Llama 3**: эмбеддинг — промежуточный «output or results» Llama Materials, анимация получена из него моделью NVIDIA. В мод не входят ни веса, ни эмбеддинги, и сам мод Llama не использует при работе. Формулировка 1.b.i («a product or service that uses any of them») по прямому смыслу про продукт, который *использует* Llama Materials; мод их не использует — я бы считал, что требование не срабатывает, но формулировка широкая. Строка «Built with Meta Llama 3» в титрах/на странице мода ничего не стоит и снимает вопрос. Запрет 1.b.v (улучшать другие LLM) к анимациям отношения не имеет.
3. Если когда-нибудь класть в мод **сами веса** (генерация в игре): SOMA — копия лицензии NVIDIA + NOTICE с атрибуцией; Llama — копия лицензии Meta, NOTICE и «Built with Meta Llama 3»; плюс Apache-2.0 порта и MIT ggml.
4. Не подавать использование как одобрение NVIDIA/Meta (товарные знаки — только для указания происхождения).
