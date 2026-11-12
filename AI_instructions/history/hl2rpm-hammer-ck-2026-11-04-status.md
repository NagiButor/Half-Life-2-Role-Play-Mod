---
name: hl2rpm-hammer-ck-2026-11-04-status
description: "2026-11-04 - Claude task markers for Hammer++ (rpm_claude_task / func_rpm_claude_area, Claude-spark icon, dropped by hl2rpm_vbsp), Hammer++ in the Creation Kit (geometry straight from VMF, auto-reload on save, task list, snapshots for Claude), CLAUDE.md split into sp/src/claude_docs; NOT committed"
metadata:
  node_type: memory
  type: project
  originSessionId: 687673d6-217f-430a-8587-89acd53b9a64
  modified: 2026-11-04T01:23:42.478Z
---

The user sketches maps by hand and leaves tasks for me in Hammer++: point `rpm_claude_task` (icon = the Claude Code spark,
the user's wish) and volume `func_rpm_claude_area` (translucent terracotta texture). Read with `vmf.py tasks`, answer with
`vmf.py reply`; `hl2rpm_vbsp` drops both at compile. See the doc mapping.md and the skill hl2rpm-hammer.

The user chose "integrate Hammer++ into CK" (not a new editor; a Hammer fork `source_level_editor` is made of leaked code -
refused). CK map window -> "Hammer++ ▾": launch Hammer++, geometry straight from VMF (no compile, rebuilt when Hammer++ saves),
VMF sketches without BSP, task list, snapshots. For me: `vmf_shot.py` (2D/3D/task PNGs + info.json in ~2 s) and
`hammer_shot.py` (the Hammer++ window only). Details: claude_docs/ck-editor.md. CK exe rebuilt.

CLAUDE.md (313 KB) was split: CLAUDE.md is now an index (rules, layout, build, git, table "work -> doc / skill");
knowledge in `sp/src/claude_docs/*.md` (15 topics + voice.md); new skill hl2rpm-voice. Add new knowledge to the topic
file, not to CLAUDE.md. Copies of skills: hl2rpm_extras/claude_skills.

Related: [[hl2rpm-voice-2026-11-04-status]], [[hl2rpm-level-building-guidance]].
