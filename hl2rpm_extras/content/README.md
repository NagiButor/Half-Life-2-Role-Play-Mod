# CONTENT — исходники диалогов, квестов и строк HL2RPM

Здесь (в git) лежат **двуязычные исходники** RPG-контента. Игра их не читает: их собирает
`hl2rpm_extras/tools/build_content.py` — заменяет тексты токенами локализации, пишет файлы в мод и сценарий
озвучки. Форматы и канонические имена — **`hl2rpm_extras/docs/RPG_SPEC.md`** (§3 условия, §4 эффекты,
§5 диалоги, §6 квесты, §10 озвучка, §11 имена, §12 токены); этот файл — практическое руководство автора.

| Папка / файл | Что писать | Что получается в моде (`E:\Steam\steamapps\sourcemods\hl2rpm`) |
|---|---|---|
| `dialogs/<npc>.txt` | один диалог NPC, корень `"Dialog"` | `scripts/dialogs/<npc>.txt` + токены `D_*` |
| `quests/<id>.txt` | квест, корень `"Quest"` (или `"Quests"` со списком квестов) | `scripts/quests/<id>.txt` + токены `Q_*` |
| `strings/*.txt` | прочие строки: записки, барки, Надзор, Брин-касты, подписи RPG | токены как есть |
| `characters.txt` | карточки голосов (кто как звучит, какая модель HL2) | сценарий озвучки |

Токены `RPG_*` уходят в `resource/hl2rpm_rpg_<lang>.txt`, все остальные — в `resource/hl2rpm_dialog_<lang>.txt`
(UTF-16LE с BOM, `english` и `russian`). Сценарий озвучки — `Дизайн-документ/Озвучка/` (README там же).
Файлы в моде **не править руками** — их перезапишет следующая сборка (в каждом стоит пометка `GENERATED`).

## Сборка и проверка

```
python hl2rpm_extras/tools/build_content.py            проверить и собрать всё
python hl2rpm_extras/tools/build_content.py --check    только проверить (ничего не пишет)
python hl2rpm_extras/tools/build_content.py --only npc_tamara   диагностика и KV-файл только этого источника
python hl2rpm_extras/tools/build_content.py -v         плюс заметки об автоисправлениях и флаги без «поставщика»
```

- Сообщения: `ERROR файл:строка  где: что`, `WARN …`, `NOTE …` (с `-v`). Код выхода 1 — есть ошибки.
- Файлы пишутся и при ошибках (чтобы чужая ошибка не останавливала остальных), **кроме** файла с
  синтаксической ошибкой: он не собирается, в моде остаётся его прошлая сборка. Перед коммитом — 0 ошибок.
- `--only` всё равно читает весь CONTENT (ссылки на квесты проверяются глобально) и пересобирает
  локализацию целиком.
- Исходники — **UTF-8** (с BOM или без). Имена файлов — латиницей.
- В игре после сборки: `dialog_reload` (нужен `sv_cheats 1`) перечитывает диалоги; новые тексты
  локализации подхватываются после перезапуска игры. Открыть диалог без карты-NPC: `dialog_force <targetname>`.

## Диалог: полный маленький пример

Рабочая копия с комментариями — `dialogs/_example_npc.txt` (файлы с `_` в начале — примеры).

```
"Dialog"
{
	"entity"   "npc_example"                       // targetname NPC на карте (по нему игра ищет диалог)
	"voice"    "citizen_m"                         // голос из characters.txt; у NPC из RPG_SPEC §11 можно не писать
	"speaker"  "Дворник Пётр"   "speaker_en" "Pyotr the Janitor"
	"auto_face_npc" "1"
	"greetings"                                    // сверху вниз: первое подходящее даёт стартовый узел
	{
		"1" { "require_flag" "example_met"  "node" "20" }
		"2" { "node" "1" }                         // безусловное - последним
	}
	"nodes"
	{
		"1"
		{
			"line"            "Чего уставился? Двор подметаю. Новенький, что ли?"
			"line_en"         "What are you staring at? I'm sweeping the yard. New here, are you?"
			"line_low_ana"    "Метла. Двор. Ты новенький?"     "line_low_ana_en" "Broom. Yard. You new?"
			"emotion"         "ворчливо, не поднимая головы"   // для озвучки, игроку не видно
			"effects"         { "set_flag" "example_met" }     // при показе узла
			"variants"
			{
				"1" { "require_time" "night"  "line" "Ночью тут не гуляют."  "line_en" "Nobody walks here at night." }
			}
			"options"
			{
				"1" { "text" "Да, только приехал." "text_en" "Yes, I just arrived." "next" "2" }
				"2" { "text" "Метла у вас довоенная?" "text_en" "Is that a pre-war broom?"
				      "check" "attr:vigilance:5"  "next" "3"  "fail_next" "4"
				      "on_success_xp" "13"  "on_fail_suspicion" "+2" }
				"3" { "text" "Я... тоже мести. Можно?" "text_en" "Me... sweep too. Can I?"
				      "require_attr" "ANA<=3"  "hide_if_unmet" "1"  "humanity" "+1"  "next" "5" }
				"0" { "text" "Не буду мешать." "text_en" "I won't bother you." "exit" "1" "close" "1" }
			}
		}
		"2"  { "line" "Под сканером не стой."  "line_en" "Don't stand under the scanner."
		       "options" { "1" { "text" "Спасибо." "text_en" "Thanks." "close" "1" "quest_event" "talk:npc_example" } } }
		"3"  { "vo" "EXAMPLE_S1_N3_PYOTR"  "line" "Глазастый."  "line_en" "Sharp eye."  "auto_next" "2" }
		"4"  { "line" "Метла как метла. Иди."  "line_en" "A broom's a broom. Move along."  "end" "1" }
		"5"  { "line" "Держи."  "line_en" "Here."  "effects" { "disposition" "+5" }  "close" "1" }
		"20" { "line" "Опять ты."  "line_en" "You again."
		       "options" { "0" { "text" "Иду." "text_en" "Going." "exit" "1" "close" "1" } } }
	}
}
```

Что из этого делает сборка (`scripts/dialogs/_example_npc.txt` в моде): `"line" "#D__EXAMPLE_NPC_N1"`,
`"line_low_ana" "#D__EXAMPLE_NPC_N1_LOW"`, вариант — `#D__EXAMPLE_NPC_N1_V1`, ответы — `#D__EXAMPLE_NPC_N1_O2`,
имя — `#D__EXAMPLE_NPC_SPEAKER`, узел 3 — `#EXAMPLE_S1_N3_PYOTR` (явный `vo` = имя токена = ID озвучки);
ключи `*_en` исчезают, тексты уходят в `hl2rpm_dialog_english/russian.txt`, реплики NPC — в сценарий озвучки
голоса `citizen_m`.

### Ключи диалога (кратко; полностью — RPG_SPEC §5)

| Где | Ключи |
|---|---|
| корень | `entity`, `voice`, `speaker`(+`_en`), `auto_face_npc`, `greetings`, `nodes`; старые: `start_node`, `returning_line`(+`_en`), `returning_choreo`, `returning_auto_next`, `returning_no_options`, `returning_auto_close`, `quests` |
| приветствие | `node` + любые `require_*` / `require`. **Реплики в приветствии нет** — приветствие только выбирает узел |
| узел | `line`, `line_low_ana`, `speaker`, `actor`, `choreo`, `sound`, `vo`, `emotion`, `effects {}`, `variants {}`, `options {}`, `auto_next`, `end`/`close`, `spoils`, `option_delay`, `in_quest`, `voice` |
| вариант | `require_*` + `line`, `line_low_ana`, `speaker`, `vo`, `emotion`, `choreo`, `sound`, `actor`, `auto_next`, `end`/`close`, `effects {}` |
| опция | `text`, `next`, `fail_next`, `check`, `check_label`, `check_hidden`, `check_once`, `check_xp`, `in_quest`, `require_*`, `hide_if_unmet`, эффекты §4 прямо в опции, `on_success_<эффект>`, `on_fail_<эффект>`, `close`, `exit`, `spoils`, `tag`; старые: `flags`, `server_sequence`, `after_line`, `after_next`… |

- **Номера** узлов и опций — числа (игра читает `atoi`). Опции нумеруйте 1, 2, 3…, выход — `0`.
- Каждый текст — пара: `"line"` + `"line_en"`, `"text"` + `"text_en"`, `"speaker"` + `"speaker_en"`,
  `"line_low_ana"` + `"line_low_ana_en"`, `"check_label"` + `"check_label_en"`. Нет одной половины — ERROR.
  Значение, уже начинающееся с `#ТОКЕН`, не переводится (ссылка на готовую строку).
- **Голос**: `voice` корня; если не задан — по `entity` (карточки `characters.txt`, затем RPG_SPEC §11,
  `npc_cp_*` → `cp`). Генерикам `npc_cit_*` пишите `"voice" "citizen_m"`/`"citizen_f"`. Узел с `actor`
  (другой говорящий в сцене) берёт голос актёра; можно задать `voice` узла явно. `"voice" "none"` — только текст.
- **Проверки** `check`: `skill:speech:50`, `attr:MIG:7` (или `attr:might:7`), `lie:35`, `bribe:50`, `barter:35`,
  `intimidate:MIG:6/speech:40`. Порог — число из скобок Part V: `[Красноречие 50]` → `skill:speech:50`.
- **Условия** `require_*` (RPG_SPEC §3): одно условие на ключ, ключи можно повторять — выполняться должны все.
  `require_attr "ANA<=3"`, `require_skill "speech>=50"`, `require_faction "lambda>=25"` / `"lambda:recognized"`,
  `require_civil ">=20"` / `"reliable"` / `"temppass"`, `require_item "item_temp_pass"`, `require_perk "PK-25"`,
  `require_flag "mq02_path=lambda"`, `require_noflag "mq00_registered"`, `require_humanity ">=61"`,
  `require_time "night"`, `require_act "1-2"`, `require_disposition ">=30"`, `require_quest_state "mq01_temp_pass:active"`.
- **Эффекты** (RPG_SPEC §4), списки через запятую: `xp "25"`, `rep "lambda:+10, reapers:-5"`, `civil "+2"`,
  `suspicion "+15"`, `humanity "-3"`, `credits "-50"`, `give_item "item_ration:2, item_water_can"`,
  `take_item`, `set_flag "mq02_path=lambda"`, `clear_flag`, `add_flag "mq02_days_left:-1"`, `disposition "+5"`,
  `disposition_npc "npc_ostap:-5"`, `grant_quest`, `complete_quest`, `fail_quest`, `set_stage "mq00_arrival:2"`,
  `quest_event "talk:npc_tamara"`, `discover "C17-02"`, `start_combat "1"`, `open_barter "1"`, `fire`, `notify`, `sound`.
  В узле — внутри `"effects" { }`, в опции — прямо ключами (у проверки — `on_success_*` / `on_fail_*`).

### Имена токенов и ID озвучки

`#D_<ФАЙЛ>_N<узел>` (реплика), `_LOW` (простак), `_V<вариант>`, `_N<узел>_O<опция>` (ответ), `_CHECK`
(подпись проверки), `_SPEAKER`, `_RETURNING`, `_O<n>_AFTER`; квесты — `#Q_<ID>_TITLE`, `_DESC`,
`_S<стадия>_TITLE`, `_S<стадия>_DESC`. `<ФАЙЛ>` — имя файла заглавными. **ID озвучки = имя токена**;
явный `"vo" "MQ02_S1_N3_TAMARA"` в узле (`<КВЕСТ>_S<сцена>_N<узел>_<ГОВОРЯЩИЙ>`, Part VI/02) становится
и токеном, и именем файла `sound/vo/hl2rpm/<язык>/<voice>/MQ02_S1_N3_TAMARA.wav`; `_LOW`/`_V1` добавляются к нему.
Ставьте `vo` в узлах сюжетных сцен — так ID не меняется при перенумерации узлов.

## Квест: полный маленький пример

`quests/sq01_can.txt` (имя файла = `id`):

```
"Quest"
{
	"id"        "sq01_can"
	"category"  "side"                         // main / faction / side / companion
	"title"     "Подберите банку"   "title_en" "Pick Up That Can"
	"desc"      "Юнит ГО у колонны уронил банку и ждёт, что я её подниму."
	"desc_en"   "A CP unit by the column dropped a can and expects me to pick it up."
	"xp"        "100"
	"act"       "1"
	"auto_track" "1"
	"complete_on_last_stage" "1"
	"rewards"     { "item_ration" "1" }          // только предметы: classname - количество
	"on_complete" { "set_flag" "sq01_done" }     // эффекты §4
	"stages"
	{
		"0"
		{
			"title" "Банка"   "title_en" "The Can"
			"desc"  "Решить, что делать с банкой."   "desc_en" "Decide what to do with the can."
			"marker"     "npc_cp_union12"            // targetname для компаса
			"marker_map" "rp_c17_02_plaza"
			"objectives"
			{
				"1" { "type" "flag" "target" "sq01_result" }
			}
		}
	}
}
```

Выдать и проверить из диалога: опция `"grant_quest" "sq01_can"`, условие `"require_quest_state" "sq01_can:active"`,
стадия `"set_stage" "sq01_can:1"`. Цели (`objectives`): `talk` (NPC, конец разговора), `flag`, `reach`
(`trigger_rpg` с `quest_event`), `kill` (`target` через `;`, `count`), `item` (`count`), `event`
(`logic_rpg QuestEvent`), `optional "1"`. Стадии нумеруются с 0 без пропусков. Ссылка на квест, которого нет
ни в `quests/`, ни в блоке `quests` диалога, — ERROR. Id квестов среза — RPG_SPEC §11 (другие — WARN).

## Строки (`strings/*.txt`)

```
"Strings"
{
	"RPG_Skill_speech"      { "ru" "Красноречие"  "en" "Speech" }
	"NOTE_pocket_TEXT"      { "ru" "Помни: ты не отсюда."  "en" "Remember: you are not from here." }
	"OW_G01"                { "ru" "Вниманию жильцов: раздача рационов в секторе семь."
	                          "en" "Attention residents: ration distribution in sector seven."
	                          "voice" "overwatch"  "emotion" "ровно"  "sound" "" }
}
```

Имя блока = имя токена (как написано). `voice` — строка попадёт в сценарий озвучки (ID = имя токена);
`sound` — уже есть готовый (ванильный) звук, записывать не нужно; `emotion`, `context`, `quest` — для актёра.
Префиксы — RPG_SPEC §12: `RPG_*` (подписи RPG), `NOTE_<id>_TITLE/_TEXT`, `BARK_<id>`, `OW_<id>` (Надзор),
`BC_<id>` (Брин-касты). Длинный текст записки — с `\n` для переноса.

## Голоса (`characters.txt`)

Один блок = один голос = папка `sound/vo/hl2rpm/<язык>/<voice>/`. Ключи: `name`/`name_en`, `npc`
(targetname через запятую, можно `npc_cp_*`), `gender`, `age`, `origin`, `accent`, `timbre`, `tempo`, `manner`,
`native_words`, `processing`, `vo` (new / mixed / vanilla / text / none), `priority`, `model`, `prompt_en`,
`tts_voice`, `notes`, `references` (3 эталонные реплики ru/en). Новый NPC с голосом: добавьте карточку
(и targetname в `npc`), а имя NPC — в RPG_SPEC §11.

## Из нотации Part V в KeyValues

| Part V | KV |
|---|---|
| `### Узел 3 — …` + `**ГО:** «…»` | узел `"3"` с `line`/`line_en` |
| `*(вариант для АНА ≤ 3)* **ГО:** «…»` | `line_low_ana` / `line_low_ana_en` |
| `` - `[Флаг: x]` **ИМЯ:** «…» `` внутри узла | `variants { "1" { "require_flag" "x" "line" … } }` |
| `**Приветствие**`: `` - `[cond]` **ИМЯ:** «…» → узел N `` | `greetings { "1" { <условие> "node" "N" } }`, реплика — в узел N. Другая фраза, но те же ответы — `variants` с тем же условием в узле N; фраза «и всё» (`■`) — свой узел с `"close" "1"` |
| `1. «…» → 2` | `"1" { "text" "…" "text_en" "…" "next" "2" }` |
| `→ ■` / `0.` | `"close" "1"` / опция `"0"` с `"exit" "1" "close" "1"` |
| `↩` (назад к хабу) | `"next" "<номер узла-хаба>"` |
| `[Красноречие 50] … → ✔ 4 · XP 13 / ✖ 5 · Подозрение +10` | `"check" "skill:speech:50" "next" "4" "fail_next" "5" "on_success_xp" "13" "on_fail_suspicion" "+10"` |
| `[Мощь 7]`, `[Бдительность 6]` | `"check" "attr:MIG:7"`, `"check" "attr:BDI:6"` |
| `[Ложь 35]`, `[Подкуп 50 кр.]`, `[Торговля 35]` | `"check" "lie:35"`, `"bribe:50"`, `"barter:35"` |
| `` `(АНА ≤ 3)` `` | `"require_attr" "ANA<=3" "hide_if_unmet" "1"` |
| `[Лямбда: Признание]`, `[ГС: Благонадёжный]` | `"require_faction" "lambda:recognized"`, `"require_civil" "reliable"` |
| `[Сострадание]` / `[Жестокость]` | `"require_humanity" ">=61"` / `"<=39"` (+ `"tag"`) |
| `[Перк: …]`, `[Предмет: X]`, `[Ночь]`, `[Отношение ≥ 30]` | `require_perk`, `require_item`, `require_time "night"`, `require_disposition ">=30"` |
| `XP 25 · ГС +2 · Лямбда +10 · кр. −50 · +Паёк ×2` | `"xp" "25" "civil" "+2" "rep" "lambda:+10" "credits" "-50" "give_item" "item_ration:2"` |
| `` `флаг: x = v` `` / `▶ SQ-05` / `✔ SQ-05` / `стадия 2` | `"set_flag" "x=v"` / `"grant_quest" "sq05_cat"` / `"complete_quest"` / `"set_stage" "sq05_cat:2"` |
| `⚔ бой` / `$ торговля` | `"start_combat" "1"` / `"open_barter" "1"` |
| `*(ремарка)*`, `[Т]`, `[ВО: …]` | в `emotion` (ремарку — не в текст!), `[ВО: путь]` → `"sound" "<путь>"` |

## Правила текста

- Кавычки в тексте — «ёлочки» (двойные `"` — WARN). Перенос строки — `\n`. Обратную косую черту не писать.
- **Ремарки** («*(пауза)*», «(тихо)») — не в `line`: игрок их увидит, TTS прочитает. Их место — `emotion`.
  Узел, где NPC молчит или действует, — `"line" "*(молча кивает)*"`: такие строки-ремарки целиком в озвучку не идут.
- Ответ героя (`text`) — до 250 байт UTF-8 (≈125 букв кириллицы), иначе WARN. Герой не озвучивается.
- Символы `| ; ~` в текстах можно: транспорт их кодирует (RPG_SPEC §5).
- Пишите «ё» — по ней TTS ставит ударение.

## Автоисправления (видно с `-v`, NOTE)

Сборка сама приводит к форме спецификации: `check "speech:50"` → `"skill:speech:50"`, `"MIG:7"` → `"attr:MIG:7"`;
`;` в списках эффектов → `,`; `require_humanity "61"` → `">=61"`; `"true"/"false"` → `1/0`; эффекты прямо в узле
переносятся в `effects {}`; блок `effects {}` в опции разворачивается в ключи опции; `auto_close` → `close`.

## Что проверяется

ERROR: синтаксис (незакрытые кавычки и скобки), нет `nodes`, нечисловые узлы/опции, повтор номера,
`next`/`fail_next`/`auto_next`/`node` на несуществующий узел, нет русского или английского текста, токен
уже занят другим текстом, неизвестный ключ, похожий на опечатку (`requre_flag` → «может, require_flag?»),
любой неизвестный `require_*`/`on_success_*`, неверный формат `check`/условия/эффекта, квест не определён,
реплика в приветствии.
WARN: неизвестный ключ (игра проигнорирует), недостижимые узлы, опция без `next` и без `close`/`exit`, узел без
выхода, двойные кавычки и ремарки в тексте, ответ > 250 байт, NPC/предмет/фракция/квест/локация не из
RPG_SPEC §11 (и не из `MOD/scripts/rpg/*.txt`), голос без карточки, пропуски в нумерации, проблемы с WAV.
