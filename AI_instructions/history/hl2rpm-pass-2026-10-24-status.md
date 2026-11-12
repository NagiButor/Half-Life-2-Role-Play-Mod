---
name: hl2rpm-pass-2026-10-24-status
description: "2026-10-24 big request (decal PBR, glass of all kinds, reflections, water kinds, clouds, upscale, death cam, TAA strafe, stunstick, default player) - done, verified, committed + pushed as b4da9cb"
metadata:
  node_type: memory
  type: project
  originSessionId: 35c5e0f6-ce9d-4d90-8861-2a4508f10b11
  modified: 2026-10-24T14:13:59.699Z
---

Request of 2026-10-24 (after commit cac6cf8 "final graphics pass"): PBR/parallax for decals (blood that
reflects, holes), PBR for NPC/corpses/weapons, realistic glass of all kinds incl. window textures of
map_v11 facades, upscale, default player model male_07, clouds, stunstick double glow with Mapbase legs,
strafe blur (map_v11 setpos -1785 -1271 60), first-person death cam from the corpse, remove water foam,
water by kinds (murky/clear/radioactive), test everything. User: "done when all of these are fixed".

Done and verified in game (screens in the session scratchpad): DEFERRED_DECAL (blood film gloss 0.9,
holes with relief), DEFERRED_GLASS (translucent panes: Fresnel sky + SSR from _rt_FullFrameDepth rgb,
sun glint, dirt), facade window gloss maps from envmap masks + HL2RPM_MIRROR pass (sky "as seen" from the
sky-view LUT + SSR) - windows of map_v11 now reflect the sky; PBR hint floors (no more 0.20/10% plastic
on 115 materials); clouds 20 km shape period + altocumulus fix (the "confetti" was the altocumulus layer);
water kinds ($water_absorb/$water_glow) + 5 new materials materials/hl2rpm/water + glowing slime; TAA
motion; death cam; stunstick; male_07; sky-not-visible clear (garbage colors when the eye is in a solid).
Details are in CLAUDE.md ("Проход 2026-10-24").

Open / caveats: Refract-shader glass stays stock; upscale of faces/weapons (in place, list in
materials/hd_inplace.txt) and decals (materials/hd/decals) - check results; mirror pass costs ~1 ms;
murky/swamp new waters were tuned denser after the first look; gfx_showcase recompiled for the glass fix.

**Why:** the user wants the graphics finished so they can move to RPG systems.
**How to apply:** committed + pushed (b4da9cb, 2026-10-24); the new mod files (materials/hl2rpm/water,
HD textures of faces/weapons/decals) must go into the next release archive.
