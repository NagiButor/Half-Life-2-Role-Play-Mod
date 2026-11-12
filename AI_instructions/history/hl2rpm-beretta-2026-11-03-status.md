---
name: hl2rpm-beretta-2026-11-03-status
description: "2026-11-03: rpm_trainstation_testoptimization 'CUtlRBTree overflow' fixed (unsigned short CUtlMap in deferred_worldmesh); Beretta 92 from CS:S Elites + P228 anims on ValveBiped arms with per-suit hands - done and verified in game, NOT committed"
metadata:
  node_type: memory
  type: project
  originSessionId: 687673d6-217f-430a-8587-89acd53b9a64
  modified: 2026-11-03T01:11:46.733Z
---

Two requests of 2026-11-03 (user screenshot of an Engine Error dialog + addon "Dual Berettas"):

1. **Map crash**: `rpm_trainstation_testoptimization` (recompiled by the user 03.11 3:09, vbsp only - no vvis/vrad in the log) closed on load with "CUtlRBTree overflow!". Cause: `edgeUse` in `deferred_worldmesh.cpp` was `CUtlMap<uint64,int>` (unsigned short index, 65535) and the map now has 89k edges. Fixed with `CUtlMap<K,V,int>` (also GI `s_AlbedoMap`, nav `visited`); map loads and renders (verified). The map name is still 33 chars (saves won't load) - told before.

2. **Beretta 92** (`weapon_rpm_beretta`): the GMA is only Lua around Garry's Mod's `c_pist_elite`; GMod is not installed, CS:S is. Built by `hl2rpm_extras/tools/make_beretta.py` (CS:S v_pist_elite gun + v_pist_p228 single-pistol anims retargeted onto ValveBiped arm bones so the mod's per-suit hands bone-merge). Verified in game: grip, tactical/empty reloads, slide lock on last shot, citizen/HEV/CP hands, NPC holds it like w_pistol, pickup + Tab card model, ck_test demo refs 120-121 + trader goods. Details in CLAUDE.md "Беретта 92".

Traps found: CS:S view models are built left-handed (gun in `Left_*` bones, mirrored by the game); CS:S view model FOV is 74 (54 puts it off screen); the decompiler's FK + a numpy rasterizer (`--preview`) caught both before running the game. Holding R > 0.6 s holsters (mod feature) - tap R in tests.

**Why:** next session should not redo or assume committed. **How to apply:** commit only on request (sp/src + hl2rpm_extras; models/sounds/textures stay out of git). Related: [[hl2rpm-menu-content-2026-11-02-status]], [[hl2rpm-keep-demo-content]].
