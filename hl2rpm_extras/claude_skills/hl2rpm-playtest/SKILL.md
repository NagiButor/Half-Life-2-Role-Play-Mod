---
name: hl2rpm-playtest
description: Launch the HL2RPM mod (hl2.exe, Source 2013 Mapbase) to test and verify changes - load a map, run scripted steps (weather, time of day, console commands, camera), take screenshots, measure FPS, check save/load, capture menus/VGUI windows, read console.log and crash reports. Use after every build of client/server/shader DLLs or map change, and whenever a visual or gameplay claim needs proof.
---

# Testing HL2RPM in the game

The user can't be asked to test each iteration: run the game yourself, look at
the screenshots, read the log. Everything here is automated and cleans up
after itself.

## Before running

- DLLs are built with MSBuild (see CLAUDE.md); PostBuild copies them into
  `E:\Steam\steamapps\sourcemods\hl2rpm\bin`. If a copy failed because the game
  or Hammer++ held the DLL, the test would run the old code: compare the md5 of
  `bin\*.dll` with the build output when in doubt.
- Shaders: `.vcs` must be in `hl2rpm\shaders\fxc`; run
  `python materialsystem\stdshaders\check_shader_combos.py` (0 mismatched) —
  a mismatch crashes the game with "invalid shader combo".
- Only one hl2.exe at a time (the script refuses to start a second).

## Running: `scripts/run_game.ps1`

```powershell
$rg = "E:\SourceModding\HL2RPMSOURCE\.claude\skills\hl2rpm-playtest\scripts\run_game.ps1"
# scripted photo steps on a map (each step waits cl_weather_tour_step = 6 s, then shoots)
& $rg -Map test_deferred -Steps "shot|sv_timecycle_set_time 15; sv_weather clear 0", "shot,sun|sv_weather misty 0"
& $rg -Map demo_map -StepsFile C:\path\steps.txt -TimeoutSec 600
& $rg -Map test_deferred -BuiltinTour                     # the standard 19-step weather tour
& $rg -Map map_v11 -CaptureAfter 45 -CaptureOut C:\tmp\v11.png   # plain load, window capture, close
```

It backs up and restores `cfg\config.cfg`, archives old screenshots to
`%TEMP%\hl2rpm_shots_archive`, deletes its temporary steps file, then prints the
exit code, the new `screenshots\*.jpg` and filtered new `console.log` lines
(`[tour]` lines carry avg/worst frame time and FPS of every step).
Run long tours with a generous `-TimeoutSec` (≈ 6 s × steps + 60 s load).

### Step format (one per line, `<flags>|<console commands>`)

- flags (comma separated, may be empty): `shot` = take a jpeg at the end of the
  step; `sun` = aim the camera at the sun; `spin` = keep turning (temporal
  artifacts); `flash0|flash1|flash2` = a close/mid/far lightning strike right
  before the shot.
- The tour sets `sv_cheats 1; cl_drawhud 0; sv_weather_auto 0; sv_timecycle_set_speed 0`.
  Every step re-applies `cl_weather_tour_angles` ("-14 40 0") unless the step
  contains `setang`. Use `setpos x y z; setang p y r` to frame a spot.
- Useful commands: `sv_timecycle_set_time <h>`, `sv_weather <name> <sec>`
  (clear fair partlycloudy overcast fog drizzle rain thunderstorm misty snow ash),
  `cl_weather_lightning 0|1|2`, `r_weather_debug_view 1|2` (rain exposure / rain
  map), `mat_fullbright 1`, `r_deferred_ssao 0/1`, `r_deferred_dlights 0/1`,
  `impulse 101`, `give <weapon>`, `ent_fire <name> <input>`, `save <n>` / `load <n>`,
  `cl_weather_tour_step <sec>` (put in the first step to slow/speed up the tour).
  New weather presets: `cumulus stratocumulus highclouds`.
- Debug views: `r_weather_cloud_debug 1|2|3` (clouds: sun light only / sky light only /
  opacity), `r_csm_pcss 2|3|4` (soft shadows: blockers found / caster−receiver depth /
  caster depth), `r_csm_color 1` (cascades), `mat_showwatertextures 1` (water
  reflection/refraction targets), `r_deferred_bounce_debug 1` (bounce lights in the log),
  `cl_weather_debug 1` (weather/light values and `[skylut]` rebuilds in the log; the
  rain/wetness/puddle numbers are an overlay at the top right of the jpeg shots).
  Wetness builds up over ~15 s of heavy rain and puddles over a minute — a short test
  shows almost no wet effect. Water in view switches the main view to
  `CAboveWaterViewDeferred` (`mat_showwatertextures 1` shows its reflection/refraction):
  test weather passes with and without water in sight.
- The `[tour]` timing line is printed only for `shot` steps; the numbers drift ±1.5 ms
  between identical steps — alternate A/B/A/B on the same view before concluding.
- Comments: lines starting with `#` or `//`.

### Every frame (flicker, one-frame glitches)

Screenshots land on random frames. To see consecutive frames record a movie with a
fixed timestep and compare frame to frame:
```
"|cl_weather_tour_step 30; ..."                       <- sets the length of the NEXT step
"|cl_weather_tour_step 1; host_framerate 0.016667; startmovie flk jpg"   <- lasts 30 s
"|cl_weather_tour_step 1; endmovie; host_framerate 0"
```
**`cl_weather_tour_step N` takes effect from the next step** — put the recording length
into the step *before* `startmovie`, or the recording lasts as long as the previous step
said (2 s ≈ 13 frames = 0.2 s of game time: periodic events, once a second, are missed).
`flk*.jpg` appear in the mod root (move them to the scratchpad): ~6 fps while
recording at 960x540 (tga), so 100 s ≈ 600 frames = 10 s of game time. Then
`python <hl2rpm_extras>\tools\flicker_stats.py <dir> <out.png>` prints the per-pair
difference and writes a max-difference heat map. Use a lower window size (`-Width 960 -Height 540`).

**One-frame flashes while moving** ("acne for a millisecond while walking"): record with
`+forward`, then `FLAT=1 python scripts/flash_detect.py <dir> flk <out_prefix> 8 4 [first last]`:
per 8x8 tile it compares frame i with the mean of i-1 and i+1 and counts tiles where
`|F_i - avg| > thr` and `> 2 x |F_{i+1} - F_{i-1}|` (smooth motion cancels, a one-frame event
doesn't); `FLAT=1` keeps only tiles without edges in all 3 frames (lighting changes, not
geometry/texture moving - thin lines and texture detail always "flash" a little at tile level),
prints the worst frames and writes `<out>_heat.png` + `<out>_ev*.png` (frame triples).
Cut the range to the steady walk: hitting a wall or climbing a step is non-linear motion and
floods the count. Known real one: the rain map re-rendered (every second while walking)
read through the previous matrix for one frame - `r_deferred_rainmap_debug_late 1` brings that
order back for A/B (a 1400-tile flash at the re-render; 0 with the fix). Look at the sun shadow
term alone with `r_csm_pcss 5; r_deferred_debug_lighting_only 1` (white albedo, only the
shadow): large lit surfaces must stay at 1; `r_csm_pcss 6` / `7` = sky visibility / SSAO alone
(with 6, `r_deferred_skyvis_debug 1/2/3` = open part in the open / under a cover / how covered),
`8` = what the G-buffer holds (red 3D skybox, green map, blue first-person body; pixels with
neither are not in the G-buffer at all - their composite reads the light of what is behind).
Turning the camera hides one-frame glitches and aliasing behind the engine's motion blur
(`mat_motion_blur_enabled`, turning only) — test walking, not spinning.
Real-time (no `host_framerate`) needs a PrintWindow capture loop (~28 captures/s).

For things that move on purpose (shadows with `sv_timecycle_set_speed 30`, drifting
clouds) use `<hl2rpm_extras>\tools\crawl_stats.py <dir> <prefix> <out.png>`: the second
difference ignores smooth motion and shows only flicker (heat map). Record `tga`, not
`jpg` (`startmovie flk_s tga`): jpeg blocks flicker on their own at sharp edges.
Compare A/B in the same run (e.g. `r_csm_pcss 1` then `0`, `r_weather_cloud_temporal 0/1`,
`r_weather_cloud_wind_scale 0` to freeze the clouds) and look at a 3-4x zoom of
consecutive frames, not only the numbers.

### Old / stock maps

EP1/EP2 maps (`E:\SourceModding\UsefulStuff\Source sdk 2013 HL2 unpacked vpk\episodic|ep2\maps`)
run in the mod (their content is mounted): copy one into `hl2rpm\maps` to test the
automatic conversion (sky, weather, lights) or open-world performance (`ep2_outland_07`),
and delete it with its `maps\graphs\*.ain` afterwards. HL2 maps are not available.

### Menus and VGUI windows: `scripts/ui_clicks.ps1`

Mouse/keyboard driven UI tests with a window capture after each `shot`:
```powershell
& "...\scripts\ui_clicks.ps1" -OutDir C:\tmp\ui -Actions "wait:22", "click:141,557", "wait:3", "click:1010,320", "wait:2", "shot"
& "...\scripts\ui_clicks.ps1" -Map test_deferred -Actions "wait:40", "key:F1", "wait:2", "shot"
```
Coordinates are window coordinates of the captures (1600x900 window + title bar;
main menu "НАСТРОЙКИ" ≈ 141,557, Options tab "Графика" ≈ 1010,320). Actions:
`wait:s`, `click:x,y`, `rclick:x,y`, `key:NAME` (F1, ESCAPE, RETURN, TAB, OEM_3...), `type:text`, `shot`.
A dialog opened by a console command (`gamemenucommand`) appears off screen — click through the menu instead.
Injected keys can't open the developer console (`key:OEM_3` does nothing), and typed
text then goes to the game as key presses. To test "a command typed with the console
open", use the pause menu instead (it makes GameUI visible the same way):
`run_game.ps1 -Steps "|cl_weather_tour_step 5; gameui_activate", "|cl_weather_tour_step 5; <command>", "|cl_weather_tour_step 60; gameui_hide", "|cl_weather_tour_step 60" -CaptureAfter 48 -CaptureOut <png>`
(the capture happens only while the game still runs - keep the last steps long).

### Looking at results

```
python scripts/shots.py sheet C:\tmp\sheet.jpg --cols 3 --labels "clear;rain;night"
python scripts/shots.py probe <jpg> 100,50 800,60        # exact RGB (e.g. sky color checks)
python scripts/shots.py crop  <jpg> 500,0,1300,500 C:\tmp\crop.jpg
python scripts/shots.py diff  <a.jpg> <b.jpg> C:\tmp\diff.png
```
Then Read the sheet/crop image. Always compare against a known-good reference
(e.g. the same step before the change) — a single shot rarely proves anything.
Use the scratchpad for sheets/crops; never leave test files in the mod folder.

## Things that bite

- **Archived convars**: `+convar value` on the command line (and `convar value`
  typed in a test) for FCVAR_ARCHIVE convars end up in `config.cfg`. run_game.ps1
  restores the file; if you launch the game any other way, back it up yourself.
- **Exit crash**: with the deferred renderer the game crashes on quit
  (0xC0000005 / 0xC0000374 in filesystem_stdio). Known, pre-existing; not a
  regression. A crash *before* the last step is a real problem.
- **Crash reports**: `hl2rpm\crashes\crash_*.txt/.dmp/.analysis.txt` (VEH handler,
  `rpm_crash_handler 1`), Windows dumps in `%LOCALAPPDATA%\CrashDumps`.
  The analyzer opens Notepad — never kill notepad.exe (user tabs).
- `jpeg` screenshots of menus / VGUI without a map are black: use
  `-CaptureAfter N` (PrintWindow of the game window, DPI-aware).
- map_v11 starts on a black scripted intro; give it time or `setpos` away.
- The map starts in a random automatic weather (often `fog` in the morning): the first
  shots after `sv_weather <name> 0` can still show the client blending out of it (a white
  haze over everything). Give the first step 10-15 s or drop the first shot.
- `fog_override 1` takes `fog_color` (default "-1 -1 -1") for the *main* view too: the
  map fog turns black. For fog A/B use `r_weather_fog 0`, `r_3dsky 0`, or set `fog_color`.
- NPCs react to the player: the flashlight in Alyx's face makes her shield her eyes,
  `ai_disable 1` freezes them (no blinking). `r_flex 0` / `r_eyes 0` isolate face problems;
  `-Extra "-nodeferred"` renders the same view with the stock renderer for comparison.
- A map compiled without vrad looks fullbright in forward materials — check with
  `bsp.py info` (hl2rpm-hammer skill).
- Timing: the first frames after `map` are loading; the tour waits
  `cl_weather_tour_delay` (4 s) + first step. Precipitation and clouds need
  a few seconds after `sv_weather <name> 0` to fill in.

## State dumps (console / console.log)

- `sv_world_status` — map, time of day, weather (and the world state carried over changelevels)
- `inventory_dump` — items, weight, suit; `quest_dump` — quests and reputation
- cheats for tests: `quest_grant <id>` (after `dialog_reload`), `quest_rep <delta>`,
  `inventory_add <classname>`
- `report_entities` — entity count per class (duplicates after transitions, leaks)
Use `-LogFilter "\[world\]|\[inventory\]|\[quests\]|Class: light_"` to see them.

## Save / load checks

1. `-Steps "|sv_weather rain 0; sv_timecycle_set_time 20; inventory_add item_healthkit", "|inventory_dump; quest_dump; save t1"`
2. Load in a fresh process: put `wait 200`, `sv_world_status`, `inventory_dump` into a
   temporary `cfg\_x.cfg` and run `-NoMap -Extra "+load t1 +exec _x.cfg" -CaptureAfter 70`
   (commands given directly after `+load` run before the level exists).
3. Delete `save\t1.*`, `save\autosave*` and the temporary cfg afterwards.

## Level transitions (changelevel continuity)

Console `changelevel <map>` is a *new game* load (everything resets). Real
transitions are `changelevel2 <map> <landmark>` (what trigger_changelevel runs)
and need an `info_landmark` of that name in both maps **and** a
`trigger_changelevel` back in the target map ("Can't find connection" otherwise).
Test maps with both: `hl2rpm-hammer` skill, `scripts/new_test_map.py --landmark lm --changelevel <other>`.
Steps: `"|sv_weather snow 0", "|changelevel2 map_b lm", "shot|sv_world_status; report_entities"` —
the tour pauses during the load and continues on the new map. Transitions also
write `save\autosave*.sav` — delete them afterwards.

## Performance

`[tour] step N "...": X ms avg (Y fps), worst Z ms` per step. The user's GPU is
a GTX 960M laptop (budget: ≥ 60 fps at 1600x900 on the "High" preset). Compare
before/after on the same steps; a single number is noise ± 3 %.
