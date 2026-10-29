# Half-Life 2: Role-Playing Mod (HL2RPM)

**English** · [Русский](README.ru.md)

A single-player role-playing game set in City 17 during the Combine occupation, in the spirit of
*Fallout: New Vegas*: you are not Gordon Freeman but a newly arrived citizen with no memory, a
temporary pass and a note in your pocket — *"Remember: you are not from here."* Talk, lie, bribe,
comply or resist; the city remembers what you did.

Built on **Source SDK 2013 / Mapbase v8.0** (singleplayer, `episodic`, Win32) with a custom deferred
renderer, procedural sky, volumetric clouds, dynamic weather and a full day–night cycle.
The game is fully localized in **English and Russian** (follows the Steam language).

This repository holds the source of the game DLLs and shaders (`sp/src` of the SDK) plus the mod's
tools, content sources, design document, FGD files and localization.

> Compiled DLLs, maps, models and other game files are not stored in git — they are published as
> archives in [Releases](../../releases).

## Vertical slice: "Prologue: Arrival"

| Map | What is there |
|---|---|
| `rp_c17_01_trainstation` | the dream, the train into City 17, the platform check, the registration hall (window 4, the citizen card), the medical point, the luggage room, the Civil Protection checkpoint |
| `rp_c17_02_plaza` | the station plaza with the Breen screen, the ration dispenser yard, the Labor Bureau annex, posts and patrols, a kiosk trader |
| `rp_c17_03_block7` | Block 7, house 7/3: neighbours, TVs with Breencasts, your room 11 with a bed (sleep, save point) |
| `rp_c17_08_opora` | the "Opora" Civil Protection precinct (former school No. 3): gym and range, barracks, cells, the duty desk |
| `rp_c17_04_oldtown` | the Old Town: Klyuchevaya street, Lukich's watch workshop with the Lambda safehouse in its cellar, Kadar's bathhouse, the flea market, the canal embankment with a Civil Protection raid on the bridge, "Lock 3" |
| `background_c17` | main menu background: the plaza at dusk, the street lamps come on |

Quests of the slice: MQ-00 *Arrival*, MQ-01 *Temporary Pass*, MQ-02 *A Question of Papers*, SQ-01 *Pick Up That Can*,
GO-01 *Volunteers* (join Civil Protection: tests and a loyalty check at "Opora"),
LM-01 *Sign of the Lambda* (a samizdat run through a raid to "Lock 3"), SM-01 *The Bathhouse* (a parcel for Kadar's black market).
Start from **New Game → Prologue: Arrival** or `map rp_c17_01_trainstation`.

## Features

### Role-playing systems

- **Character** — six attributes Λ.A.M.B.D.A. (34 points), 13 skills (tag 3), 12 traits (up to 2),
  levels, experience, perks; health and carry weight follow the attributes.
- **Citizen card** — character creation as a registration form filled in at window 4, with the clerk
  commenting on your traits.
- **Dialogs** — branching conversations with skill and attribute checks (threshold visible,
  green / red), lies, bribes, items, reputation and civil-status conditions; keys 1–9, Esc to leave.
- **Civil status and suspicion** — your standing with the Combine; suspicion grows in control zones
  (running near Civil Protection, a weapon in hand, restricted areas, failed lies) and decays out of
  sight. At 100 Civil Protection turns hostile.
- **Factions and relations** — ten factions plus personal disposition of every named NPC.
- **Quests** — stages, objectives with markers on the compass, journal and notes.
- **Inventory, loot, trade** — items with weight, searching bodies and containers; traders with their own
  till and stock (kept in the save), prices by Barter skill and faction standing, haggling in dialogs.
- **Sleep** — beds you own: choose the hours, suspicion resets, curfew is checked.
- **Persistent world** — character, quests, flags, time and weather survive saves and map transitions.

### Interface

- **HUD** that changes with your clothing: citizen bracelet (health, civil status, credits),
  Civil Protection mask, Overwatch visor, rebel and HEV themes.
- **Compass** with the tracked quest marker, distance and up/down arrow; **suspicion eye**;
  quest tracker; notification feed; banners for level-ups, locations and quests.
- **Use hint** under the crosshair — `[E] Talk`, `Take`, `Sleep`, `Read`, `Use`.
- **Registry bracelet** (TAB / I) — *Citizen* (attributes, skills, perks, traits),
  *Belongings* (inventory by category, weight, use / drop / read), *Data* (quests, notes,
  relations), *Rations*, *Radio* (Breencasts).
- **Trade screen** — the trader's goods and your belongings side by side, prices, the trader's till.
- **Reader** for notes, documents and terminals; **sleep** screen.

### Big world

The Source engine cannot stream BSP geometry and limits a map to 2048 networked entities, so the world is
built like *New Vegas* cells: sectors joined by seamless transitions, interiors as separate maps, and
**population streaming** — map entities marked `rpg_stream 1` (citizens, patrols, crates) are not created
with the map; they appear when the player comes near and go away out of sight, keeping their state in
the save. A test field with 2600 streamed entities runs with ~350 of them alive at a time.

### Deferred renderer

On top of Mapbase there is a deferred renderer (light pre-pass, based on Biohazard90's deferred):

- **Sun shadows** — 2–4 cascades fitted to the view, 24-bit depth, cubic B-spline filtering, PCSS
  soft penumbra, screen-space contact shadows.
- **Lamps** — point and spot lights with shadows and a shadow budget; the map's classic lights are
  converted on load, **street and indoor lamps switch on at dusk** and off at dawn; muzzle flashes,
  fire and explosions light the scene.
- **Global illumination** — a probe grid around the camera with multiple bounces: rooms are dark,
  light from windows and doorways spreads softly.
- **SSAO and sky visibility**, **PBR materials** (roughness, metalness, specular, cavity),
  **parallax occlusion mapping** built from the stock normal maps and textures, **HD textures**.
- **Water** — reflection and refraction, per-type absorption and colour, foam, caustics; mirrors and
  reflective glass; transparent glass with Fresnel reflections; decals with PBR (blood, bullet holes).
- **Anti-aliasing** — TAA (default) or FXAA.
- **Post-processing** — eye adaptation, bloom with lens dirt, sun flare and glare, colour grading,
  vignette, grain, chromatic aberration, sharpening, depth of field (zoom and dialogs).
- **Wind** — trees and grass sway; light through foliage; soft particles; dust motes in sunbeams.

### Weather, sky, time of day

- **Procedural sky** (Hillaire atmosphere LUTs), sun, moon and stars, dusk and night lighting.
- **Volumetric clouds** with temporal accumulation and cloud shadows on the ground.
- **14 weather presets** from clear to thunderstorm, fog, drizzle, snow, ash; smooth transitions and
  automatic weather that depends on the time of day.
- **Precipitation** — GPU rain, snow and ash with a roof map (dry under cover), splashes where drops
  land; **wet surfaces and puddles** with screen-space reflections; height fog; sun shafts.
- **Thunderstorms** — lightning at three distances, thunder delayed by distance.

### Tools

- **Options → Graphics / Effects** — quality presets and individual switches.
- **F1 editor** — time of day and weather; light editor (`r_deferred_light_editor_toggle`).
- **Crash debugger** — minidump, text report and automatic `cdb` analysis (`hl2rpm\crashes`).
- **Photo tour** — scripted runs through weather presets with screenshots and frame times.
- **Content pipeline** — dialogs, quests and strings are written in RU + EN text files and built
  into game scripts and localization; the same tool writes the voice-over scripts for recording.

## Installing

1. Steam: **Source SDK Base 2013 Singleplayer** (appid 243730), Half-Life 2, Episode One, Episode Two.
2. In `steamapps\sourcemods`: Mapbase (`mapbase_shared`, `mapbase_hl2`, `mapbase_episodic`).
3. Unpack `hl2rpm-vX.Y.zip` from Releases into `steamapps\sourcemods` (the archive already contains
   the `hl2rpm` folder) and restart Steam. The mod appears in the library as **Half-Life 2: Role-Playing Mod**.

Map sources (`.vmf`) and Hammer++ FGD files are in the `hl2rpm-mapsrc-*.zip` and `hl2rpm-fgds-*.zip`
archives of the same release; the FGD load order is described in `CLAUDE.md`.

### Controls

| Key | Action |
|---|---|
| E | use / talk / take / read / sleep |
| TAB or I | registry bracelet (character, belongings, quests, notes, radio) |
| 1–9, Esc | dialog answers, leave the dialog |
| Esc | close a screen (the bracelet, a note) |

## Repository layout

| Path | Contents |
|---|---|
| `game/client`, `game/server`, `game/shared` | game code (client.dll / server.dll) |
| `game/*/hl2rpm` | RPG systems: character, dialogs, quests, entities, use hints, HUD and screens |
| `game/client/deferred`, `game/client/weather` | deferred renderer and weather (client) |
| `game/shared/weather`, `game/shared/timecycle` | weather presets, `env_weather`, the world clock |
| `materialsystem/stdshaders` | shaders: `.fxc`, C++ wrappers, `game_shader_dx9.dll` |
| `game/ShaderCompile235` | shader compiler (no perl and no DirectX SDK needed) |
| `Дизайн-документ/` | the design document (Russian): world canon, rules, quests, full dialog script, voice-over |
| `hl2rpm_extras/content` | dialog, quest and string sources (RU + EN) → `tools/build_content.py` |
| `hl2rpm_extras/docs/RPG_SPEC.md` | how the RPG systems and data formats are implemented |
| `hl2rpm_extras/tools` | map generators for the slice (`prep_rp_c17_*.py`, `phaseb_*.py`), content and texture tools |
| `hl2rpm_extras/art` | Steam store and library art, menu logo (generated from in-game frames) |
| `hl2rpm_extras/` | copies of FGD, crashtools, localization, Claude Code skills — see its [README](hl2rpm_extras/README.md) |
| `CLAUDE.md` | detailed technical notes (Russian): architecture, console variables, pitfalls found |

## Building

Visual Studio 2022 (toolset v143, platform Win32). The `.vcxproj` files are edited by hand —
**do not run VPC** (`create*projects.bat` would overwrite the changes).

```powershell
$msb = "<VS2022>\MSBuild\Current\Bin\MSBuild.exe"
& $msb "game\client\client_episodic.vcxproj" /p:Configuration=Release /p:Platform=Win32 /m
& $msb "game\server\server_episodic.vcxproj" /p:Configuration=Release /p:Platform=Win32 /m
& $msb "materialsystem\stdshaders\game_shader_dx9_episodic.vcxproj" /p:Configuration=Release /p:Platform=Win32 /m
```

PostBuild copies the DLLs and PDBs into `<Steam>\steamapps\sourcemods\hl2rpm\bin`.

Shaders are compiled **only** with `ShaderCompile235` and only the ones the mod uses (lists:
`materialsystem/stdshaders/hl2rpm_custom.txt`, `deferred_shaders.txt`):

```powershell
cd materialsystem\stdshaders
..\..\game\ShaderCompile235\bin\ShaderCompile.exe -ver 30 -shaderpath . lightingpass_global_ps30.fxc
python check_shader_combos.py   # .vcs and C++ must agree: 0 mismatched
```

Content (dialogs, quests, strings, voice-over scripts):

```powershell
python hl2rpm_extras\tools\build_content.py     # writes scripts/ and resource/ of the mod
```

Slice maps are generated from BSPSource decompiles of the retail maps and compiled with the
`hl2rpm-hammer` skill scripts (`compile_map.ps1 -Map rp_c17_01_trainstation -Profile final`).

## Main console variables

| Variable | Purpose |
|---|---|
| `r_csm_quality` 0..5 | sun shadow quality |
| `r_deferred_aa` 0 / 1 / 2 | anti-aliasing: off / FXAA / TAA |
| `r_deferred_gi`, `r_deferred_ssao` | global illumination, SSAO |
| `r_deferred_pbr`, `r_deferred_pom` | PBR materials, parallax occlusion |
| `r_hl2rpm_post`, `r_hl2rpm_bloom`, `r_hl2rpm_dof` | post-processing |
| `sv_weather <name> [sec]`, `sv_weather_auto`, `sv_weather_list` | weather |
| `sv_timecycle_set_time <hours>`, `sv_timecycle_speed_scale` | time of day and its speed |
| `cl_rpg_hud`, `cl_rpg_hud_compass` | RPG HUD and compass |
| `rpg_bracelet [tab]` | open the bracelet |
| `rpg_stream_info`, `rpg_stream_radius` | population streaming: counts, radius |
| `r_deferred_timecycle_editor` | time and weather editor (F1) |

Run without the deferred renderer (stock Mapbase): `-nodeferred`.

## Performance

Reference: a GTX 960M laptop at 1600×900 — an open map with every effect at ~55–60 FPS.
The cost of individual effects and how to measure it is in `CLAUDE.md`.

## Credits

- Valve — Half-Life 2, Source SDK 2013
- [Mapbase](https://github.com/mapbase-source/source-sdk-2013) — the base of the mod
- Biohazard90 — Alien Swarm Deferred, the base of the deferred renderer
- Sébastien Hillaire — the atmosphere model of the procedural sky
- Timothy Lottes — FXAA 3.11
- BSPSource — decompiling the retail maps the slice levels are built on

The SDK code is distributed under the Source 1 SDK license; Half-Life 2 assets belong to Valve.
The mod is a free non-commercial fan project.
