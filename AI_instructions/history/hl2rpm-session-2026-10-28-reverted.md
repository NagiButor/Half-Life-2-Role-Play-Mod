---
name: hl2rpm-session-2026-10-28-reverted
description: "The 2026-10-28/29 RPG-slice session (design doc -> playable slice, RPG UI, quests, rp_c17_* maps, law, language option) was reverted on the user's request; only test maps in hl2rpm\\testmaps were kept. Where the reverted work lives."
metadata:
  node_type: memory
  type: project
  originSessionId: 36bb20a5-cfb0-4f50-9842-f145d58bb161
  modified: 2026-10-31T00:48:50.572Z
---

2026-10-29 the user ordered: "Убери все изменения из этой сессии, оставь только, что тестовые карты в отдельной папке". Done the same day:

- **Kept:** test/dev maps and their content in `hl2rpm\testmaps` (gameinfo search path below the mod root), VMFs in `mapsrc\test`, `compile_map.ps1` routing, the CLAUDE.md rows and hammer SKILL.md lines about it. The old duplicate test maps (map_v10, map_v11_001, test_deferred_1..4, test_dialogue_2/3, test_saveload*, dialog_test, test) came back from the archive into `testmaps\maps`; `hl2rpm\maps` holds no maps now. New Game still starts `test_dialogue` (chapter1.cfg), as before the session.
- **Reverted:** everything else: sp/src back to `cff32d7` (the user's design-doc PR stayed then; the design doc itself was deleted 2026-10-31 on request, see [[hl2rpm-no-design-doc]]), mod files (gameinfo name "Half-Life 2: Role-Playing Mod", clientscheme, localization, chapters, FGDs, playtest skill), the mod-folder cleanup (mod_archive moved back and removed), DLLs rebuilt.
- **Where the reverted work is:** git commits 5dc412b, 453efcd, c8487d6, c9c2a77, 27f1260 (on GitHub master; the revert was left staged, not committed or pushed, pending the user's word); every file the session made or changed outside git is in `E:\SourceModding\hl2rpm_session_backup_2026-10-28` (mod, mapsrc, fgd, skills, BSPSource + decompiles, map backups). `git show 27f1260:CLAUDE.md` holds engine traps learned in that session (func_brush Solidity 2 never opens, logic_rpg retest, MSVC CP1251 literals, TextEntry PaintBackground, NPC parented to func_tracktrain falls through, VGUI capture of RPG screens).

User's verdict on that work before the revert: "Оооочень посредственое качество", the first location "похоже на жуткий испорченный оригинальный уровень игры, а не на новую ААА игру". See [[hl2rpm-level-building-guidance]].

**Why:** the next session must not assume the slice, rp_* maps, RPG UI or law system exist.
**How to apply:** check `git log`/`git status` in sp/src first; restore pieces from the commits or the backup folder only if the user asks.

Update 2026-10-30: the user chose "commit everything by topic": the revert is commit 80f1906, pushed to GitHub master.
