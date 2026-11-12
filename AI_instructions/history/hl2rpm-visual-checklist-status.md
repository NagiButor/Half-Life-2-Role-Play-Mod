---
name: hl2rpm-visual-checklist-status
description: "2026-10-03..10 the user's ~120-item \"modern graphics\" checklist + \"dark interiors with soft light\": everything built and verified (GI, post stack, TAA, soft particles, motes, flare, DoF...), what was left out and why; committed and pushed as e4ce7fd with a root README.md"
metadata:
  node_type: memory
  type: project
  originSessionId: b1741039-a865-4aa5-b059-5b842a23b5ba
  modified: 2026-10-10T08:18:45.469Z
---

Request 2026-10-03: big checklist (lighting, shadows, AO, materials, reflections, water, atmosphere, post, temporal...) — "check what exists, add what's missing, make it beautiful; interiors must be dark with soft light". User said "Продолжай" several times (work autonomously) and watches the test runs live ("Я вижу пылинки, если что" when I claimed the motes weren't drawn — stills hide them, motion shows them).

**State 2026-10-10: built, verified in game, documented in CLAUDE.md, committed and pushed on the user's request** (`e4ce7fd` on `master`, origin in sync; the same commit added the repo's first root `README.md` — the GitHub front page, in Russian; keep it current when features change). Released the same day as `v0.2` (tag on `869c53b` = that commit + a README install-step fix), see [[hl2rpm-release-procedure]].

Built in this task (details and convars in `sp/src/CLAUDE.md`):
- GI probes (dark interiors, bounce), contact shadows (sun), SSR in puddles/wet, water foam + caustics, sun shaft dust indoors.
- Post stack: eye adaptation, bloom + lens dirt, grading, vignette/grain/CA, CAS sharpen; added 10-10: sun lens flare (`r_hl2rpm_flare`), depth of field (`r_hl2rpm_dof` 1 = zoom and dialogs).
- TAA (`r_deferred_aa` 2, default) replacing FXAA; engine MSAA is switched off for the session at startup (`r_deferred_disable_msaa`, registry untouched: the user's stored value stays 8x) — it smoothed nothing in deferred and cost ~2-3 ms.
- Soft particles (`r_deferred_soft_particles`), dust motes (`r_weather_motes`).
- RT content loss handling (mat_reloadallmaterials / device reset emptied the GI atlas: interiors went bright) — `DeferredMaterials_OnRestore`.
- Options: page "Графика" 11 settings, "Эффекты" 12 (full).
- Demo map `gi_test.bsp` left in the mod's maps on purpose (showcase of dark rooms / beams / motes; source in mapsrc, generator in extras/tools) — tell the user it can be deleted before a release.

Not done, with reasons (say so in reports): PBR/roughness/metallic, POM, tessellation/displacement, clear coat, anisotropy, SSS/skin, hair — need authored content (maps HL2 doesn't have); per-object motion blur / motion vectors for skinned meshes; local fog volumes (needs a new entity); GTAO/bent normals (SSAO kept); upscaling (FSR/TAAU). Moving NPCs under TAA rely on history clipping (no ghosting, weaker AA).

Measured (demo_map_rus spawn, 960M, 1600x900, MSAA off): all on 17.6-18 ms (56 FPS), without GI/post/contact/AA 13.7 ms.

**Why:** long autonomous task across many sessions; a new session must not redo finished parts or re-investigate solved pitfalls. **How to apply:** if the user reports a visual issue, check CLAUDE.md's section of that system first; if asked to commit, sync extras (FGD, crashtools, resource, claude_skills) and follow the commit rules. Related: [[hl2rpm-shadows-phaseB-status]], [[hl2rpm-weather-testing]], [[user-hardware]].
