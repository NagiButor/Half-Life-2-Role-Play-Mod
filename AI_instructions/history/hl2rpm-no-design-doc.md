---
name: hl2rpm-no-design-doc
description: "The user's design document (sp/src/Дизайн-документ: LAMBDA stats, bracelet UI, acts) was deleted on request 2026-10-31 - never use it as a source for RPG/UI/quest design"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 687673d6-217f-430a-8587-89acd53b9a64
  modified: 2026-10-31T00:48:54.837Z
---

2026-10-31, while I was planning the RPG system from `sp/src/Дизайн-документ` (Λ.А.М.Б.Д.А. stats, 13 skills, "учётный
браслет"), the user said: «Стоп стоп стоп, не надо дизайн документ использовать этот, удали его ваще». Deleted in commit
4d0ba29 (still in git history, e.g. cff32d7).

**Why:** the user does not want that document to drive the game anymore.
**How to apply:** do not read or cite it; RPG design is "like New Vegas" (7 SPECIAL-like stats, 12 skills, perks, FNV XP
curve), with all names/numbers editable in the Creation Kit — my own defaults, see [[hl2rpm-dialog-editor-2026-10-30-status]].
Do not restore it from git unless the user asks.
