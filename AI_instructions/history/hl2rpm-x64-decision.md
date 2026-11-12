---
name: hl2rpm-x64-decision
description: "2026-10-23 x64 / Source SDK 2013 MP-base (TF2 branch) move checked and postponed - why, the measured evidence, the spike folder, and traps met while testing the 64-bit engine"
metadata:
  node_type: memory
  type: project
  originSessionId: 35c5e0f6-ce9d-4d90-8861-2a4508f10b11
  modified: 2026-10-23T09:31:47.851Z
---

The user brought a strategy doc (`E:\SourceModding\UsefulStuff\hl2rpm_x64_engine_strategy.md`, written with another AI) to port HL2RPM onto Valve's 2025 x64 SDK (SDK Base 2013 MP, AppID 243750) and use the TF2-2018 leak as an engine reference. After my review they chose "measure memory + spike SP on x64"; outcome 2026-10-23: **postponed**, recorded in `sp/src/CLAUDE.md` (bullet "x64 / MP-база 2025").

Evidence: HL2RPM 32-bit peaks at 1.85 GB of 4 GB address space over 7 heavy map loads (no memory pressure). Stock episodic x64 (`E:\SourceModding\x64_spike`, `SPIKE_NOTES.md`, logs in `logs\`) builds cleanly, but SP is broken in closed DLLs: `cl_localnetworkbackdoor 1` crashes on connect (engine passes -1 to a SendProxy), `load` leaves every physics constraint without its objects (vphysics.dll x64 pointer remap), returning to a visited level loses its state. Doc errors corrected: Steam release does not need x64 (Entropy: Zero 2 is Mapbase SP x86 on Steam), HL2 20th anniversary is still x86, "+10-20% FPS" baseless (GPU-bound).

**Why:** the user asked for my honest opinion before investing months; they accepted the evidence-first approach.
**How to apply:** don't reopen x64 unless Valve ships x64 SP/HL2, Mapbase ships SP-x64, or memory runs out (re-measure with `x64_spike\logs\mem_sampler.ps1`: VirtualQueryEx below 4 GB). The leak is useful as a reference for *our current* 32-bit engine, never copy code from it. Testing traps on the MP base: needs `cfg\valve.rc` for `+commands`; `wait` doesn't delay at startup (use the `spike_after` harness); pass `+cl_cloud_settings 0` (the 243750 cloud config is shared by all MP-base mods); engine writes `*.vpk.sound.cache` into mounted installs; `E:\Steam\dumps\crash_hl2_win64*` also holds the user's own dumps - never move/delete by wildcard. Related: [[user-hardware]], [[hl2rpm-weather-testing]], [[hl2rpm-bugs-2026-10-17-status]] (DXVK rejected).
