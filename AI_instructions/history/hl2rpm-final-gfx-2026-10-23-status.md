---
name: hl2rpm-final-gfx-2026-10-23-status
description: "2026-10-23/24 'final graphics' request: showcase map, POM everywhere, plastic PBR, $detail, fire/explosion light, stunstick, HD textures, dusk, texture-light merge, fade order - done and verified in game; committed + pushed as cac6cf8"
metadata:
  node_type: memory
  type: project
  originSessionId: 35c5e0f6-ce9d-4d90-8861-2a4508f10b11
  modified: 2026-10-24T03:47:06.049Z
---

Request 2026-10-23 (user: "финал графики, дальше RPG"): test map with all models/materials/NPCs/fire/explosions;
POM missing on many materials; brick walls and human NPCs look plastic with PBR; upscale/improve all textures;
fire and explosions must light the scene; all player weapons OK in deferred (stunstick effects buggy).
Done when screenshots can't be faulted. Status 2026-10-24: all done and verified in game, reported to the user;
NOTHING committed (user commits on request only). Everything is in CLAUDE.md ("Финальный проход графики",
"Сумерки", texture-light merge in the light conversion paragraph, fade order in the post-processing paragraph).

Final screenshot sheets: scratchpad final_report/1..8 (session 35c5e0f6). Step files there: final1_steps.txt
(showcase day), final2_steps.txt (night: fire, explosion movie, stunstick/pistol movies, flashlight), final3_steps.txt
(map_v11 day/dusk/night + demo_map_rus), dusk_steps.txt (17.5..22.5 series from one spot), hdab_steps.txt (HD A/B).

Found during the final pass (2026-10-24), fixed: dusk at 18:36 three times darker than moonlit midnight (twilight
sky + dusk floor); moon lens flare ring (flare x0.05 at night); 384 converted texture-light patches on one showcase
panel = 7 fps (MergeSurfaceLights, map_v10 66 -> 33 lights); screen fades/overlays went through exposure/bloom/TAA
(moved after post; Source fades blend in linear space even in stock - 12 % white over dark = ~95/255).
HD textures: 381 (254 MB) in materials/hd for the user's maps + showcase, only with mat_picmip <= 0 (user: -1).
POM cache cache/pom ~200 MB - keep out of release archives.

Left to the user (reported, not changed): player body = models/player.mdl placeholder (gameinfo has
player_default_legs 1 but no player_default_model) - grey dummy when looking down and in mirrors; map_v11 3D-skybox
canal water flat without built cubemaps (stock renders it pink); flat white small clouds in clear weather.
Testing traps: inventory weight limit 40 kg blocks a second weapon after the physcannon (sv_inventory_weight_max 500
in test steps; physcannon only reliably when given first/alone); [tour] frame times are clamped at 100 ms - count frames.
Related: [[hl2rpm-weather-testing]], [[user-hardware]], [[hl2rpm-pom-occlusion-2026-10-12-status]],
[[hl2rpm-release-procedure]].
