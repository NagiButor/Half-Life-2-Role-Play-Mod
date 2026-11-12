---
name: hl2rpm-gi-v3-2026-10-30-status
description: "GI probes reworked 2026-10-30 (user: 'shading/bounce loads in chunks as you approach'): one dense 64x64x16 near grid, far grid at 2x, background worker threads, GPU-eased display atlas; verified in game, committed + pushed 1c7c4eb"
metadata:
  node_type: memory
  type: project
  originSessionId: bd1d1ac4-2d09-4731-a8bb-05ed5dcd0cde
  modified: 2026-10-30T09:55:06.641Z
---

Request (2026-10-30, with two screenshots in the station hall of `rpm_trainstation_testoptimization`, setpos -6420/-6816): "fix the crappy loading of soft shadows when getting closer; rework the system, maybe remove it or do it the modern way". Clarified mid-task: it is the darkening of rooms and the bounce light (GI), "chunks load one after another in real time".

Cause found: far grid 4x coarser than the near one (±1024) - its border crossed the hall in front of the player; probes first traced with 32 rays then refined; bounce re-gathers swept slots in order (blocks changed one after another).

Done (details in sp/src CLAUDE.md, bullet "GI без «подгрузки»"): near grid 64x64x16 (±2048), far 48x48x12 at 128; own worker threads (below normal) in the background, full rays at once; display atlas eased toward the written one on the GPU (`deferred_gi_blend_ps30`, new shader in hl2rpm_custom.txt); points upload; pending list + slab-only grid moves (main thread 0.3-1 ms/frame while walking). Old vs new A/B: rooms of gi_test look the same; the moving bright bands at the hall end are gone. First fill on load 3-4.5 s.

Not the GI: a "dark sphere around the camera" in `r_deferred_gi_debug 3` was the hall floor (roofed, sky 0 - correct) plus frame exposure; same-point brightness from different distances differs by exposure, not GI (debug 10 proved it).

Also added: `capture_rt.ps1` to the playtest skill (real-time window capture between `echo CAPSTART/CAPEND`).

Committed + pushed 2026-10-30 on request as four topic commits: 80f1906 revert of the 10-28 session ([[hl2rpm-session-2026-10-28-reverted]]), d32da3d compilers + occlusion ([[hl2rpm-compilers-occlusion-2026-10-30-status]]), a166feb vitals ([[hl2rpm-vitals-2026-10-30-status]]), 1c7c4eb GI. Intermediate commits were not built on their own (the final tree is the tested one).

**Why:** next session must know the GI architecture changed. **How to apply:** If the user still sees loading: check in real time (not startmovie), `r_deferred_gi_stats` phases, `r_deferred_gi_debug 5/10`.
