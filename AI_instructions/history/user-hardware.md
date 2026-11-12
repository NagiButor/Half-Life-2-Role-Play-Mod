---
name: user-hardware
description: "User's PC is a laptop with GTX 960M (2 GB) + Intel HD 530, 1536x864 desktop at 125% DPI — sets the performance budget for HL2RPM graphics"
metadata:
  node_type: memory
  type: user
  originSessionId: b1741039-a865-4aa5-b059-5b842a23b5ba
  modified: 2026-10-01T23:59:11.621Z
---

The user's machine (checked 2026-09-29): NVIDIA GeForce GTX 960M 2 GB + Intel HD Graphics 530 (Optimus laptop), Windows 11, desktop 1536x864 logical (1920x1080 at 125% DPI). hl2.exe runs on the NVIDIA GPU (the Options → Graphics page shows the adapter name).

Measured on test_deferred at 1280x720, vsync off (2026-09-29): "Ultra" shadows ~57 fps, "Medium" ~83 fps; weather/clouds cost ~1 ms, shadows dominate.
After the 2026-09-30 shadow work (no oDepth in the sun cascades + staggered 2nd cascade), 1600x900 "High": test_deferred ~11–13 ms (was 15–17), open EP2 map ep2_outland_07 ~16–17 ms (was ~20); ~7 ms of that scales with resolution.
The user's own config.cfg is on Ultra (`r_csm_quality 5`, `r_deferred_shadow_quality_pointspot 5` = 2048² cube faces). 2026-10-02: spot/point shadow maps without shader depth — one point light shadow at Ultra ~2–3 ms (was ~9.5), demo_map_rus start view 57 → 77 fps; map_v11 from 400 u above the spawn ~37 fps (worst seen).

**How to apply:** keep new rendering features cheap enough for this GPU; default the mod to the "High"/"Medium" graphics presets, not Ultra. Window screenshots need a DPI-aware capture (SetProcessDPIAware + PrintWindow). See [[hl2rpm-weather-testing]].
