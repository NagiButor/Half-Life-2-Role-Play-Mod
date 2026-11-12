---
name: hl2rpm-shadows-phaseb-status
description: "2026-10-02/03 shadow & flicker work on HL2RPM: walking micro-flicker finally traced to the rain map matrix order (one-frame flash), sky visibility rework, water/skybox/grass fixes of the 6-item list of 2026-10-03; what is verified and what is open"
metadata:
  node_type: memory
  type: project
  originSessionId: b1741039-a865-4aa5-b059-5b842a23b5ba
  modified: 2026-10-03T02:41:44.763Z
---

2026-10-02: 8-item list (sun shadows acne/stairs/gaps, Citadel, time speed, water, body) done; FXAA added (no MSAA in deferred), crease normal, footprint filter.

2026-10-03, 6-item list (user: "самое главное — мерцание и наложение теней"):
- **Walking micro-flicker = rain map re-render read through the previous frame's matrix** (WeatherRender_CommitFrame ran before RenderRainOcclusion). Verified by A/B walking recording with `r_deferred_rainmap_debug_late 1`: 1400 flashing 8x8 tiles in one frame -> 0 with the fix. The earlier FXAA/footprint work only reduced edge crawl.
- "Overlapping shadows" in shade = sky visibility ring probes (bands parallel to walls). Replaced by a precomputed 128x128 sky visibility map (deferred_skyvis_ps30, 192 taps, from the rain map's raw R32F). ESM-style soft-min failed: void behind the map edge (depth 1.0) dominates.
- Water opaque/clear flip = one refraction view for all waters, alpha = fog of the engine-picked water. Now alpha = sqrt(depth/1024), each water decodes with its own fog range; mod compiles SDK_water_ps2x (ps20b) itself.
- Skybox buildings through map houses = translucent non-two-pass models (row_*_fullscale with antenna) never in the G-buffer; their opaque meshes are now drawn into it.
- Grass (detail sprites) now deferred (G-buffer + DEFERRED_BRUSH alpha-test), gets sun shadows.
- Barrel dark stripe: NOT reproduced (pit barrels, 6 sun times, lying/standing, near the player). Facet-aligned stripe suggests shadow self-occlusion of one facet; open.
- Seen while testing: wavy acne bands on demo_map gravel displacement in the shadow term at 18h (low sun); not addressed.

Follow-up 2026-10-03 (user: "фликеринг тени исправил", 4 new items):
- Parts of shadows popping with a step / camera turn = `r_csm_center_origin 1` (my 10-02 change: light camera at the cascade center with negative zNear; caster culling ignores the negative near plane). Default reverted to 0 (12000 u toward the sun), A/B verified at 8 u steps.
- Shade bands in the test_deferred courtyard = sky visibility under a high floating block (covered branch judged at the block's own top -> inverted, plus wall/curb bands). Now: outdoor scale 8, cover threshold 24-48 u, "no ground in sight at the point's level" -> covered, and a wide 512 u map for covers > 128 u above.
- Citadel top bluish by day / black at night = translucent VertexLitGeneric fallback; `$deferredtranslucent` keeps it in DEFERRED_MODEL composite.

**Why:** the user judges by eye and video; claims must come from recordings (flash_detect.py, FLAT=1) and A/B toggles, not single screenshots.
**How to apply:** for new flicker reports record a steady walk and run playtest `scripts/flash_detect.py`; use `r_csm_pcss 5/6/7/8` debug views to split shadow / sky visibility / SSAO / G-buffer content. Related: [[hl2rpm-weather-testing]], [[hl2rpm-big-request-status]], [[user-hardware]].
