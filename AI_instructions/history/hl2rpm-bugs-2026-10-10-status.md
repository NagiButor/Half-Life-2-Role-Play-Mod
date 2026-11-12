---
name: hl2rpm-bugs-2026-10-10-status
description: "2026-10-10: the user's list of 17 visual bugs found right after the v0.2 release (black UI, GI loading in chunks, fullbright objects, water, clouds...) - causes, what was fixed and verified, what is left; nothing committed"
metadata:
  node_type: memory
  type: project
  originSessionId: b1741039-a865-4aa5-b059-5b842a23b5ba
  modified: 2026-10-10T13:42:22.083Z
---

Right after the v0.2 release the user played and sent 17 bugs with screenshots ("Дохренища визуальных багов", "Ты поломал интерфейс"), then "Продолжай делать исправления". All 17 were traced to a handful of causes and fixed the same day; details are in `sp/src/CLAUDE.md` (bullets on c29-c30, "Всё, что относится к непрозрачной сцене…", GI, water, sun shaft dust).

**Causes (one cause = several of the user's items):**
- Engine constant c30 trashed by our passes (the dust motes pass was the last one of a frame without a viewmodel): black VGUI/HUD, black water. Fixed generically: every draw of our shader DLL hands c29/c30 back.
- Things composited after the weather pass got no fog/wetness: "fullbright" houses (models with a translucent antenna), a crisp distant tree (static prop inside its fade distance), grass in the rain. Now drawn with the opaque scene.
- GI probes: one thread, 1 ms a frame - visible chunks for half a minute, a bright seam 740 units out, a light pit floor. Now worker threads, the whole grid in a map's first frame, a far cascade and an estimate beyond it, fade-in.
- Half resolution GI read with plain bilinear: light lines on every brush edge, dark outline of silhouettes. Now a surface-aware upsample pass.
- Water: red foam (unset register), black refraction outside the drawn area, vertical caustic stripes on walls, repeating caustic cells. Sun shaft "dust under a roof" under trees (aura). Cloud shapes repeating every 8 km.

**State at the end of 2026-10-10:** built, deployed to the mod, verified in game on test_deferred / demo_map / gi_test (UI with HUD on, walking with `r_deferred_gi_debug 5`, save/load, map change, mat_reloadallmaterials; exit code 0, no new dumps). **Not committed** (commit only when asked). The published v0.2 archive still holds the buggy DLLs - a new release needs the user's word.

**Left as it was (not in the user's list, say so if they come up):** hard-edged displacement blend patches on demo_map's ground in deferred; clouds are flat white blobs at noon; a `*_beneath` water material placed on top of a pool (test_deferred) stays dark; static props don't block GI any more (world-only tracing); fading props pop at the end of their fade distance instead of fading.

**Why:** the user will re-test these exact spots; a new session must know which complaint maps to which fix. **How to apply:** if one of the 17 comes back, start from the CLAUDE.md bullet of its cause; verify the way [[hl2rpm-weather-testing]] says (the checks that were missing). Related: [[hl2rpm-visual-checklist-status]], [[hl2rpm-release-procedure]].
