---
name: hl2rpm-big-request-status
description: "Status of the user's HL2RPM polish requests: 18-item list, 5 follow-up bugs (fixed), the 11-bug list of 2026-10-01 (all addressed 2026-10-02) — what was done, what is still open"
metadata:
  node_type: memory
  type: project
  originSessionId: b1741039-a865-4aa5-b059-5b842a23b5ba
  modified: 2026-10-01T23:58:20.797Z
---

The user gave an 18-item list (Claude skills; auto sky/weather/light conversion on old maps; simpler F1 editor; deferred fixes for water/glass/eyes/foliage; realistic soft shadows; Graphics page; save/load; precipitation; clouds/moon/fog; flicker; open-world performance) and then mostly said "Продолжай" — work is done autonomously.

2026-10-01, first report — 5 bugs, all fixed the same day.

2026-10-01, second report — 11 bugs with screenshots (`C:\Users\mikeo\Pictures\Screenshots\Снимок экрана 2026-10-01 *.png`), all addressed by 2026-10-02 (details in CLAUDE.md):
1 skybox sparkles — 3D-skybox pixels marked in the G-buffer (normal alpha 1.0), no CSM/sky visibility for them.
2 nodraw shadows — shadow world lists with bShadowDepth + no culling in the shadow pass.
3 foggy distance — composite fog measured from the main view origin in the skybox (100 % fog); map fog color at night now the weather's.
4 mirrors (CGlassViewDeferred), Alyx eyelids (deferred VS ignored CPU flexes — no blinking/lip sync at all), fullbright glass (sky pixels of the light buffer held garbage).
5 blurry shadows — cascade lambdas 0.92+, PCSS rework. 6 bands — sky visibility moved into the SSAO pass. 7 light through walls — autoshadow + client shadow budget + DEFLIGHT_ENABLED respected; lamp shadows made ~4x cheaper (no oDepth). 8 opaque water — refraction alpha (water fog) now written. 9 flicker — r_csm_novis. 10 splashes — killed above eye height. 11 crash — shader API 32-constant overflow (hl2rpm_psconstants.cpp).

Still open / caveats: mirror reflections of points the main view doesn't see use approximate light (dark at night); APC glass fix verified on a test map, not on the user's own map (their APC spawns from a template); `volumpass_spot_ps30` "Couldn't load combo 4000" seen a few times (cause not found); the user's maps have no vrad/cubemaps and strong sky_camera fog (fogend 20000) — map authoring, not code. The user promised a list of old maps to test the automatic conversion. All work up to 2026-10-03 committed as bdc7c00 on master (on request; NOT pushed — local master was 5 commits ahead of origin). Commit/push only on request.

**Why:** a future session should not redo finished work and should know what the user still owes/was told. **How to apply:** continue from the open items; record frames, don't trust single screenshots. Related: [[hl2rpm-weather-testing]], [[user-hardware]].
