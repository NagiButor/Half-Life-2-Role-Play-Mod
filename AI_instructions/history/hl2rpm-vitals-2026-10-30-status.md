---
name: hl2rpm-vitals-2026-10-30-status
description: "Suitless vitals done 2026-10-30: ECG heart-monitor HUD in the HL2 box (white->yellow->orange->red), COD4-style red edges with growing veins, faint waves (blur/dark), sway + aim drift, breathing/heartbeat; verified in game, NOT committed (unstaged, on top of the staged 10-28 revert)"
metadata:
  node_type: memory
  type: project
  originSessionId: e3bafd22-6984-41a7-b906-8a8aac88c71b
  modified: 2026-10-29T23:45:07.439Z
---

User's design (2026-10-30, with a concept image): without the HEV suit the HUD shows no numbers - a white pulse line in the stock HL2 rounded box where HUD health is; lower health = stronger/faster pulse and the line goes yellow -> orange -> red; the old full-screen red tint replaced by COD4-style red edges "like veins"; at very low health sway, clouding, darkening that comes and goes. Breathing ("отдышка") kept.

Done and verified in game (screenshots at 100/60/40/22/10/4 %, a 6-s frame recording, death, suit on, `-nodeferred`, save/load in a fresh process, sound starts via `developer 1; snd_showstart 1`). Details are in sp/src CLAUDE.md, section "Без костюма HEV: пульс в HUD и «раненый» взгляд". Cost of the screen pass ~0.45-0.5 ms on the 960M (A/B in one run).

Taste points left to the user: vein look (texture from `make_vitals_assets.py`, seed 7, blur 1.6), how strong the red is at 40-60 %, sway amplitude (`cl_vitals_sway`), heartbeat volume (an addition the user didn't ask for - say so if they dislike it), HEV low-health beep now silent without the suit. On death the stock HL2 `HudPlayerDeath` red fill still covers everything (untouched).

**Why:** next session must know this exists and is uncommitted; the user commits only on request.
**How to apply:** check `git status` in sp/src - the 10-28 revert is still staged ([[hl2rpm-session-2026-10-28-reverted]]); these changes are unstaged on top. Don't mix them into a commit of the revert unless the user asks.

Update 2026-10-30: committed + pushed on the user's request ("закоммить и запушь") - see [[hl2rpm-gi-v3-2026-10-30-status]] for the four commits.
