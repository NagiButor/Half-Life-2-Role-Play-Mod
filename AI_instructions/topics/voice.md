# Озвучка NPC: генерация голосов, встраивание в CK

> Общая справка для любой AI-модели; процедура — `../skills/hl2rpm-voice/SKILL.md`. Здесь устройство, решения и ловушки (2026-11-03/04). Позднее дополнение английских фраз описано в `../CURRENT_STATUS.md`; доступность моделей/API перепроверять перед новым заданием.

## Где что
- Рабочая папка (не в git): `E:\SourceModding\HL2RPMSOURCE\voice_gen` - задания (`job*/job.json` + `refs/`), результаты
  (`cktest_fish/raw`, `cktest/raw`, `test3`, `fish_test`), страницы прослушивания (`compare_qwen_fish.html`).
  Копия скриптов в git: `hl2rpm_extras/tools/voice/`. Архив проб: `E:\SourceModding\HL2RPMSOURCE\backups\voice_tests_2026-11-04.zip`.
- **Fish Audio (основной путь)**: `fish_job.py <job dir> <out dir> [--model s2.1-pro-free]` - API `https://api.fish.audio/v1/tts`,
  тело MessagePack (`references` = образец голоса + его текст, клон без обучения), venv `voice_gen\fish_venv` (msgpack, numpy,
  soundfile). Модель `s2.1-pro-free` кредитов не тратит; ~3-15 с на фразу. Ключ `FISH_AUDIO_API_KEY` (реестр HKCU\Environment) -
  только в заголовке `Authorization`, нигде не печатать. **Не перечислять переменные окружения по шаблону** - у пользователя
  была переменная, чьё имя было самим ключом (ключ утёк в чат, перевыпущен).
- **Qwen3-TTS на Colab (запасной путь)**: `voice_gen\colab\drive.py <job dir> <results>` (Colab CLI в `voice_gen\colab\venv`,
  токен `~/.config/colab-cli/token.json`), `setup.py`, `step.py` (порции по 6, LoRA ударений `sknyazev/qwen3-tts-12hz-1.7b-ru-stress-gguf`
  + RUAccent, проверка Whisper turbo). Аккаунт бесплатный: T4 иногда отбирают, дневной лимит кончается («Service Unavailable»).

## Как озвучка попадает в игру
1. `cktest_build.py` (по проекту CK) - все места озвучки: реплики, повторные, after-реплики, **общие темы и приветствие тем
   (голос того NPC, кто говорит)**, фразы NPC, наборы фраз; одинаковые (голос, язык, текст) генерируются один раз.
   `fish_cktest_build.py` - то же для Fish: эмоции метками в начале фразы по событию/тексту.
2. Генерация (Fish / Colab) -> `<results>/<id>.wav`.
3. `cktest_apply.py [--job DIR --raw DIR]` - громкость (RMS -19 дБ), ГО и солдаты: спектр подогнан к оригинальным записям HL2
   (FIR по LTAS) + стоковые щелчки рации (`npc/metropolice/vo/on1..off4`, `npc/combine_soldier/vo/on1..off3`), химзащита -
   фильтр респиратора; файлы - в канонические места `testmaps/sound/dialogs/<npc или barks_<набор>>/<язык>/<ключ>.wav`;
   `dlgedit/voiceattach.py` привязывает их к проекту; экспорт CK делает сцены с липсинком.
4. `ck_demo_content.py` часть 9 снова привязывает файлы из канонических мест (повторный запуск генератора не теряет голоса;
   готовые сцены подхватываются без перегенерации).

## CK: что добавлено
- `model.Topic.voices` (NPC -> {язык: Voice}) и `Project.topic_greeting_voices` - голос ответа темы / приветствия в голосе
  конкретного NPC; `Exporter.with_topics` отдаёт узлам 9000+ и приветствию **сами объекты проекта** (экспорт пишет в них пути и липсинк).
- `dlgedit/voiceattach.py` - `attach_canonical_voices(project, npc_names, bark_set_ids)`.
- **Набор фраз озвучен одним голосом на всех** - у каждого особого голоса своя копия набора: `ck_citizens` (male01),
  `ck_citizens_f` (женщина), `_t` (торговец), `_w` (рабочий), `_m` / `_wm` (в респираторе); NPC получают `bark_sets`.

## Состояние (2026-11-04)
ck_test озвучен **Fish Audio** целиком: 121 фраза на 195 мест (18 минут, 0 ошибок), проверено в игре (фразы своих наборов у торговца /
рабочего / химзащиты, диалоги ГО, солдат, торговца). Озвучка Qwen и страницы сравнения — в архиве `backups\voice_tests_2026-11-04.zip`.
Звуковых эффектов (UI, локации) у Fish Audio нет — только речь.

## Голоса ck_test
ГО - клон русской озвучки HL2 (`npc/metropolice/vo` из `hl2_sound_vo_russian_dir.vpk`) и английского оригинала; солдаты Альянса -
`npc/combine_soldier/vo` (prison_soldier_*); горожане - `vo/npc/male01`, `female01`; торговец и рабочий - голоса, сделанные по
описанию (Qwen VoiceDesign, образцы в `job_cktest/refs/trader_new.wav`, `worker_new.wav`). Образцы склеены из 2-3 длинных фраз
(12-18 с) с расшифровкой (Whisper). Русская озвучка HL2 есть: ГО, Надзор, солдаты, горожане.

## Русский: ударения, паузы, эмоции
- Fish: эмоции/тон/звуки - метки в квадратных скобках в начале фразы (`[angry]`, `[shouting]`, `[scared]`, `[whispering]`,
  `[sighing]`, `[break]`, `[long-break]`, `[emphasis]` перед словом; можно словами: `[slightly tired]`), до 3 на фразу; русский
  среди 13 языков. Фонемы (`<|phoneme_start|>`) - только en/zh/ja. Как Fish понимает U+0301 / заглавную ударную - проверка на
  слух: `voice_gen\fish_test\stress\` (замер по энергии слогов не различает - нужен слух).
- Qwen3-TTS: исходная модель ставит ударения плохо - дообучение `sknyazev/...-ru-stress-gguf` (LoRA r16, alpha 32) + U+0301
  после ударной гласной от RUAccent (без отметок у односложных и слов с ё).
- Как говорится, а не пишется: «ГО» -> «гэ-о», «10-04» -> «десять-четыре», «22:00» -> «двадцати двух ноль-ноль» (таблица SPEAK).
  «C.P.s» по-английски хуже, чем «CPs». Опечатки в тексте озвучиваются - исправлять в проекте.

## Ловушки
- Colab CLI: `exec` часто не получает ответ о завершении и ждёт свой таймаут - запускать с `--timeout 15` и ждать файл-итог
  (`colab ls`); скрипт читается в cp1251 без `PYTHONUTF8=1`; Git Bash портит `/content` (`MSYS_NO_PATHCONV=1`); три модели в
  одной сессии (VoiceDesign + Whisper + Base) - машину отбирают (12,7 ГБ ОЗУ).
- Qwen: полный клон английского ГО/солдата (образцы-рация) «убегает» до предела длины - только клон по тембру (x_vector).
- voicy (olluorg, готовая сборка CUDA 13) падает на T4 (`rms_norm`) и GTX 960M; Kaggle и ElevenLabs (гео-блок) отклонены;
  Alibaba Model Studio требует документ.
- Проверка в игре: `developer 1; snd_showstart 1`, `rpm_bark <npc> <событие>`, `dialog_force <npc>` - в console.log видны
  `dialogs\<npc>\<язык>\<ключ>.wav` и сцена.
