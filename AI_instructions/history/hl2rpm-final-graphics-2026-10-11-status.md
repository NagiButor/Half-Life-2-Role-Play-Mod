---
name: hl2rpm-final-graphics-2026-10-11-status
description: "Final graphics pass" request of 2026-10-10/11 (6 bugs, PBR, water types, 2 crashes) - all done and verified in game, NOT committed; what was left out and the user's taste notes
metadata:
  type: project
---

The user's request of 2026-10-10 ("крайняя правка по графике, финальный красивый вид", reference: RDR2):
6 visual bugs (GI blotches, sun shafts at roof edges, contact shadow fringe, fog lost at the water
surface, func_brush not blocking GI, GI "loading" on approach), "realistic PBR", stock water types
(murky / clear / sea) instead of "all water is just transparent", crashes on map_v11 and when changing
our graphics options.

State on 2026-10-11: everything implemented, built into the mod folder and checked in game
(weather tour, map_v11 with reload / save / load, options dialog, demo_map in rain, exit code 0,
`check_shader_combos.py` 0 mismatched). Details are in `sp/src/CLAUDE.md` (bullets dated 2026-10-11).
**Nothing is committed or pushed** - the repo also still holds the uncommitted work of the previous
session ([[hl2rpm-bugs-2026-10-10-status]]). Commit / tag / release only when the user asks
([[hl2rpm-release-procedure]]).

Left out on purpose (say so if asked, don't present as done):
- static props still don't occlude the GI probes (only world + resting brush entities);
- PBR parameters are guessed from `$surfaceprop` / names - no per-pixel roughness or metalness maps
  (a material can set `$pbr_roughness` / `$pbr_metalness` / `$pbr_specular` itself; `$mraotexture` is not supported);
- mirror-sharp reflections still come only from baked cubemaps (the "environment" of PBR is the local indirect light);
- `$forcecheap` water is lit by the time of day only (no shadows on it); `*_beneath` water used as a pool's top stays dark;
- the three info lines under the list of the "Графика" options page no longer fit (12 rows).

Taste notes from this request (my reading of the user's words): effects should be visible but
"не переборщи" - natural strength, compared against the original game where one exists; they
judge by real play views (roof edges, under the feet, walking up to a wall), not by test stills.

Costs measured on the GTX 960M ([[user-hardware]]): real-time reflection for refract-only waters
(`r_deferred_water_reflect_all`, default 1) ~1-1.5 ms on demo_map; PBR <= 0.5 ms.
