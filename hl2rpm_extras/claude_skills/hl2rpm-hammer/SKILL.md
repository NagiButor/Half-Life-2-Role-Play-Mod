---
name: hl2rpm-hammer
description: Edit, create, compile and inspect HL2RPM maps (Source 2013 Mapbase, Hammer++). Use for any work on .vmf/.bsp files - adding or changing entities, brushes, lights, outputs, dialog NPCs on a map, recompiling (vbsp/vvis/vrad), checking what a compiled map contains, or opening a map in Hammer++ for the user.
---

# HL2RPM maps: VMF editing, compiling, BSP inspection

Hammer++ is a GUI program; it cannot be driven from here. Maps are edited as
text (VMF) with `scripts/vmf.py`, compiled with `scripts/compile_map.ps1`
(the same SDK tools Hammer++ runs) and inspected with `scripts/bsp.py`.
Hammer++ is only launched to show a result to the user.

## Where things are

| What | Path |
|---|---|
| Map sources (`.vmf`; `.vmx` = Hammer's autosave copy, never edit it) | `E:\XBLAH's modding tool\content\Mapbase\hl2rpm\mapsrc` |
| Compiled maps the game loads | `E:\Steam\steamapps\sourcemods\hl2rpm\maps` |
| Compile tools (stock SDK 2013 SP) | `E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer\bin\{vbsp,vvis,vrad}.exe` |
| Hammer++ | `...\Source SDK Base 2013 Singleplayer\bin\hammerplusplus.exe` (config `bin\hammerplusplus\hammerplusplus_gameconfig.txt`) |
| FGDs (entity definitions, custom keys) | `E:\XBLAH's modding tool\tools\FGDs\Mapbase\` — `base_vizzys.fgd` (BaseNPC with dialog/loot/faction keys), `halflife2_vizzys.fgd`, `hl2rpm.fgd`, `deferred.fgd` |
| BSP backups made by compile_map.ps1 | `E:\SourceModding\HL2RPMSOURCE\map_backups` |

Test maps with full permission to change: `test_deferred`, `demo_map`, `map_v11`.
Any other map: ask before changing it.

## Rules

1. **Hammer must not have the map open** while its VMF is edited here — Hammer's
   next save would silently overwrite the edit. Check first:
   `tasklist /FI "IMAGENAME eq hammerplusplus.exe"`. If it runs, ask the user to
   save and close the map (or close Hammer).
2. `vmf.py` keeps a timestamped `*.vmf.bak_*` before every save and writes the
   file byte-identical except for the edits (`vmf.py check` proves the round trip).
3. After editing: compile, copy to the mod (the script does both), then load the
   map in the game with the `hl2rpm-playtest` skill and look at it.
4. Map names/paths with spaces: always quote (`XBLAH's` contains an apostrophe —
   in bash use double quotes, in PowerShell single quotes don't work around it).

## Editing a VMF

CLI (`python scripts/vmf.py <cmd> <map.vmf> ...`):

```
info   <vmf>                               counts per class, bounds, skyname
ents   <vmf> [--class C] [--name N] [--keys]
get    <vmf> --id N                        print a block
set    <vmf> --id N key=value ...          "key=" removes the key
add    <vmf> <classname> x y z key=value   point entity
output <vmf> --id N OnTrigger "target,Input,param,delay,times"
delete <vmf> --id N
box    <vmf> x1 y1 z1 x2 y2 z2 MATERIAL [--entity-id N]   axis-aligned brush (world or brush entity)
check  <vmf>                               round-trip test
```

Library (bigger edits: write a small Python script):

```python
import sys; sys.path.insert(0, r"E:\SourceModding\HL2RPMSOURCE\.claude\skills\hl2rpm-hammer\scripts")
from vmf import VMF
m = VMF.load(r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\mapsrc\test_deferred.vmf")
npc = m.add_entity("npc_citizen", (0, 256, 0), targetname="trader_1", angles="0 180 0",
                   additionalequipment="weapon_pistol", loot_displayname="Trader")
trig = m.add_brush_entity("trigger_dialog_start",
                          [m.make_box((-64, 128, 0), (64, 384, 128), "TOOLS/TOOLSTRIGGER")],
                          npc_name="trader_1", trigger_once="1", spawnflags="1")
m.add_output(trig, "OnStartTouch", "trader_1", "StartScripting")
m.save()
```

`Node` API: `get/set/add/delete/all(key)`, `child(name)`, `children_named(name)`,
`add_child/remove_child`, `.items` keeps keys and blocks in file order.
`VMF`: `world`, `entities(classname, targetname)`, `find_id(id)`, `solids()`,
`add_entity`, `add_brush_entity`, `make_box`, `add_box`, `add_output`, `delete`, `bounds()`.

### Source conventions that matter

- Units: 1 unit ≈ 1.9 cm, z up. Player hull 32×32×72 (crouch 36), eye 64.
  Doorway ≥ 56×112, step ≤ 18, ramps ≤ 45°. World limit ±16384.
- Brushes: every map must be sealed from the void by world brushes (sky with
  `TOOLS/TOOLSSKYBOX`), otherwise vbsp reports a leak and vvis/vrad will not run
  properly (the compile script stops). Non-sealing detail geometry goes into
  `func_detail`. Invisible faces: `TOOLS/TOOLSNODRAW`; triggers: `TOOLS/TOOLSTRIGGER`;
  player clip `TOOLS/TOOLSPLAYERCLIP`.
- Texture scale 0.25 is the norm (`uaxis "[1 0 0 0] 0.25"`), lightmapscale 16
  (smaller = sharper baked shadows, costlier).
- Keys starting with `_` (`_light`, `_cone`, `_inner_cone`, `_quadratic_attn`...)
  are compile-time keys read by vrad.
- `_light` = "R G B brightness". Named lights (targetname) become switchable
  (own lightstyle, TurnOn/TurnOff) — needed for the deferred conversion to keep I/O.
- Outputs: `"OnTrigger" "target,Input,param,delay,times"` (times -1 = forever).
  Hammer++ may use the ESC character (`\x1b`) instead of commas; `add_output`
  follows whatever the file already uses.

### HL2RPM specifics

- Deferred renderer: on load the server converts classic `light`/`light_spot` and
  texture lights (worldlights) into deferred lights and creates a global light
  from `light_environment`; `env_weather` and the procedural sky
  (`sky_proc_atmo_01`) appear automatically. Maps with any `light_deferred`
  keep their own deferred lights (no conversion).
- Custom entities (see FGDs): `light_deferred`, `light_deferred_spot`,
  `light_deferred_global`, `env_weather` (start_weather 0..10, auto_weather),
  `env_timecycle`, `trigger_dialog_start`, `func_loot_container`,
  `item_combine_soldier_suit`, `weapon_xm1014`. NPC dialog/loot/faction keys:
  see the `hl2rpm-dialogs` skill.

### Throwaway test maps

`python scripts/new_test_map.py <out.vmf> [--size 1024] [--height 512] [--landmark NAME] [--lamp] [--indoor] [--changelevel MAP]`
builds a sealed box (sky ceiling or `--indoor` roof), sun, spawn, pillar, optional
landmark / named lamp / trigger_changelevel. Keep such VMFs in the scratchpad,
compile with `-Vmf`, and delete the BSPs from `hl2rpm\maps` after the test.

## Compiling

PowerShell: `& "E:\SourceModding\HL2RPMSOURCE\.claude\skills\hl2rpm-hammer\scripts\compile_map.ps1" -Map test_deferred [-Profile quick|normal|final]`

- `quick`: no vis, `vrad -fast` — layout/entity iteration (seconds).
- `normal` (default): `vvis -fast`, `vrad -both` (LDR+HDR).
- `final`: full vis, `vrad -final -StaticPropLighting -StaticPropPolys -TextureShadows`.
- Extra switches: `-VbspArgs`, `-VvisArgs`, `-RadArgs`, `-Vmf <any path>`, `-NoCopy`, `-AllowLeak`.
- A leak stops the script; the pointfile is `<map>.lin` next to the VMF.
- Full log: `<map>.log` next to the VMF. Big maps (map_v11) take many minutes with
  full vis — run such compiles in the background.
- A map compiled without vrad has no lighting data (fullbright forward
  materials, `bsp.py info` shows 0 worldlights) — always run vrad.

## Inspecting / patching a compiled BSP

```
python scripts/bsp.py info   map.bsp          # entities per class, worldlights LDR/HDR, skyname
python scripts/bsp.py ents   map.bsp --class light_environment --keys
python scripts/bsp.py lights map.bsp          # compiled lights (type, style, intensity, falloff)
python scripts/bsp.py setkv  map.bsp --name lamp1 "_light=255 200 150 300"
python scripts/bsp.py export map.bsp ents.txt ; ...edit... ; python scripts/bsp.py import map.bsp ents.txt
```
Entity-lump edits are for keyvalues/outputs of existing point entities only;
geometry, brush entities and lighting need a recompile.

## Showing a map to the user in Hammer++

`Start-Process "E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer\bin\hammerplusplus.exe" -ArgumentList '"<full path to .vmf>"'`
(only when the user asks; while it is open, do not edit that VMF).
