---
name: hl2rpm-dialog-editor-2026-10-30-status
description: "HL2RPM Creation Kit (formerly Dialog Editor, PySide6+PyOpenGL exe): GECK-like 3D map window, NPC actor + AI packages, world layer in game, nav generation — status, what is verified, what is NOT committed"
metadata:
  node_type: memory
  type: project
  originSessionId: 687673d6-217f-430a-8587-89acd53b9a64
  modified: 2026-10-30T17:30:05.327Z
---

2026-10-30, part 1: dialog editor (dialogs/quests/voice/lipsync, languages in one file) — committed + pushed
(e736703) at the user's request.

Part 2 (same day): user asked to make the program "like GECK" — edit the map, set what NPCs do (sleep,
eat, patrol, everything GECK has) to fill the open-world RPG. Done, verified in game and in the exe,
**NOT committed** (user commits on request; offer it).
- Program renamed **HL2RPM Creation Kit**: exe `E:\SourceModding\HL2RPMSOURCE\HL2RPM Creation Kit\`
  (old `HL2RPM Dialog Editor` folder deleted), entry `hl2rpm_creation_kit.py` (git mv from
  hl2rpm_dialog_editor.py), PyOpenGL added. 3D map window (BSP + VPK content, props, Hammer NPCs),
  object palette, cell view, factions, NPC tabs Диалог/Персонаж/Распорядок with 24 h timeline.
- Game side: `game/server/hl2rpm_world.cpp` (layer spawn, defs, factions, nav, rpm_* commands/cvars),
  `hl2/ai_behavior_rpmroutine.cpp` (packages via actbusy), prop_loot_container, MAX_NODES 8192.
- Test map `ck_test` + its world in the user's test project `dialogsrc\test_dialogs.hl2dlg`.
**Why/how to apply:** details are in sp/src/CLAUDE.md section "HL2RPM Creation Kit" and the
hl2rpm-dialogs skill. Traps learned: widget grab() misses QPainter labels over QOpenGLWidget (shots use
screen grab); `+exec` cfg after `+map` runs before the map exists (use rpm_nav_autogen / rpm_start
cvars); Python heredocs mangle `\\n` — patch with script files.
Not done / open: FGD entry for prop_loot_container; marker placement presets per furniture model;
validator still lists old test_npc translation warnings.
Related: [[hl2rpm-weather-testing]]
