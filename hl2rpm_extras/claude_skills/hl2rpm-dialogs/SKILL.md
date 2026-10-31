---
name: hl2rpm-dialogs
description: Work with the HL2RPM RPG systems - NPC dialogs (scripts/dialogs/*.txt), quests, reputation, factions, loot (NPC corpses, func_loot_container, prop_loot_container), inventory, the HL2RPM Creation Kit world layer (scripts/npcs, scripts/world - NPCs placed without Hammer, markers, AI packages / daily routine, nav nodes) and their Hammer entities/keys (trigger_dialog_start, ftag, spoil_on_dmg, loot_items...). Use to write or fix a dialog, quest or NPC routine, place a talking NPC on a map, validate dialog files, or debug why a dialog/quest/loot/routine does not work.
---

# HL2RPM dialogs, quests, loot

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
AI packages, factions and nav nodes - see "World layer" below.
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
(game folder = `hl2rpm\testmaps`). Screenshot mode for reports (captures the screen - window grab() misses the 3D labels):
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
  `anim`, `owner`), `item` (`classname`), `container` (`model`, `loot_items`, `loot_displayname[_<lang>]`),
  `prop` (`model`, `physics`), `path` (`name`, `points { "0" { "origin" "wait" "yaw" "anim" } }`), `entity`
  (`classname`, `keyvalues`). Spawned once on a new map (never on a loaded save / revisited level).
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
