---
name: hl2rpm-dialogs
description: Work with the HL2RPM RPG systems - NPC dialogs (scripts/dialogs/*.txt), quests, reputation, factions, loot (NPC corpses, func_loot_container, prop_loot_container), inventory, the HL2RPM Creation Kit world layer (scripts/npcs, scripts/world - NPCs placed without Hammer, markers, AI packages / daily routine, nav nodes) and their Hammer entities/keys (trigger_dialog_start, ftag, spoil_on_dmg, loot_items...). Use to write or fix a dialog, quest or NPC routine, place a talking NPC on a map, validate dialog files, or debug why a dialog/quest/loot/routine does not work.
---

# HL2RPM dialogs, quests, loot

This procedure works with any AI model. Read `../../PROJECT.md`,
`../../CURRENT_STATUS.md`, and the relevant `../../topics/{rpg-systems,world-ai,ck-editor}.md`.
Resolve `scripts/...` relative to this skill's directory; other source paths are
relative to `sp/src`. Use equivalent file/shell/image tools from your environment.

Server code: `game/server/dialog_definitions.cpp` (file parsing, payload to the
client), `dialogsystem_server.cpp` (choices, quest actions), `quest_system.cpp`,
`loot_system.cpp` + `loot_container.cpp`, `inventory_system.cpp`;
client UI: `game/client/vgui_dialogpanel.cpp`, `vgui_lootpanel.cpp`,
`vgui_inventorypanel.cpp`. Map-side keys live in the FGDs (`base_vizzys.fgd`
BaseNPC block, `hl2rpm.fgd`). Placing entities: `hl2rpm-hammer` skill.
Testing in game: `hl2rpm-playtest` skill.

## The HL2RPM Creation Kit (preferred way for the user; formerly "Dialog Editor")

`E:\SourceModding\HL2RPMSOURCE\HL2RPM Creation Kit\HL2RPM Creation Kit.exe`
(sources + build: `hl2rpm_extras/tools/dialog_editor`, entry `hl2rpm_creation_kit.py`,
`build_editor.ps1`; README there). Besides dialogs it has a 3D map window (GECK-like: place
NPCs, markers, items, containers, props, patrol paths on compiled maps), NPC actor data and
AI packages, factions and nav nodes - see "World layer" below. Previews (M7): a model viewer with all
animations of a model and its $includemodel models (map context menu / NPC "Анимации…"), "Показать → Анимации NPC"
(NPCs play their package of the chosen hour on its marker) and "Показать → Освещение / Ночь" (lamps of the map +
the sun of the hour, cube shadows of the nearest 4); "Манекены на маркерах" (on by default, GECK-like: a figure on
every marker plays its animation). The same markers in the game let the player sit / lie / lean (`hl2rpm_seat.cpp`,
prompt "[E] Сесть", third person). Demo content kept on ck_test (locker + key + note + terminals + turret post +
quest): `hl2rpm_extras/tools/ck_demo_content.py`. Screen grabs (`--shot`, grabWindow) capture whatever window is
on top - for reports grab the widget (`widget.grab()`) and never send a grab showing other windows.
A project (`*.hl2dlg`, JSON) holds NPCs, nodes, quests, voices of all languages and
canvas positions. Saving writes `scripts/dialogs/<npc>.txt`, converts/copies voices to
`sound/dialogs/<npc>/<language>/<key>.wav` and generates lip sync scenes
`scenes/dialogs/<npc>/<language>/<key>.vcd` (`<key>` = node id, `returning`,
`<node>_<option>_after`) into the project's game folder, plus `scripts/npcs/<npc>.txt`,
`scripts/world/<map>.txt`, `scripts/world/<map>_nav.txt`, `scripts/world/factions.txt`. Lip sync = the mod's
generator (`dlgedit/lipsync_gen/vcd_english.py`, `vcd_russian.py`), regenerated only
for changed text/audio. Files written by the editor carry the header
`// HL2RPM Creation Kit: generated` (older: `HL2RPM Dialog Editor: generated`) — **edit those in the program, not by hand**
(hand edits are overwritten on the next save; import them back with Файл → Импорт).
The user's test project: `E:\SourceModding\HL2RPMSOURCE\dialogsrc\test_dialogs.hl2dlg`
(game folder = `hl2rpm\testmaps`). Legacy screenshot mode below captures the screen and can include unrelated windows; do not use that output for reports. Use a verified window-only capture (PrintWindow / widget capture), noting that `widget.grab()` can miss 3D labels. Historical CLI options:
`"HL2RPM Creation Kit.exe" <project> --shot out.png --npc demo_npc_1 --lang russian --zoom 0.85 --select node:1|option:1:2|returning|quest:<id>|ref:<id>|hammer:<entity index>|faction:<id> [--sim --sim-steps c,0] [--tab map --map ck_test --cam x,y,z,pitch,yaw --hour 23 --npc-tab 0|1|2 --left N]`.

## World layer and NPC routines (Creation Kit)

Server: `game/server/hl2rpm_world.cpp` (+ `.h`), behavior `hl2/ai_behavior_rpmroutine.cpp`.
Test map: `ck_test` (house with bed/table/chairs, yard, no info_node; `hl2rpm_extras/tools/make_ck_test.py`),
its content in `testmaps/scripts/world/ck_test*.txt`, `testmaps/scripts/npcs/ck_*.txt`.

- `scripts/npcs/<targetname>.txt` `"NPC"`: `classname`, `model`, `skin`, `weapon`, `squad`, `faction` (= ftag),
  `player_relation` friend|neutral|enemy, `health`, `essential` 1 (never dies), `commandable` 1 (may join the
  player's squad - otherwise citizens are made SF_CITIZEN_NOT_COMMANDABLE), `display_name[_<lang>]`,
  `keyvalues { ... }` (citizentype, hate_on_dmg, loot_items, loot_displayname...), `packages { "0" {...} }`.
  Applies to a layer-spawned NPC and to a Hammer NPC with the same targetname (routine, faction, flags).
- Package keys: `name`, `type` (sleep eat sandbox patrol wander travel guard use_marker wait follow talk),
  `start` (hour), `duration` (hours, 24 = always), `location` self|ref|point|player|current, `ref`, `point "x y z"`,
  `yaw`, `radius`, `run`, `anim` (actbusy name), `markers "sit;lean"`, `path`, `path_mode` loop|pingpong|once,
  `target` (follow), `once` (talk), `min_use`/`max_use` (sandbox), `require_quest_state "q:active"`,
  `require_quest_stage "q:1"`. The first matching package runs (GECK order).
- `scripts/world/<map>.txt` `"World" { "refs" { "<id>" { "type" ... "origin" "x y z" "angles" "p y r" } } }`:
  `npc` (`npc` = targetname of a definition), `marker` (`name`, `kind` sleep|sit|eat|lean|ground|work|guard|idle,
  `anim`, `owner`, `seat_h` - seat / mattress height for the library animations), `item` (`classname`),
  `container` (`model`, `loot_items`, `loot_displayname[_<lang>]`),
  `prop` (`model`, `physics`), `path` (`name`, `points { "0" { "origin" "wait" "yaw" "anim" } }`), `entity`
  (`classname`, `keyvalues`). Spawned once on a new map (never on a loaded save / revisited level).
- Library animations of markers (hl2rpm_extras/tools/anims, `../../topics/animation.md`): `rpm_sit_chair`,
  `rpm_sit_couch`, `rpm_eat_chair` (blend by `seat_h`), `rpm_lie_bed` (mattress height, the head to the marker's left),
  `rpm_lean_wall` (marker 14 in front of the wall, back to it), `rpm_sit_ground`. Place them with the kit's buttons
  ("Подогнать к мебели / к кровати", "Прислонить к стене", "Поставить на пол"); test with
  `rpm_marker_use <npc> <marker>` / `rpm_marker_leave <npc>`, the player with `rpm_seat_at <marker>`.
- NPC behaviour keys in `scripts/npcs`: `yield` (step aside from the player), `pickup_weapons` (0/1/2 = squad only),
  `weapon_law` (0 off, 1 civilian, 2 warn, 3 fire); rules `npc_yield`, `npc_pickup_weapons`, `npc_fists`,
  `npc_talk_gestures`, `weapon_law*` in `scripts/rpg/rpg.txt`. Bark event `weapon_law` (warn/final/attack/comply).
- More ref types: `spawner` (`keyvalues { npcs "def:3;npc_headcrab" count_min count_max radius activate_dist
  from_hour to_hour respawn_hours require_flag }` -> `rpm_spawner`), `transition` (`name`, `target_map`, `target` =
  name of the paired transition there, `size "w d h"`, `arrive "x y z"`, `arrive_yaw`; the server makes landmarks,
  box trigger_changelevel / trigger_transition itself - no map rebuild; both maps need their half of the pair).
- NPC definition extras: `template` is resolved by the kit at export; `models`/`weapons` "a;b" (empty weapon = none),
  `names { "0" { "english" ".." "russian" ".." "female" "1" } }`, `skin_max`, `proficiency` 0..4, `xp`,
  `vendor { list "list:<id>" money markup pays restock_hours currency }` (+ dialog option `"trade" "1"`).
- RPG data: `scripts/rpg/items.txt` (`"Items" { "<id>" { name[_lang] desc text category base weight value heal
  stackable quest damage ... } }`; `base` note/key/currency/generic -> `item_rpm`), `scripts/rpg/lootlists.txt`
  (`list:<id>` in any loot string, rolled when first opened). Locks: container `lock_key`/`lock_level`, door
  `rpm_key`/`rpm_lock_level`. Console: `rpm_give <id> [n]`, `rpm_trade <npc>`, `rpm_vendor_dump`, `rpm_state_dump`,
  `rpm_flag`, `rpm_var`, `rpm_loot_roll <list>`.
- `scripts/world/factions.txt` `"Factions" { "<id>" { "player" "friend" "relations" { "<other>" "enemy" } } }`.
  Without faction relations metrocops/soldiers kill citizens on sight (HL2 class relations).
- Nav: `rpm_nav_generate [spacing] [max] [file]` writes AI nodes; `scripts/world/<map>_nav.txt` ("x y z" lines)
  is added to the node graph, a changed file rebuilds the `.ain`.
- Console: `rpm_world_reload [1]` (1 = respawn the layer), `rpm_world_dump`, `rpm_ai_debug 1|2`,
  `sv_timecycle_set_time <h>` to jump in time. Test: `run_game.ps1 -Map ck_test -Steps "shot|sv_timecycle_set_time 23; rpm_ai_debug 1"`.

## Languages

One file carries every language: plain keys = the project's base language (english),
`<key>_<language>` = translations (`line_russian`, `text_russian`, `after_line_*`,
`returning_line_*`, `display_name_*`, quest/stage `title_*`/`desc_*`) and per-language
voice (`choreo_russian`, `sound_russian`, `returning_choreo_*`, `returning_sound_*`,
`after_choreo_*`, `after_sound_*`). A present `<key>_<lang>` wins even when empty
(`"choreo_russian" ""` = no Russian voice, text only). The server picks the language
from `dialog_language` (empty = `cl_language`, then Steam; on this machine `russian`);
changing it reloads the dialogs at once (`dialog_language english` to test).
The old copies `demo_npc_1_rus.txt` / `demo_npc_2_rus.txt` (entity `*_rus`, quests `*_rus`,
for `demo_map_rus`) still load; the editor merged them into `demo_npc_1`/`demo_npc_2`.

## How a dialog starts

- The player presses USE on **any entity whose targetname has a dialog file**
  (`CBaseEntity::Use` → `DialogSystemServer::StartDialogForEntity`).
- `trigger_dialog_start` (brush trigger): `npc_name` (targetname), `trigger_once`,
  `npc_approach` 1 = the NPC walks/runs (`approach_run`) to the player first,
  `approach_distance` (96). Needs line of sight at the end of the approach.
- Console: `dialog_force <targetname>`; reload files after editing:
  `dialog_reload` (no cheats needed; the editor sends it with `hl2.exe -hijack`).
  Debug prints: `dialog_debug 1` (+ `developer 1`).

The dialog key is the NPC **targetname**, case-insensitive. Without a
targetname no dialog is possible. The name shown in the dialog/loot window is
`display_name` of the dialog file (localized); NPCs without it use the Hammer key
`loot_displayname` (`CAI_BaseNPC::GetLootDisplayName`).

## Dialog file: `hl2rpm\scripts\dialogs\<any>.txt`

KeyValues text, UTF-8 (Russian text is fine). One file per NPC; if two files use
the same `entity`, the one loaded later (alphabetical) wins — the `*_rus.txt`
files in the mod use different entity names (`demo_npc_1_rus`) for the Russian map.

```
"Dialog"
{
	"entity"        "trader_1"          // targetname of the NPC
	"start_node"    "0"
	"auto_face_npc" "1"                 // camera turns to the NPC

	"display_name"          "Trader"              // dialog + loot window name ("display_name_russian" ...)

	// second and later visits (after the dialog was finished once)
	"returning_line"        "Back again?"
	"returning_choreo"      "scenes/trader_1/returning"   // its own voice only (never the node's)
	"returning_auto_next"   "1"          // options of this node follow the line (auto_next chain is
	                                     // followed); with returning_no_options: node to go to after it
	"returning_no_options"  "0"
	"returning_auto_close"  "0"

	"quests" { ... see below ... }       // quests are global: any dialog may use any quest

	"nodes"
	{
		"0"
		{
			"line"         "What do you want?"
			"choreo"       "scenes/trader_1/0"     // .vcd (extension optional)
			"sound"        ""                       // or a plain sound instead of a scene
			"sequence"     ""                       // NPC animation to play
			"option_delay" "auto"                   // seconds before options appear, auto = after the line
			"auto_next"    "1"                      // no options: continue with node 1
		}
		"1"
		{
			"line" "Trade or talk?"
			"options"
			{
				"0" { "text" "Show me your goods."  "next" "10" }
				"1" { "text" "Any work?"            "next" "20" "require_quest_state" "rats:inactive" }
				"2" { "text" "The rats are dead."   "next" "-1" "complete_quest" "rats" "after_next" "30" "require_quest_stage" "rats:1" }
				"3" { "text" "Bye."                 "next" "-1" "close" "1" }
			}
		}
		"30" { "line" "Here is your pay." "end" "1" }      // end/close = close after the line
	}
}
```

Node keys: `line`, `choreo`, `sound`, `sequence`, `server_sequence` (targetname
of a scripted_sequence/logic entity that gets `BeginSequence`), `option_delay`,
`auto_next`, `end`/`close`/`auto_close`, `spoils` (1 = after this node the NPC never talks again).

Option keys:
- flow: `text` (≤ 255 bytes), `next` (-1 = no node), `close` 1, `spoils` 1,
  `server_sequence`.
- quest actions: `grant_quest <id>`, `complete_quest <id>`, `fail_quest <id>`,
  `set_stage <id>:<n>`; after completing: `after_next <node>` or
  `after_line`/`after_choreo`/`after_sound`/`after_sequence`.
- conditions (option hidden when false): `require_rep <n>` (reputation ≥ n),
  `require_quest_state <id>:inactive|active|completed|failed`,
  `require_quest_stage <id>:<n>` (active and stage ≥ n), or `require_quest` +
  `require_stage`. Options with `grant_quest` hide themselves once that quest is
  active/completed.

**Never use `|`, `;` or `~` in lines and option texts** — they are the field
separators of the payload sent to the client. Quotes are escaped automatically.

### Quests (inside any dialog's "quests" block)

```
"quests"
{
	"rats"
	{
		"title" "Rat problem"  "desc" "Clear the cellar."  "min_rep" "0"
		"fail_on_kill" "trader_1"  "fail_rep_penalty" "-10"   // killing him fails it, costs reputation
		"stages"
		{
			"0" { "title" "Kill the rats" "desc" "..." "kill_targets" "rat_1;rat_2;rat_3" "kill_count" "3" }
			"1" { "title" "Report back"   "desc" "Talk to the trader." }
		}
		"rewards" { "item_healthkit" "2"  "weapon_pistol" "1" }
	}
}
```
Kill stages advance automatically when the named NPCs die. Reputation only
changes through quest penalties (`fail_rep_penalty`). Console: `quest_dump_def <id>`,
`quest_request`, `quest_reward_debug 1`.

The Creation Kit writes every quest into **`scripts/rpg/quests.txt`** (root `"Quests"`, same per-quest
format, wins over a dialog file's block) with the logic additions below.

### Creation Kit logic (game/server/hl2rpm_logic.cpp)

One language for conditions and actions everywhere:
```
"conditions" { "c" { "type" "quest_stage" "quest" "rats" "op" ">=" "value" "1" }
               "c" { "type" "flag" "name" "door_open" "not" "1" "or" "1" }     // or: either this or the next
               "c" { "type" "skill" "name" "speech" "value" "50" } }
"actions"    { "a" { "type" "give" "item" "rpm_credits" "count" "20" }
               "a" { "type" "message" "text" "Done" "text_russian" "Готово" "delay" "2" } }
```
- conditions: `quest_state` (quest, value active|completed|failed|inactive), `quest_stage`, `objective`
  (quest, obj), `flag`, `var`, `item`, `skill`/`stat`/`level`, `rep`, `time` (from, to), `day`, `weather`,
  `map`, `zone`, `near` (target, value), `alive`/`hostile`/`talked` (target), `health`, `suit`, `event`, `chance`.
- actions: `give`/`take` (item, count), `money`, `xp`, `flag` (name, value), `var` (op set|add|sub),
  `quest_start`/`quest_stage`/`objective`/`quest_complete`/`quest_fail`, `rep`, `hostile` (target; empty =
  the dialog NPC), `faction_hostile`, `message`, `sound`, `teleport`, `dialog`, `trade`, `kill`, `remove`,
  `fade`, `time`, `weather`, `skill`/`stat`, `fire` (target input value), `command`. hostile / teleport /
  trade / dialog / kill wait until the dialog window closes (client `dialog_closed`).
- dialog option: `conditions` (+ `show_unmet 1` = greyed), `check { "skill" "speech" "value" "50" }` +
  `fail_next`, `check_once` (default 1), `fail_actions`, `actions`; node: `actions` (when the line shows).
- quest stage: `objectives { "o1" { "type" "kill|collect|talk|go|use|cond|custom" "text" .. "target" ..
  "item" .. "count" .. "radius" .. "optional" "1" "marker" "0" "conditions" {..} } }`, `actions`, `next`,
  `final`; quest: `xp`, `on_complete`, `on_fail`, `fail_conditions`. All required objectives done → next
  stage / completed. HUD markers over the targets, objectives in the journal (`rpm_journal`).
- `scripts/rpg/events.txt`: `"Events" { "<id>" { "on" "zone_enter" "target" "z1" "once" "1" "delay" "0"
  "map" "" "conditions" {..} "actions" {..} } }`; `on`: map_enter, zone_enter, zone_leave, kill, use, talk,
  hour (value), quest_start/quest_stage (value)/quest_complete/quest_fail (target = quest), when.
- `scripts/rpg/globals.txt`: `"Globals" { "flags" { "name" "0|1" } "vars" { "name" "N" } }` (new game).
- map layer ref: `"conditions"` = presence; ref `"type" "zone"` (`size`, rotated by `angles` yaw).
- tests: `rpm_test_state scripts/rpg/test_state.txt` (`"TestState" { quests { id { state stage } } flags
  vars skills stats items level rep hour weather }`), `rpm_logic_debug 1|2`, `rpm_events_dump`,
  `rpm_skill <id> <n>`, `quest_grant`, `rpm_flag`, `rpm_var`, `rpm_state_dump`.

### The RPG character (game/server/hl2rpm_char.cpp)

- `scripts/rpg/rpg.txt`: `"RPG" { "rules" { "stat_total" "40" "tags" "3" "tag_bonus" "15" "points_base" "10" "perk_every" "2"
  "hp_base" "50" "hp_per_end" "10" "carry_base" "25" "carry_per_str" "3" ... } "stats" { "str" { "name" .. } }
  "skills" { "guns" { "stat" "agi" "name" .. } } "perks" { "<id>" { "name" .. "desc" .. "level" "2" "ranks" "1"
  "req" { "int" "5" "medicine" "20" } "effects" { "e" { "type" "skill" "id" "medicine" "value" "10" } } } }
  "traits" { "<id>" { "name" .. "effects" {..} } } }`. Effect types: stat, skill, skills, hp, carry, damage (id =
  guns|energy|melee|explosives or empty), crit, heal, xp, price, points, dr.
- `scripts/rpg/newgame.txt`: `"NewGame" { "map" "ck_test" "spawn" "x y z" "yaw" "0" "create" "1" "name" "Гражданин"
  "actions" { "a" { "type" "give" ... } } }` — applied once on a new game.
- Conditions `perk` (name), `created`, `rep` (faction, op, value); actions `perk` (name), `char_create`, `rep` (faction, value).
- Console: `rpm_char_dump`, `rpm_char_debug 1`, `rpm_xp <n>`, `rpm_perk <id>`, `rpm_rep <faction> <delta>`,
  `rpm_character`, `rpm_char_open create|levelup|sheet`; `rpm_skill <id> <total>` sets a skill's total.

### Sound of the Creation Kit (game/server/hl2rpm_audio.cpp)

- `scripts/rpg/music.txt`: `"Music" { "tracks" { "<id>" { "file" "music/x.mp3" "volume" "0.8" "kind"
  "explore|combat|location|stinger" "time" "any|day|night" "maps" "a;b" "event" "location|quest_start|
  quest_complete|quest_fail" } } }`. Mood: action `music` override > combat (an enemy fights the player) >
  zone music > explore. Actions: `music` (value = track ids, empty = normal), `stinger` (track), `bark`
  (target, value = bark event, default custom).
- NPC barks in `scripts/npcs/<npc>.txt`: `"stock_speech" "all|combat|none"`, `"bark_sets" "a;b"` (empty = sets of
  its class, `-` = none), `"barks" { "<id>" { "event" "greet|idle|farewell|hurt|combat|weapon|ally_death|kill|
  package|custom" "value" ".." "text" .. "text_russian" .. "choreo" "scenes/dialogs/<npc>/<lang>/bark_<id>"
  "sound" "dialogs/<npc>/<lang>/bark_<id>.wav" "weight" "1" "cooldown" "30" "once" "1" "chance" "100"
  "conditions" {..} } }`; shared sets `scripts/rpg/barks.txt`: `"Barks" { "<set>" { "classes" "npc_citizen" "<id>" {..} } }`.
- Location = zone ref with `display_name(_lang)`, `discover_xp`, `music` (track ids), `soundscape` (name in
  `scripts/soundscapes_hl2rpm.txt`, stock soundscape format). Ref `"type" "sound"`: sound, volume, radius, everywhere.
- HL2 lines about Freeman never play (`scripts/hl2rpm_speech_block.txt`, `make_speech_block.py`).
- Console: `rpm_audio_debug 1`, `rpm_music_dump`, `rpm_music_status`, `snd_rpm_music_debug 1`, `rpm_bark <npc> <event>`,
  `rpm_speech_check <npc> <concept> <sound|scene>`.

### Terminals and lockpicking (M6.5: game/server/hl2rpm_terminal.cpp, hl2rpm_lockpick.cpp)

- `scripts/rpg/terminals.txt`: `"Terminals" { "<id>" { "style" "pc|combine" "title(_lang)" .. "header(_lang)" .. "lock_level" "0..100"
  "lock_skill" "science" "password_item" "<item>" "entries" { "<n>" { "title(_lang)" .. "text(_lang)" .. (a record; its "actions"
  run when first read) | "actions" {..} "result(_lang)" .. (a command) | "entries" {..} (a submenu); "conditions" {..} "once" "1" } } } }`.
  Map: layer ref `"type" "terminal"` with `"terminal" "<id>"` and `"model"`, or Hammer `rpm_terminal` (keys terminal, style,
  lock_level, screen_pos/ang/size; outputs OnUsed, OnHacked, OnLockout).
- Actions: `door` (target, op open|close|lock|unlock|toggle), `field` (target, op off|on|toggle), `turret` (target - empty = turrets
  and cameras within `radius`/1500 of the terminal, op off|on|friendly|hostile).
- Locks with a level (`lock_level` of containers, `rpm_lock_level` of doors) start the bobby pin minigame; `bobby_pin` is a built-in
  stackable item. Console: `rpm_terminal_use [name]`, `rpm_terminal_key ...`, `rpm_terminal_reset`, `rpm_lockpick_key ...`, `rpm_lockpick_reset`.

### Scenes (lip sync, gestures)

The editor's layout: `scenes/dialogs/<npc>/<lang>/<node>.vcd` + `sound/dialogs/<npc>/<lang>/<node>.wav`
(older files: `scenes/<npc>/<node>.vcd` + `sound/<npc>/<node>.wav`). Phonemes for lip
sync are inside the WAV (VDAT chunk; the Russian generator writes words in cp1251).
Minimal hand-written VCD (no facial animation):

```
// Choreo version 1
actor "trader_1"
{
  channel "audio"
  {
    event speak "trader_1\0.wav"
    {
      time 0.000000 3.500000
      param "trader_1\0.wav"
      fixedlength
      cctype "cc_master"
      cctoken ""
    }
  }
  channel "look at" { event lookat "Look at player" { time 0.000000 3.500000 param "!player" } }
  channel "face to" { event face "Face player" { time 0.000000 3.500000 param "!player" } }
}
```
Set the end time to the WAV length. Without a scene/sound the line is only text.

## NPC keys in Hammer (every NPC, BaseNPC)

| Key | Meaning |
|---|---|
| `targetname` | dialog key (required for dialogs, quests `kill_targets`) |
| `loot_displayname` | name shown in dialog and loot windows |
| `loot_items` | corpse contents `classname[:amount];classname2` |
| `ftag` | faction tag (shared hate/spoil) |
| `hate_on_dmg` / `hate_f_on_dmg` | player damage makes him / his faction hostile |
| `spoil_on_dmg` / `spoil_f_on_dmg` | player damage ends dialogs (him / faction) |
| `gpc`, `gpc_off_dmg`, `gpc_off_f_dmg` | Gordon pre-criminal override (0 global, 1 on, 2 off) |

Inputs: `HatePlayer`, `SpoilDialog`, `ClearSpoilDialog`, `HateFaction <ftag>`,
`SpoilFaction <ftag>`. Outputs: `OnHatedPlayer`, `OnDialogSpoiled`,
`OnFactionHatedPlayer`, `OnFactionDialogSpoiled`.

Loot box: `func_loot_container` (brush, TOOLS/TOOLSTRIGGER-like volume around a
crate): `loot_displayname`, `loot_items`, `loot_sound_open`, `loot_sound_close`.
Inventory weights: `hl2rpm\scripts\inventory_weights.txt` (`"classname" { "kg" "1.0" }`),
`sv_inventory_weight_max` 40 kg. Console: `inventory_add <classname>`,
`inventory_force`, `inventory_scan_map`.

## Workflow

1. Write/modify the dialog file, then validate:
   `python scripts/dialog_check.py [file.txt] [--tree] [--map <map.vmf|bsp>]`
   (broken node links, unknown quests, bad characters, missing .vcd, NPC missing on the map).
2. Make sure the NPC on the map has the targetname (and a trigger if needed):
   `hl2rpm-hammer` skill, then compile.
3. Test: `run_game.ps1 -Map <map> -Steps "shot|dialog_force trader_1"` — the
   dialog panel is VGUI, so also use `-CaptureAfter` for a window capture, and read
   console.log with `dialog_debug 1`.
4. Russian and English texts: keep both files in sync when the user asks for both.

## Shared topics, theft, NPC character (Creation Kit, 2026-11-01)

- **Shared topics** (`scripts/rpg/topics.txt`, CK "Logic" -> "+ тема"): a question + answer for every project NPC of
  the listed classes / factions. The CK export writes them into those NPCs' dialog files (nodes 9000+, options before
  "Bye", the answer node repeats the option list; keys `"topic" "<id>"` - the importer skips them); an NPC without
  lines gets node 0 = the greeting of the topics + topics + "Bye". `once` = flag `topic_<id>_<npc>`. Not for
  Hammer-only NPCs. Do not hand-edit the generated nodes.
- **Container owner**: key `owner` (NPC targetname or faction) on `prop_loot_container` / `func_loot_container`
  (CK: container page). Taking while the owner / a member of its faction sees the player (view cone + visible,
  < 1200 units; < 150 always) -> bark event `theft`, the witness turns hostile, faction rep -`rep_theft` (5).
  The use prompt shows a red "Steal" + owner.
- **NPC keys** in `scripts/npcs`: `aggression` (0 never first, 1 enemies, 2 all but friends, 3 frenzied),
  `confidence` (0 cowardly - flees at once, 1 cautious < 50 % hp, 2 average < 25 %, 3 brave).
- **Zone soundscapes**: `soundscape_night` (21-6 h), `soundscape_rain` (drizzle / rain / thunderstorm) next to
  `soundscape`; switched by the server once a second.
- Bark events now include `theft`. Debug: `rpm_markers_debug 1` (HUD markers / tracked quest), `rpm_quest_track <id>`.
