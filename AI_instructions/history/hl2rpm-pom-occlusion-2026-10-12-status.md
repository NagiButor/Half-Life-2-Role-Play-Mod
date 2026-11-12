---
name: hl2rpm-pom-occlusion-2026-10-12-status
description: "2026-10-12..17 — POM (incl. relief made of textures without bump maps), occlusion culling, tree sway, leaf transmission, envmap mask fix, GI sky light: built into the mod; 10-12 part committed (f76df78), 10-17 fixes NOT committed; what was verified"
metadata:
  node_type: memory
  type: project
  originSessionId: 01820a4e-5399-4b35-a927-2611225f1594
  modified: 2026-10-17T07:37:46.906Z
---

Request of 2026-10-11/12 (continuation of [[hl2rpm-final-graphics-2026-10-11-status]]): POM, occlusion culling, foliage translucency + sway, (optional) radiosity. Committed and pushed as f76df78 on 2026-10-12 at the user's request.

User feedback 2026-10-12 (screenshots on test_deferred) and the fixes of 2026-10-17 (built into the mod, `check_shader_combos.py` 0 mismatched, **not committed** — commit only when asked):
1. "No realistic light through the pine against the sun" → sun pass lets light through foliage shade by blocker distance (c80, `r_deferred_foliage_light`), composite transmission got a narrow bright lobe. Verified A/B: needles near the sun glow.
2. "Trunks sway like the foliage" → trunk only leans slightly, branches swing by reach from the trunk axis. Verified with a tga movie: trunk silhouette static, crown edges move.
3. "White/blue models" (Metrocop white, APC blue) → composite used base alpha as envmap mask un-inverted for every material; stock is 1 - alpha; flags were lost by the passthru. Fixed (c35, flags restored). The pale-blue surfaces → GI sky light was x4 linear: now open boost 1.8, response v^0.5, desat 0.3, indoor terms keep x4 (A/B on gi_test: rooms 1.0-1.2 of before).
4. "POM not visible on the brick wall" → that material (brick/brickfloor001a) had no bump map AND the user's config.cfg had `r_deferred_pom "scale"` (read as 0). Now: relief + normal map made of the texture for bump-less masonry (validated polarity on 174 stock materials, rules in scripts/hl2rpm_relief.txt), depth x0.4, adaptive march, groove shading, glancing-angle fade; non-numeric r_deferred_pom = 1.
Also: LUX-Shaders reviewed (forward shader overhaul, nothing for the deferred pipeline), console cleaned (editor_shader spheres, cl_thirdperson, CDeferredLight TimegateThink datadesc).

Still not done/verified: options-menu entries for the new convars (Effects page full); occlusion culling gain on a scene with hidden shadowed lamps; texture-relief load cost ~170 ms per material (demo_map 8, map_v11 17).

**Why:** the user calls this the final graphics pass and checks it on their own maps; they report what they see with setpos/setang lines.
**How to apply:** reproduce the user's setpos/setang on the named map first (test_deferred here), check their config.cfg for odd values, then A/B toggles inside one run; details in `sp/src/CLAUDE.md` (bullets dated 2026-10-12/17).

Update 2026-10-18: the 10-17 fixes are committed and pushed too (462305f, with bug list #2: [[hl2rpm-bugs-2026-10-17-status]]).
