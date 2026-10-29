# HL2RPM — спецификация RPG-систем и форматов данных (v1, вертикальный срез)

Источник правил — `Дизайн-документ/Part I` (числа, формулы) и `Part V/00` (нотация диалогов).
Этот файл фиксирует **как это сделано в коде и данных**. Меняя формат — правь здесь.

Пути: **SRC** = `sp/src`, **MOD** = `E:\Steam\steamapps\sourcemods\hl2rpm`,
**CONTENT** = `sp/src/hl2rpm_extras/content` (исходники контента в git; сборка — `hl2rpm_extras/tools/build_content.py`).

---

## 0. Принципы

1. Всё, что видит игрок, — через токены локализации (`#TOKEN`). Языки: `english`, `russian`
   (выбирается языком Steam у Source SDK Base 2013 SP; английский — запасной для отсутствующих токенов).
2. Сервер — источник истины (персонаж, флаги, квесты, проверки). Клиент только показывает.
3. Всё состояние персонажа **сохраняется** и **переживает `trigger_changelevel`** (как квесты:
   статическая память server.dll + свой `ISaveRestoreBlockHandler`, сброс только на `MapLoad_NewGame`).
4. Проверки детерминированы: значение ≥ порога — успех. Порог виден игроку (зелёный/красный).
5. Старые ключи диалогов/квестов продолжают работать (тестовые карты в `testmaps`).

---

## 1. Персонаж (server: `game/server/hl2rpm/rpg_character.{h,cpp}`)

| Поле | Тип | Старт | Примечание |
|---|---|---|---|
| `name` | string | «Гражданин» | вводится на создании |
| `attr[6]` | int 1..10 | 5 | `agility LOV`, `analytics ANA`, `might MIG`, `vigilance BDI`, `spirit DUH`, `authority AVT` |
| `skillInvested[13]` | int | 0 | очки, вложенные на уровнях |
| `tagged[3]` | skill id | — | +15 к навыку |
| `traits` | список TR-xx | — | 0..2 |
| `perks` | id → ранг | — | |
| `level`, `xp`, `skillPoints`, `perkPicks` | int | 1, 0, 0, 0 | |
| `credits` | int | 0 | на регистрации +40 (+300 с TR-11) |
| `civil` (ГС) | int −100..100 | 0 | TR-12 +15; до MQ-02 статус «Временный пропуск» (флаг `civil_temp_pass`) |
| `suspicion` | float 0..100 | 0 | |
| `humanity` | int 0..100 | 50 | |
| `resonance` | int 0..100 | 0 | скрыта до MQ-05 |
| `influence` | int 0..100 | 0 | скрыта всегда |
| `factions` | id → int −100..100 | `factions.txt` | |
| `flags` | name → int/string | — | `world_*`, `<квест>_*`; строковое значение допустимо (`mq02_path=lambda`) |
| `disposition` | npc → int | `rpg_disposition` NPC | |
| `notes` | список note id | — | прочитанные записки/терминалы |
| `locations` | список location id | — | открытые локации |
| `chargenDone` | bool | false | |

**Формулы** (одно место в коде — `rpg_formulas.cpp`):
- Навык: `2 + 2·P + ceil(S/2)` + 15 (тег) + вложено + бонусы черт/перков/брони; предел 100 (для проверок до 110).
  Пары P/S — `skills.txt`.
- XP уровня L (накопленный): `80·(L−1)·(L+3)`; за уровень: очков навыков `10 + floor(ANA/2)`, +3 к макс. HP; перк на чётных.
- Макс. HP: `50 + 10·DUH + 3·(L−1)` (применяется к игроку: `SetMaxHealth`, текущее не лечится).
- Предел веса: `25 + 3·MIG` кг (заменяет `sv_inventory_weight_max` в `GetPlayerMaxCarryWeight`).
- XP за проверку: навык — порог/2, характеристика — 5×значение; если у опции/узла `in_quest 1` или
  проверка внутри активного квеста — половина.
- Подкуп: `база × (1.3 − 0.06·AVT) × (1.2 − 0.005·barter)`, округление вверх до 5.
- Цены торговли: покупка `×max(1.0, 1.6 − 0.006·barter − R)`, продажа `×min(0.9, 0.4 + 0.004·barter + R)`;
  R: Признание +0.1, Союзник +0.2, Неприязнь −0.25.

**Подозрение** (Part I/06 §3): растёт только в зонах контроля (`trigger_rpg_zone`), источники:
бег рядом с ГО (+2/с), оружие в руках рядом с ГО (+20/с), запретная зона (+5/с), проваленная ложь (+15),
провал подкупа (+20), эффекты диалогов/карт. Спад −5 за игровую минуту вне взгляда ГО/сканеров
(множители: Благонадёжный ×1.5, Лоялист ×2, Под наблюдением ×0.5); сон дома — 0. TR-04: рост ×0.75.
Пороги: 25 проверка документов, 50 досмотр, 75 задержание, 100 тревога (`OnSuspicion25/50/75/100`
у `logic_rpg_watch`; NPC ГО с `rpg_faction cp` становятся враждебны на 100 до сброса).

**Закон** (server `game/server/hl2rpm/rpg_law.cpp`, Part I/06 §3, Part I/07 §3) — что ГО делает с подозрением:
- **25+ «Внимание»**: юнит ГО, который видит героя 4 с, подходит проверить документы (не чаще
  `rpg_law_attention_interval` 75 с); **50+ «Досмотр»** — подходит сразу; **75+ «Задержание»** — бегом.
  Юнит подходит на 110 ед. и начинает диалог `_law_stop` (узел по подозрению): документы / ложь /
  взятка / досмотр / сдаться / бежать. Ушёл из виду дольше 6 с или дальше 1400 ед. — «скрылся»: +20
  (с задержания — +100, тревога); ушёл, не дослушав, — +15. После проверки юнит возвращается на пост.
- **100 «Тревога»**: голос Надзора, ГО бьёт и стреляет — для юнитов снимается `gordon_precriminal`
  (`m_iGordonPrecriminalOverride` = 2 на время тревоги; без этого `CNPC_MetroPolice::IRelationType`
  оставляет к игроку нейтралитет при любом отношении — так «тревога ничего не делала»), юниты в 2000 ед.
  знают, где герой. Выход: скрыться до спада ниже 25 — или **сдаться**: присесть без оружия рядом с
  юнитом (240 ед., 1.2 с). Удар ГО, который уложил бы героя (здоровье ≤ 10), — **задержание**.
- **Задержание**: затемнение, штраф (сдался в разговоре 30, при тревоге 40, уложили 80 кр.; не хватает —
  долг `law_debt`, юнит при встрече требует погасить), изъятие контрабанды (`contraband > 0` в items.txt,
  неизвестное оружие), ГС −5/−15, флаг `law_arrests`, +2/4 ч, подозрение 0, здоровье не ниже 60 %; герой
  приходит в себя у `info_target rpg_arrest_release` карты (вокзал — дежурка ГО, площадь — портик вокзала),
  иначе на месте. Юниты ГО после задержания и конца тревоги забывают прежнее (замечания, погоня, дубинка).
- **Толчок**: метрокоп отталкивает героя, который упёрся в него (и делает замечание), — +6 подозрения
  (не чаще раза в 3 с; в форме ГО/Надзора и в разговоре — нет). Своего счёта замечаний с избиением
  дубинкой у юнитов больше нет: дальше — обычная лестница закона.
- «Берёт» (условие `law corrupt`): скрытый флаг юнита по имени (~30 %), `npc_gannon*` — всегда.
- Тест: `rpg_law_test <подозрение>`, `rpg_law_arrest [0|1|2]`, `rpg_law_debug 1`, `rpg_law_enable 0`.

**Уровни ГС**: ≤−75 Антигражданин, −74..−40 Злостный нарушитель, −39..−15 Под наблюдением,
−14..19 Гражданин, 20..49 Благонадёжный, ≥50 Лоялист. Ключи: `anticitizen`, `malicious`, `watched`,
`citizen`, `reliable`, `loyalist`, плюс `temppass` (флаг `civil_temp_pass`).

**Уровни фракций**: ≤−60 Враг `enemy`, −59..−25 Неприязнь `dislike`, −24..24 Нейтралитет `neutral`,
25..59 Признание `recognized`, ≥60 Союзник `ally`.

**Человечность**: 0–19 Пустой, 20–39 Чёрствый, 40–60 Человек, 61–80 Сострадательный, 81–100 Праведный.

### Сеть
User message `RPG_State` (шлётся при изменении, пачкой раз в кадр; полная синхронизация — при
спавне/загрузке/переходе и по `rpg_request_state`): записи `type byte, key string, value int|string`,
не больше ~220 байт на сообщение. Клиент держит зеркало `C_RPGCharacter` (`game/client/hl2rpm/c_rpg_character.*`).
User message `RPG_Notify`: `kind byte, token string, value int, extra string` — всплывающие уведомления
(+XP, уровень, репутация, ГС, новая локация, предмет, квест).

### Команды (тест)
`rpg_dump`, `rpg_set <поле> <значение>` (cheat), `rpg_xp <n>` (cheat), `rpg_flag <name[=v]>` / `rpg_unflag`,
`rpg_rep <faction> <delta>`, `rpg_chargen` (открыть создание персонажа), `rpg_levelup`, `rpg_request_state`,
`rpg_effects "<эффекты>"` (cheat, например `open_barter=kiosk_blagodarnost`).
Интерфейс: `rpg_chargen_commit`, `rpg_skill_add <навык> <очки>`, `rpg_perk_pick <PK-xx>`, `rpg_sleep <часы>`,
`rpg_barter_buy` / `rpg_barter_sell <classname> [количество]` (торговец - последний открытый экран `barter`;
NPC-торговец жив и не дальше 320 ед.).

### Торговля
Касса и склад торговца хранятся во флагах персонажа (значение + 1; 0 / нет флага - как в `traders.txt`):
`trd_<id>_cr` - касса, `trd_<id>.<classname>` - количество. Цена покупки = ceil( База × max(1.0, buy_mult ×
BuyPriceMult × (1 − h)) ), продажи = floor( База × min(0.9, sell_mult × SellPriceMult × (1 + h)) ), h - торг:
эффект `haggle` (% до конца игровых суток; флаги `haggle_<id>` и `haggle_<id>_day` = `world_day`).
Предметы с `value 0` не продаются и не покупаются; контрабанду берут только торговцы с `contraband 1`.
Считает один код на обеих сторонах (`CRPGCharacterData::GetTrader*`) - экран показывает итог сделки.

---

## 2. Данные (MOD `scripts/rpg/*.txt`, KeyValues, UTF-8)

| Файл | Содержимое |
|---|---|
| `attributes.txt` | 6 записей: `id`, `abbr` (LOV…), `name`/`desc` токены, `icon` |
| `skills.txt` | 13: `id`, `primary`, `secondary`, `name`, `desc`, `icon` |
| `traits.txt` | TR-01..12: `id`, `name`, `desc`, блок `mods` |
| `perks.txt` | PK-01..30 (уровни 2–6): `id`, `level`, `ranks`, `require` (условия §3), `name`, `desc`, `flavor`, `mods` |
| `factions.txt` | `id`, `name`, `start`, `hidden` |
| `items.txt` | по classname: `category` (weapon/armor/medicine/food/document/key/note/ammo/misc), `kg`, `value` (кр.), `contraband` 0..3, `name`, `desc`, `icon`, `use` (heal N / food N / note id / readable), `armor` (тип брони §8) |
| `traders.txt` | торговцы: `credits`, `buy_mult`, `sell_mult`, `restock_hours`, список `items { classname count }` |
| `locations.txt` | `C17-01` …: `name`, `xp` (20/40/80), `map` |

Блок `mods` (черты, перки, броня): `skill:<id> +N`, `attr:<id> ±N`, `suspicion_gain ×K`, `carry +N`,
`hp +N`, `start_credits +N`, `start_civil +N`, `start_rep:<faction> ±N`, `melee_damage ×K`,
`shot_spread ×K`, `armor N`. Неизвестный ключ — предупреждение в консоль, без эффекта.

---

## 3. Условия (диалоги, квесты, `logic_rpg`, `trigger_rpg`)

Каждый ключ `require_*` можно повторять; все должны выполняться. Также общий `require "<выр>, <выр>"`.

| Ключ | Значение | Пример |
|---|---|---|
| `require_attr` | `ID op N` (ID: LOV/ANA/MIG/BDI/DUH/AVT или agility…) | `ANA<=3` |
| `require_skill` | `id op N` | `speech>=50` |
| `require_faction` | `id op N` или `id:tier` | `lambda>=25`, `lambda:recognized` |
| `require_civil` | `op N` или tier | `>=20`, `reliable`, `temppass` |
| `require_item` | `class[:N]` | `item_temp_pass` |
| `require_perk` / `require_trait` | id | `PK-25` |
| `require_flag` / `require_noflag` | `name`, `name=value`, `name op N` | `mq02_path=lambda` |
| `require_humanity` / `require_resonance` / `require_suspicion` / `require_credits` / `require_level` | `op N` | `>=61` |
| `require_act` | `N` или `N-M` | `1-2` |
| `require_time` | `day` / `night` / `curfew` / `H-H` | `night` |
| `require_disposition` | `op N` (собеседник) | `>=30` |
| `require_quest_state` / `require_quest_stage` | как раньше | `mq01_pass:active` |
| `require_npc_var` | `i/n` — «характер» генерика: вариант i из n по имени собеседника | `2/8` |
| `require_law` | `contraband` / `armed` / `corrupt` / `debt` / `record` / `papers` / `stop`, можно `!` | `!corrupt` |
| `hide_if_unmet` | 1 | скрыть вместо «красного» |

Операторы: `>= <= > < = !=`.

## 4. Эффекты

Списки — через запятую. У проверки — `on_success_<ключ>` / `on_fail_<ключ>`.

| Ключ | Пример |
|---|---|
| `xp` | `25` |
| `rep` | `lambda:+10, reapers:-5` |
| `civil`, `suspicion`, `humanity`, `resonance`, `influence`, `credits` | `+2`, `-50` |
| `give_item` / `take_item` | `item_ration:2, item_water` |
| `set_flag` / `clear_flag` / `add_flag` | `mq00_registered`, `mq02_path=lambda`, `mq02_days_left:-1` |
| `disposition` / `disposition_npc` | `+5` / `npc_ostap:-5` |
| `grant_quest` / `complete_quest` / `fail_quest` / `set_stage` / `quest_event` | `sq01_can`, `mq00_arrival:2`, `talk:npc_tamara` |
| `discover` | `C17-02` |
| `damage` / `heal` | `5` |
| `start_combat` | 1 — собеседник (и его `ftag`) враждебен |
| `open_barter` / `open_chargen` / `open_levelup` | 1 / trader id |
| `haggle` | `+10` (торговец собеседника), `kadar:-5` — цены лучше/хуже на N % до конца суток |
| `fire` | `target,Input,param,delay` (I/O карты) |
| `law` | `release` (проверка окончена), `frisk` (досмотр: изъять контрабанду, штраф 30), `arrest`, `flee`, `fine:N`, `pay_debt` |
| `notify` | токен |
| `sound` | звук/саундскрипт |

---

## 5. Диалоги v2 (MOD `scripts/dialogs/<npc>.txt`; исходник — CONTENT `dialogs/<npc>.txt`)

Исходник двуязычный: каждый текст — пара `"line" "русский"` + `"line_en" "English"`
(аналогично `text`/`text_en`, `speaker`/`speaker_en`, `line_low_ana`/`line_low_ana_en`).
`build_content.py` заменяет тексты токенами (`#D_<DIALOG>_N<node>` / `#D_<DIALOG>_N<node>_O<opt>`,
или явный `vo "MQ02_S1_N3_TAMARA"`), пишет `resource/hl2rpm_dialog_{english,russian}.txt` и сценарий озвучки.

```
"Dialog"
{
	"entity"        "npc_tamara"            // targetname
	"voice"         "tamara"                // персонаж для озвучки (characters.txt)
	"speaker"       "Тамара Ильинична"  "speaker_en" "Tamara Ilyinichna"
	"auto_face_npc" "1"
	"greetings"                              // первый подходящий даёт стартовый узел
	{
		"1" { "require_flag" "mq02_done"      "node" "60" }
		"2" { "require_flag" "mq00_registered" "node" "40" }
		"3" { "node" "1" }
	}
	"nodes"
	{
		"1"
		{
			"line"         "Следующий! ...Ну, чего встал, милок? Документы."
			"line_en"      "Next! ...Well, don't just stand there, dear. Papers."
			"line_low_ana" "Документы, милок. Бумажки. Есть?"   "line_low_ana_en" "Papers, dear. Paper. Got any?"
			"emotion"      "устало, по-доброму ворчливо"          // для озвучки
			"effects"      { "set_flag" "mq00_window4" }         // при показе узла
			"variants"                                            // условная замена реплики/перехода
			{
				"1" { "require_flag" "mq00_poster_suit" "line" "..." "line_en" "..." }
			}
			"options"
			{
				"1" { "text" "Я только с поезда." "text_en" "I just got off the train." "next" "2" }
				"2" { "text" "Ваш почерк... я его где-то видел." "text_en" "..." "check" "attr:vigilance:6"
				      "next" "5" "fail_next" "6" "on_success_set_flag" "note_handwriting" }
				"3" { "text" "Я... хорошо работать." "text_en" "Me... good worker." "require_attr" "ANA<=3" "hide_if_unmet" "1" "next" "7" }
				"0" { "text" "Мне пора." "text_en" "I have to go." "exit" "1" "close" "1" }
			}
		}
	}
}
```

**Кто отвечает на E**: у каждого NPC есть ответ — свой диалог по targetname (точное имя или шаблон
`npc_cp_*`), иначе общий по ключу NPC `rpg_dialog` (`_bark_citizen`, `_bark_citizen_f`, `_bark_cp`,
`_bark_queue`, `_bark_clerk` — файлы с `_` в начале), иначе реплика фракции (citizen → `_bark_citizen`,
женская модель → `_bark_citizen_f`, cp → `_bark_cp`). Подпись общего диалога — `loot_displayname` NPC
(токены `#HL2RPM_Speaker_*`), затем `speaker` его своего диалога. Старое окно диалога (`dialog_definitions`,
`vgui_dialogpanel`) больше не открывается — раньше E на любом NPC без диалога открывало пустое окно.

**Варианты узла**: эффекты узла применяются всегда, плюс эффекты первого подходящего варианта; вариант
заменяет `line`, `sound`, `auto_next`, `end`. Взаимоисключающие исходы — двумя вариантами с
противоположными условиями или двумя опциями с одним текстом (`require_law corrupt` / `!corrupt`,
`hide_if_unmet 1`), не «эффектами узла по умолчанию».

**Узел**: `line`, `line_low_ana`, `speaker` (другой говорящий в сцене), `actor` (targetname, кто
«говорит»: поворот, сцена, звук), `choreo`, `sound`, `vo`, `emotion`, `effects`, `variants`,
`options`, `auto_next`, `end`/`close`, `spoils`, `option_delay`.

**Опция**: `text`, `next`, `fail_next`, `check`, `check_label`, `check_hidden`, `check_once`,
`check_xp`, `in_quest`, `require_*`, `hide_if_unmet`, эффекты §4, `close`, `exit`, `spoils`, `tag`.

**Проверки** (`check`):

| Значение | Подпись | Порог |
|---|---|---|
| `skill:<id>:N` | `[Красноречие 50]` | навык |
| `attr:<id>:N` | `[Мощь 7]` | характеристика |
| `lie:N` | `[Ложь 35]` | speech |
| `intimidate:MIG:6/speech:40` | `[Запугивание]` | лучший из двух |
| `bribe:N` | `[Подкуп 50 кр.]` | цена по формуле; успех = хватает кредитов и NPC «берёт» (флаг/фракция), снимает кредиты |
| `barter:N` | `[Торговля 35]` | barter |

Состояние опции для клиента: обычная / проверка проходит (зелёная) / не проходит (красная, но
выбирается) / недоступна (серая) / выход. Повторить проваленную `check_once` нельзя
(флаг `chk_<entity>_<node>_<opt>`). Нельзя `|`, `;`, `~` в текстах исходников не опасно — транспорт
кодирует поля (`%xx`).

**Метки перед текстом** (`tag`, ставит клиент): `[λ]` Лямбда, `[ГС]`, `[Перк]`, `[Предмет]`,
`[Резонанс]`, `[Сострадание]`, `[Жестокость]`, простак — без метки.

---

## 6. Квесты v2 (MOD `scripts/quests/<id>.txt`; исходник — CONTENT `quests/`)

```
"Quest"
{
	"id"        "mq00_arrival"
	"category"  "main"            // main / faction / side / companion
	"faction"   ""                // для faction-квестов
	"title"     "Прибытие"        "title_en" "Arrival"
	"desc"      "..."             "desc_en"  "..."
	"xp"        "200"
	"act"       "1"
	"auto_track" "1"
	"complete_on_last_stage" "1"
	"rewards"   { "item_ration" "1" }      // только предметы
	"on_complete" { "set_flag" "mq00_done" "credits" "+0" }
	"stages"
	{
		"0"
		{
			"title" "..." "title_en" "..."   "desc" "..." "desc_en" "..."
			"marker" "npc_tamara"             // targetname для компаса
			"marker_map" "rp_c17_01_trainstation"
			"objectives"
			{
				"1" { "type" "talk"  "target" "npc_tamara" }
				"2" { "type" "flag"  "target" "mq00_registered" }
				"3" { "type" "reach" "target" "loc_plaza" "optional" "1" }
				"4" { "type" "kill"  "target" "zombie_1;zombie_2" "count" "2" }
				"5" { "type" "item"  "target" "item_temp_pass" "count" "1" }
				"6" { "type" "event" "target" "mq00_vort_scene" }
			}
			"on_complete" { "xp" "25" }
		}
	}
}
```

События: `talk:<npc>` (конец разговора), `reach:<name>` (`trigger_rpg` с `quest_event`),
`event:<name>` (`logic_rpg QuestEvent`), `kill` (как раньше), `item`/`flag` — проверяются при
изменении инвентаря/флагов. Журнал получает id + токены, текст локализует клиент.
Старый блок `quests` внутри диалогов работает как раньше.

---

## 7. Энтити карт (`hl2rpm.fgd`, server `game/server/hl2rpm/rpg_entities.cpp`)

| Класс | Что делает |
|---|---|
| `logic_rpg` | входы: `ApplyEffects` (ключ `effects`, формат §4 как `ключ=значение, ...`), `AddXP`, `SetFlag`, `ClearFlag`, `AddRep "<f> <d>"`, `AddCivil`, `AddSuspicion`, `AddCredits`, `AddHumanity`, `AddResonance`, `GiveItem`, `TakeItem`, `GrantQuest`, `CompleteQuest`, `FailQuest`, `SetQuestStage`, `QuestEvent`, `Discover`, `SetAct`, `OpenCharGen`, `OpenBarter`, `Notify`, `Test` (ключ `condition`) / `TestCondition <выр>` → `OnTrue`/`OnFalse`; `OnMapSpawn` с `test_on_spawn 1` — для слоёв по флагам | С `test_on_spawn 1` условие ещё и перепроверяется, когда меняются флаги/предметы персонажа (выходы — только при смене результата): барьеры и слои карты по флагам открываются сразу.
| `trigger_rpg` | браш: игрок вошёл → `condition`, `effects`, `quest_event`, `discover`, `once`; `OnPass`/`OnFail` |
| `trigger_rpg_zone` | браш: зона контроля Альянса (`zone 1` обычная, `2` запретная): подозрение растёт только тут; HUD показывает «глаз» |
| `prop_rpg_usable` | модель с действием по +use (§11): записка/терминал → окно чтения и «Заметки», кровать → сон, иначе эффекты; `condition`, `effects`, `once` |
| `point_rpg_vo` | (опц.) реплика-«барк» по ID озвучки с субтитром |
| NPC (BaseNPC) | новые ключи `rpg_faction` (cp/lambda/…), `rpg_trader` (id торговца), `rpg_disposition` |

---

## 8. Броня и интерфейсы

| `armor` в `items.txt` | Пример предмета | HUD-тема | Броня | Маскировка |
|---|---|---|---|---|
| `citizen` | роба (без предмета) | **Браслет**: минимализм, янтарно-белый, здоровье/патроны маленькие | 0 | гражданин |
| `worker` | `item_suit_worker` (комбинезон ИК) | Браслет + «наряд» | 5 | рабочий |
| `cp` | `item_suit_cp` (форма ГО) | **Маска ГО**: тонкий синий интерфейс, номер юнита, коды, виньетка маски | 15 | ГО (проверка Скрытность+5·АВТ) |
| `overwatch` | `item_combine_soldier_suit` | **Надзор**: бело-голубой, сетка, щит | 30 | Надзор (без импланта — раскрытие) |
| `rebel` | `item_suit_rebel` | **Повстанец**: оранжевый λ, потёртый | 10 | подозрение 100 в зонах |
| `hev` | `item_suit` | **HEV**: классический оранжевый HL2 | 50 + заряд | — |

Тема — сетевая переменная игрока (`m_iSuitType`, сохраняется), HUD перекрашивается и меняет набор элементов
без перезагрузки. Классический HUD HL2 (здоровье/патроны) виден и без костюма (в теме «Браслет»).

---

## 9. Интерфейс (клиент `game/client/hl2rpm/`)

Общий стиль — «как если бы Valve сделала RPG в HL2»: сдержанно, функционально, диегетично.
- **Альянс/браслет**: тёмная полупрозрачная панель `12 16 20 235`, акцент голубой Альянса `110 190 255`,
  вторичный — белый `220 230 240`, предупреждение `255 170 60`, опасность `230 70 60`, успех `120 220 140`;
  тонкие линии 1 px, угловые скобки, лёгкие сканлайны; шрифт данных — моноширинный с кириллицей.
- **HL2/HEV**: стоковая палитра HUD `255 220 0`/`255 176 0` на `0 0 0 76`.
- Шрифты — с кириллицей (TTF в `resource/`, лицензия OFL), регистрируются в `clientscheme.res`.

Экраны: HUD (компас с метками квеста, «глаз» подозрения, лента уведомлений, иконки состояний, ГС),
**браслет** (TAB/I; вкладки Гражданин / Имущество / Данные: задания, заметки, отношения, карта / Пайки / Радио),
**создание персонажа** («Карточка гражданина»: имя, Λ.А.М.Б.Д.А. 34 очка, 3 специализации, 0–2 черты),
**новый уровень** (навыки → перк), **диалог** (говорящий, реплика, цветные проверки, клавиши 1–9, Esc),
**торговля** (`rpg_barter.cpp`: товар торговца и имущество героя, цена по навыку Торговли, касса торговца,
ЛКМ — 1 шт., ПКМ — 5 шт.; отказ сервера — строкой на экране), **чтение записки**, **сон**.

---

## 10. Озвучка

ID реплики = имя токена (`MQ02_S1_N3_TAMARA` или `D_<DIALOG>_N<n>`). Файл:
`MOD/sound/vo/hl2rpm/<english|russian>/<voice>/<ID>.wav` (44.1 кГц, 16 бит, моно).
Есть файл для языка игры — играет он (с длительностью по файлу); нет — реплика текстом с паузой по длине.
Сценарий для записи генерирует `build_content.py` → `Дизайн-документ/Озвучка/` (по персонажам, RU+EN).

---

## 11. Канонические имена вертикального среза

**Карты** (`rp_<регион>_<nn>_<имя>`; основа — ретейловые карты HL2, декомпиляция BSPSource, сюжет Гордона вырезан):

| Карта | Сектора | Основа | Переходы (`info_landmark`) |
|---|---|---|---|
| `rp_c17_01_trainstation` | SP-00 сон, C17-00 поезд, C17-01 вокзал (перрон, КПП, регистрационный зал с окном 4, пост ГО, камера хранения, медпункт, каморка вортигонтов) | `d1_trainstation_01` | `lm_01_02` → plaza |
| `rp_c17_02_plaza` | C17-02 площадь + зал вокзала, Бюро трудовых ресурсов и приёмная Ружены (пристройка Администрации), подземный переход | `d1_trainstation_02` | `lm_01_02`, `lm_02_03` → block7, `lm_02_08` → opora |
| `rp_c17_03_block7` | C17-03 Квартал 7: двор, дом 7/3 (кв. 2, 4, 9, 11, 14, 17, 20, чердак, подвал), улица Старого города: часовая мастерская Лукича, баня Кадара, будка «Шлюз-3» у канала | `d1_trainstation_03` (+`_04`) | `lm_02_03` |
| `rp_c17_08_opora` | C17-08 участок ГО «Опора» (бывшая школа №3): КПП, дежурная часть, допросные, камеры, тир/спортзал, двор, казарма, кабинет Шульца, курилка | новая/из фрагментов | `lm_02_08`, `lm_08_04` → oldtown (восточная арка) |
| `rp_c17_04_oldtown` | C17-04 Старый город: улица Ключевая, часовая мастерская Лукича с подвалом-явкой «Часовщик» (Лейла), баня Кадара (вестибюль, парилка), «Толкучка», набережная с мостом (пост ГО «Виктор-5», облава LM-01), C17-14 «Шлюз-3» — будка шлюзовщика Эрнесто на том берегу | новая (`make_oldtown.py` + `phaseb_rp_c17_04.py`) | `lm_08_04` → opora (западная арка) |

**Локации** (`locations.txt`): `C17-00` Поезд, `C17-01` Вокзал, `C17-02` Привокзальная площадь,
`C17-03` Квартал 7, `C17-04` Старый город, `C17-07` Бюро трудовых ресурсов, `C17-08` Участок «Опора»,
`C17-14` «Шлюз-3».

**NPC (targetname → голос `voice`)**:
`npc_gman` (gman), `npc_oldman_train` (oldman), `npc_yun_sohi` (sohi), `npc_cp_xray7` (cp), `npc_cp_checkpoint` (cp),
`npc_cp_union12` (cp), `npc_cp_stik4` (cp), `npc_cp_line6` (cp), `npc_cp_victor5` (cp), `npc_savva` (savva),
`npc_tamara` (tamara), `npc_mitra` (mitra), `npc_victor6` (victor6), `npc_oswaldo` (oswaldo), `npc_amani` (amani),
`npc_ostap` (ostap), `npc_gary` (gary), `npc_rosario` (rosario), `npc_galina` (galina), `npc_tiago` (tiago),
`npc_lotta` (lotta), `npc_ilona` (ilona, ларёк «Благодарность», торговец `kiosk_blagodarnost`), `npc_bureau_clerk` (clerk), `npc_gannon` (gannon), `npc_schulz` (schulz), `npc_barney` (barney),
`npc_lukich` (lukich), `npc_leila` (leila), `npc_ernesto` (ernesto), `npc_kadar` (kadar), `npc_mateo` (mateo),
`npc_tishina` (—, молчит), `npc_ruzhena` (ruzhena), `npc_priya` (priya), `npc_radek` (radek), `npc_violator` (violator),
`npc_breen_screen` (breen, только ванильные Брин-касты). Генерики: `npc_cit_*` (citizen_m/citizen_f), `npc_cp_*` (cp).
Голос Надзора — `overwatch` (объявления, `point_rpg_vo`).

**Фракции** (`rpg_faction`): `lambda`, `ferrymen`, `reapers`, `freefolk`, `chorus`, `shareholders`, `rust`,
`writtenoff`, `remnant`, `gardeners`; служебные (без шкалы, ГС): `cp`, `overwatch`, `admin`, `citizen`, `vort`.

**Предметы** (`items.txt`, classname): `item_temp_pass` (временный пропуск 17-Т-0451), `item_account_card`
(учётная карта), `item_forged_id_card`, `item_lambda_card`, `item_ration` (паёк), `item_water_can` («>B»),
`item_ration_coupon` (талон), `item_pocket_note` («Помни: ты не отсюда»), `item_citizen_memo` (памятка),
`item_samizdat_package`, `item_kadar_package`, `item_chocolate`, `item_zenit_camera`, `item_letter_113`,
`item_work_order`, `item_lockpick`, `item_suit_cp` (форма ГО), `item_suit_worker`, `item_suit_rebel`;
ванильные — `item_healthkit`, `item_healthvial`, `item_battery`, `weapon_stunstick`, `weapon_pistol`…

**Флаги — префиксы**: `mq00_*`, `mq01_*`, `mq02_*`, `sq01_*`, `go01_*`, `lm01_*`, `sm01_*`, `world_*`
(`world_act` = 1), `civil_temp_pass`, `chk_*` (проваленные проверки).

**Квесты**: `mq00_arrival`, `mq01_temp_pass`, `mq02_papers`, `sq01_can`, `go01_volunteers`, `lm01_lambda_sign`,
`sm01_bathhouse` (+ хуки: `sq02_neighbors`, `sq05_cat`, `sq10_zenit`, `sq17_separated`).

**Использование предметов мира** (`prop_rpg_usable`): `use_action` = `note` (окно чтения + «Заметки»),
`sleep` (кровать: выбор часов, сброс подозрения, проверка комендантского часа), `effects` (только эффекты),
`terminal` (текст терминала); общие ключи `condition`, `effects`, `once`, `name` (подсказка «[E] …»).

---

## 12. Файлы локализации и имена токенов

| Файл (MOD `resource/`, UTF-16LE с BOM) | Кто пишет | Что внутри |
|---|---|---|
| `hl2rpm_<lang>.txt` | код (через `hl2rpm_extras/tools/add_loc_tokens.py`), грузит движок | меню, настройки графики, **подписи интерфейса** `#HL2RPM_UI_*`, `#HL2RPM_HUD_*` |
| `hl2rpm_rpg_<lang>.txt` | `build_content.py` из CONTENT `strings/rpg.txt`, грузит клиент (`AddFile`) | `#RPG_Attr_<id>`, `#RPG_Attr_<id>_Desc`, `#RPG_Skill_<id>(_Desc)`, `#RPG_Trait_<TR-01>(_Desc)`, `#RPG_Perk_<PK-01>(_Desc/_Flavor)`, `#RPG_Faction_<id>`, `#RPG_FactionTier_<tier>`, `#RPG_Civil_<tier>`, `#RPG_Item_<classname>(_Desc)`, `#RPG_Loc_<C17-01>`, `#RPG_Check_<kind>` |
| `hl2rpm_dialog_<lang>.txt` | `build_content.py` из CONTENT `dialogs/`, `quests/`, `strings/texts.txt` | `#D_<dialog>_N<node>[_V<n>][_LOW]`, `#D_<dialog>_N<node>_O<opt>`, `#D_<dialog>_SPEAKER`, явные ID озвучки (`MQ02_S1_N3_TAMARA`), `#Q_<quest>_TITLE/_DESC/_S<n>_TITLE/_S<n>_DESC`, `#NOTE_<id>_TITLE/_TEXT`, `#BARK_<id>`, `#OW_<id>` (Надзор), `#BC_<id>` (Брин-касты) |

Исходник строк (CONTENT `strings/*.txt`):
```
"Strings"
{
	"RPG_Skill_speech"      { "ru" "Красноречие"  "en" "Speech" }
	"NOTE_pocket_TEXT"      { "ru" "Помни: ты не отсюда."  "en" "Remember: you are not from here." }
	"OW_G01"                { "ru" "..." "en" "..." "voice" "overwatch" "sound" "npc/overwatch/cityvoice/..." }
}
```

### 11.1 Открытый мир: сектора вместо глав (решение 2026-10-28)

Мир — открытые сектора в духе Fallout: New Vegas, а не линейные главы. Соседние главы Valve, которые
делят одно место (в оригинале конец одной карты — обрезанная копия начала следующей), **сливаются в
один сектор**: смещение — по паре `info_landmark` Valve, дубли перекрытия удаляются (по каждой стороне
шва остаётся одна полная копия). На внешних границах сектора приём Valve с «обрезанным соседом» за
переходом сохраняется — так переход выглядит бесшовно. Фальшивые дома 3D-скайбокса там, куда игрок
должен попасть (пути к Кварталу 7, «Опоре», Старому городу), заменяются настоящими улицами в полный
масштаб; дальний город остаётся скайбоксом. Пределы Source (±16384 ед., 8192 браша, 2048 edicts) —
причина делить мир на сектора, а не делать одну карту.

| Сектор (карта) | Из чего |
|---|---|
| `rp_c17_01_trainstation` | `d1_trainstation_01` + `_02` слиты: сон, поезд, вокзал, зал, **площадь** (бывший `rp_c17_02_plaza`), пристройка Администрации, улицы к Кварталу 7 и «Опоре». Переходы: `lm_02_03` → block7, `lm_02_08` → opora |
| `rp_c17_03_block7` | `d1_trainstation_03` + `_04` слиты, + улица Старого города, канал «Шлюз-3». Переход `lm_02_03` → trainstation |
| `rp_c17_08_opora` | новая карта. Переход `lm_02_08` → trainstation |

**Состояние (2026-10-29):** слияние `01` + `02` в один сектор ещё не сделано — сейчас это две карты с
переходом `lm_01_02` у выхода из вокзала (квесты и `locations.txt` ссылаются на `rp_c17_02_plaza`).
Когда сектор будет слит, `rp_c17_02_plaza` в данных заменить на `rp_c17_01_trainstation`.

### 11.2 Система большого мира (аналог «ячеек» Fallout: New Vegas)

Движок закрыт: геометрию BSP подгружать по частям нельзя, предел координат ±16384 вшит в engine.dll.
Поэтому большой мир собирается из слоёв, как у Bethesda:

1. **Мировое пространство из секторов** — внешние карты до ~830×830 м; соседние сектора стыкуются
   переходами `trigger_changelevel` + `info_landmark` по краю, на краю видна обрезанная копия соседа
   (бесшовно). Состояние мира, персонажа, квестов, NPC переживает переходы.
2. **Интерьеры — отдельные карты**, вход через двери (как ячейки-интерьеры в NV): баня, участок,
   НИИ, квартиры-квестовые локации. Внешний сектор не тратит пределы браш/edicts на интерьеры.
3. **Подгрузка «жизни» по расстоянию** — **сделано** (`game/server/hl2rpm/rpg_streaming.cpp`, 2026-10-29).
   Энтити карты с ключом `rpg_stream 1` (NPC, ящики, лут, патрули; в FGD — у `BaseNPC`, у прочих
   ключ ставится вручную) вынимаются из entity lump до разбора карты (`CServerGameDLL::LevelInit` →
   `RPGStreaming_PrepareMapEntities`) и создаются из своего блока ключей, когда игрок ближе
   `rpg_stream_radius` (2600) к их месту; уходят дальше радиуса + `rpg_stream_hysteresis` (900) и только
   вне поля зрения (не в бою, не в скрипте, не во время разговора). Ушедшая запоминает место, углы и
   здоровье, убитая больше не появляется; состояние — в сохранении уровня (блок `HL2RPMStreaming`,
   переживает save/load и возвращение на посещённую карту; стримленные энтити помечаются
   `m_iHammerID` = −1000000 − индекс). Не стримятся энтити с `parentname`. I/O карты к стримленной
   энтити работает, пока она существует. `rpg_stream_per_update` (12, 4 раза в секунду) — сколько
   появляется/уходит за обновление, `rpg_stream_info` — счётчики и занятые edicts, `rpg_stream_debug 1/2`.
   Проверка — тестовая карта `stream_test` (`hl2rpm_extras/tools/make_stream_test.py`): 2600 энтити на
   поле 14 000² ед., без подгрузки карта не загрузилась бы (предел 2048); живых 190–350, edicts 320–480.
   Не сделано: состояние лута контейнеров (обыскан ли) и отношение NPC — хранятся в самой энтити и
   при уходе теряются (сейчас стримить имеет смысл «фоновую жизнь»: горожан, патрули, ящики).
4. **Дальний план** — соседние сектора на горизонте в 3D-скайбоксе (масштаб 1/16), упрощённые
   копии их геометрии, генерируются инструментом. *(не сделано)*
5. **Быстрое перемещение — трамвай** — **сделано** (2026-10-29): остановка — `prop_rpg_usable use_action travel`
   (столб `props_c17/signpole001`, `slice_kit.tram_stop`) открывает экран маршрута (`CRPGTravel`, `rpg_reader.cpp`) со всеми
   открытыми локациями `travel 1` из `locations.txt`; `rpg_travel <локация>` (сервер, `rpg_commands.cpp`) проверяет, что
   локация открыта, это другая карта, подозрение < 50 и на карте есть `info_landmark lm_travel`, сдвигает часы на
   `rpg_travel_hours` (0.5) и делает `changelevel2 <карта> lm_travel`; на новой карте игрок ставится в
   `info_target travel_arrive`. Крошечный `trigger_transition lm_travel` под потолком не даёт NPC у остановки уехать
   вместе с игроком. Остановки: вокзал (зал КПП), площадь, «Опора», Старый город.
