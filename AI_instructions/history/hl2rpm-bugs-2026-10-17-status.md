---
name: hl2rpm-bugs-2026-10-17-status
description: "Bug list #2 of 2026-10-17 (rain SSR flicker, sun glare through foliage, leaf backlight, grass shadows on demo_map) fixed + verified in game, committed + pushed 2026-10-18 (462305f, "good playable version" per the user); DXVK / graphics-API switch measured and rejected by the user on 2026-10-18"
metadata:
  node_type: memory
  type: project
  originSessionId: 01820a4e-5399-4b35-a927-2611225f1594
  modified: 2026-10-17T23:36:19.494Z
---

Second bug list after the POM/occlusion commit (f76df78). All four fixed, built into the mod and checked in game; committed and pushed together with the 10-17 list #1 fixes as 462305f on 2026-10-18 - the user called it "хорошая играбельная версия" (no tag/release made; offer v0.3 if they want a backup release). Details are in sp/src/CLAUDE.md (SSR paragraph, "Ореол солнца", "Просвечивание листвы", "Трава detail-спрайтов").

- Rain SSR flicker while walking: the frame-copy slot `TEXTURE_FRAME_BUFFER_FULL_TEXTURE_0` was overwritten by refracting rain particles (`UpdateRefractTexture` into `_rt_PowerOfTwoFB`). Verified: puddle reflection of the orange wall decays smoothly over a 282-frame strafe in rain, frame-to-frame jumps < 1 %.
- Glare/flare through leaves and trunks: sun visibility now from G-buffer depth on the GPU (`hl2rpm_lum_ps30` pass 2, `.b`). Verified hidden behind the crown.
- Leaf backlight ("не вижу подсветку листвы"): glow term in the sun pass + light through the crown only through *leaves* (litface casters flagged +2 in the sun's raw caster depth). Defaults now foliage_light 0.7 / depth 100 / glow 2; I changed the user's config.cfg `r_deferred_foliage_light` 0.55 -> 0.7 (backup `config_before_foliage.cfg` in the session scratchpad). Strong against a low sun; with the sun behind a dense pine only the crown edge glows (physically right).
- Grass shadows on demo_map: detail sprites into the 2 nearest sun cascades; cost within noise (0.3-0.7 ms).
- `lightingpass_global_ps30` is at the temp-register limit: even one extra debug return gives X4505 (see [[hl2rpm-weather-testing]]).

**DXVK decision:** the user asked for DXVK / DX10-12 / Vulkan with a menu switch, then after my measurements said "Что-то мне идея с DXVK не нравится, отменяем. Оставляй пока D3D9".
**Why:** DXVK 3.1.1 on their GTX 960M gave the same picture but 41-48 ms vs 20-24 ms at 1600x900 (GPU-side; equal at 800x450); native DX11/12 is impossible without engine source.
**How to apply:** don't propose graphics-API translation layers again unless the user brings it up; nothing of it was left installed (base `bin` has no d3d9.dll, the DXVK cache in %LOCALAPPDATA%\dxvk was deleted).

Related: [[hl2rpm-pom-occlusion-2026-10-12-status]], [[user-hardware]].
