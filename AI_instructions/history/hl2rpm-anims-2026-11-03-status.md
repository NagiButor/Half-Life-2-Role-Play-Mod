---
name: hl2rpm-anims-2026-11-03-status
description: "2026-11-03 request - NPC yield/pickup/weapon law, fists, talk gestures, animation library (chair, couch, eat, bed, ground, ground sleep, wall, idles, Kimodo clips), third person, MotionBricks/Kimodo; all verified in game, NOT committed; traps"
metadata:
  node_type: memory
  type: project
  originSessionId: 687673d6-217f-430a-8587-89acd53b9a64
  modified: 2026-11-03T12:37:02.159Z
---

Request of 2026-11-03 (user: "work as many hours as needed, report on everything"): NPCs don't step aside / pick up
weapons (CK keys yield, pickup_weapons), Combine react to a civilian's gun (weapon_law), fists for aggressive NPCs,
a big animation library usable by every NPC with transitions, Fallout 4-like third person (V), motion-bricks.cpp and
Kimodo installed (Kimodo: CPU only, ~10 min per clip, 10 clips in kimodo.cpp\hl2rpm_out, agent report in HL2RPM_NOTES.md).

Done and verified in game on ck_test (-nodeferred), NOT committed:
- yield / pickup / weapon law / fists; talk gestures on dialog lines and barks (hl2rpm_talkgest.cpp).
- library sets: chair, couch, eat (bowl + spoon), bed (sit on the edge -> lie, 3 bases), ground sit, ground sleep,
  wall lean (per-skeleton back depth), 6 standing idles, fists, Kimodo clips (bored idle, talk, crouch search) via
  rpm_anim/kimodo.py retarget. All 4 skeletons (male, female, police, combine).
- CK: fit buttons (furniture, bed, wall, floor), anim lists/defaults, fixed false "no animation" warning; exe rebuilt.
- player can sit / lie / lean on the same markers; lying camera from above (world-only trace).
- actbusy approach to CK markers: a spot 8 in front, else 8 behind (hull check) - fixed NPCs that never reached a
  lean marker when unseen (efficient AI) and work markers facing a crate.

Traps found: keyed() base weight (standing sets used the sitting base); model bind pose faces -Y for the Kimodo
retarget; a test camera inside a brush hides entities; console-made cops without a faction die on ck_test; heredoc
quoting (write edit scripts with the Write tool, [[feedback-heredoc-escapes]]).

Related: [[hl2rpm-beretta-2026-11-03-status]], [[hl2rpm-keep-demo-content]].
