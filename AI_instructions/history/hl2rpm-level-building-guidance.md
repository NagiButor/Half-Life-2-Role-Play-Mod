---
name: hl2rpm-level-building-guidance
description: "How the user wants HL2RPM levels built: Valve maps only as reference, new AAA-looking levels (retail-map conversions were rejected), deferred night lighting, seamless open world"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 36bb20a5-cfb0-4f50-9842-f145d58bb161
  modified: 2026-10-29T12:05:25.366Z
---

The user gave reference folders to study ACTIVELY ("не ленись") for how Valve builds buildings, City 17 residential blocks, canals, outskirts: `E:\SourceModding\UsefulStuff\HL2 decompiled maps`, `E:\SourceModding\UsefulStuff\HL2Leak-VMFs` (the user calls both fan decompiles, not a leak), the unpacked VPKs, and their own 2020 `map_v11` (station extension; they find their own buildings too simple).

2026-10-28/29 I built the slice by converting retail d1_trainstation maps (own BSPSource decompile, Valve people stripped, RPG layer on top). The user's verdict: very mediocre, "похоже на жуткий испорченный оригинальный уровень игры, а не на новую ААА игру", Valve NPCs must go entirely ("ты делаешь с нуля"), and then the whole session was reverted ([[hl2rpm-session-2026-10-28-reverted]]). Reference for style, not the base of the level.

Maps made for baked lighting look wrong in deferred: street and indoor lamps (models, sprites, lit windows) must switch on after dark and off at dawn; check at 19:00, 22:00, 02:00, 06:00. Baked-light fakes (`effects/vol_light*` shafts, `point_spotlight` beams for window light, `env_sun` sprites, fog cards) do not belong in a deferred map: the renderer has real shafts, volumetrics and fog.

Open world like New Vegas, seamless like the original: where Valve cut a place between chapters, the mod's sector is whole (the user did this in map_v11 with 01+02, and turned 3D-skybox fake houses into a real location).

**Why:** the user will publish on Steam and judges by playing the first location; anything that reads as retail HL2 reused reads as "garbage".
**How to apply:** for level work, propose original layouts informed by the references and agree the approach with the user before building; night-lighting pass with night screenshots ([[hl2rpm-weather-testing]]).
