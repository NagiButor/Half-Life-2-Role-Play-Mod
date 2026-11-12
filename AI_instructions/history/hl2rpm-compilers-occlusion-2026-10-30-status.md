---
name: hl2rpm-compilers-occlusion-2026-10-30-status
description: "Own map compilers (hl2rpm_vbsp/vvis/vrad) + open-world occlusion culling of the map itself and lamps, done overnight 2026-10-30 while the user slept; verified in game, NOT committed"
metadata:
  node_type: memory
  type: project
  originSessionId: 37827680-dda4-4057-9a7f-de3ecfff164c
  modified: 2026-10-30T02:12:00.125Z
---

Request (2026-10-30, night): the user removed areaportals/skip and replaced the hugging sky brushes of `rpm_trainstation` with one big sky box (`mapsrc/test/rpm_trainstation_testoptimization.vmf`) — vbsp died with "Too many t-junctions to fix up!". They asked for "a new compiler for the mod" + "advanced occlusion culling, only what the player sees", then went to sleep: "сам мониторь все ошибки... жду готовый результат".

Done (details in CLAUDE.md sections "Компиляторы карт HL2RPM" and "Occlusion culling для открытого мира"):
- Fork of Mapbase vbsp/vvis/vrad in `sp/src/utils`, installed by `hl2rpm_extras/tools/install_compilers.ps1` as `hl2rpm_*.exe` in the SDK bin; Hammer++ gameconfig switched to them (backup `.bak_20261030_043414`); `compile_map.ps1` defaults to them (`-Tools stock` = Valve).
- vbsp: t-junction triangle lists by area within the 65536-index format limit (no more abort), map budget report. vvis: quick vis default (`-full`), `-radius`. vrad: flat lightmaps default (`-full`/`-final` = baked) — 42 s vs ~12 min. Library asserts no longer pop a modal dialog (the user saw one mid-night).
- Client: world leaves hidden by poking engine `mleaf_t.visframe` around BuildWorldLists (validated leaf by leaf); lamps culled leaf by leaf; occluder fixes (face normal sign bug = half the walls were never occluders; t-junction vertices; displacement triangles); `UTIL_GetLevelFileName` (engine truncates long map names to 39 chars).

Measured (GTX 960M): 16 views of the trainstation, culling off / on — with areaportals 23.83 / 23.44 ms avg; without them 25.11 / 24.31 ms; single views up to −6.8 ms (lamps culled leaf by leaf; helps the areaportal map too). World draw ≈ 9.5 ms on both maps = pixel shading, not leaf count. Pixel A/B on/off: noise only.

**Why:** the user wants an open world without manual vis work. **How to apply:** don't promise FPS from world-leaf culling on GPU-bound views; lamps/renderables are where it pays. Not committed — the repo already has unstaged vitals work over a staged revert ([[hl2rpm-vitals-2026-10-30-status]], [[hl2rpm-session-2026-10-28-reverted]]); ask before committing.

Found on the way: map names over 31 chars can't be loaded from a save (engine keeps 31; vbsp now warns) — the user's test map name is 33. Save/load of a short-named copy works with the new culling.

Follow-up the same morning: the user reported "too aggressive culling" at `setpos -2954 -3625 60; setang -5 69 0` with endless "Too many vertex format changes in frame, whole world not rendered". Not the culling (same spam with it off): the engine's 127 mesh changes per static batch, hit by long open-map views. Fixed by `deferred/deferred_worldlists.cpp` (stable sort of the engine's opaque sort groups by `g_WorldStaticMeshes` mesh right after BuildWorldLists; 145 -> 3 changes), verified on 17 views + map_v11 + map change, not committed. The user also asked what of vbsp/vvis/vrad a deferred mod really needs (answered: vbsp must; vvis cheap, little use on open maps; vrad only for worldlights of classic/texture lights + forward/-nodeferred fallback; offered a vrad-free path for light_deferred-only maps).

Left out / ideas: server-side AI efficiency by visibility; auto-removing never-visible faces; finer leaves; static props as occluders; rebuilding mathlib without asserts.

Update 2026-10-30: committed + pushed on the user's request ("закоммить и запушь") - see [[hl2rpm-gi-v3-2026-10-30-status]] for the four commits.
