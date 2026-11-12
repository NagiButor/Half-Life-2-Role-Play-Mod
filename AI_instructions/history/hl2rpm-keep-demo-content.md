---
name: hl2rpm-keep-demo-content
description: User wants finished features left on the test map as demo content (in the CK test project), not tested and deleted
metadata:
  type: feedback
---

When a feature is verified in game, leave a working example of it on the test map ck_test, in the user's Creation Kit
test project `E:\SourceModding\HL2RPMSOURCE\dialogsrc/test_dialogs.hl2dlg` (exported to `hl2rpm/testmaps`), instead of
temporary files that are deleted after the check. Back the project up before editing, and keep the generator script in
`hl2rpm_extras/tools` (as `ck_demo_content.py`).

**Why:** 2026-11-01 the user: «хотелось бы ещё видеть результаты твоей работы на тестовой карте, а то ты потестил и всё удалил».

**How to apply:** throwaway helpers (cfg/_*.cfg, debug convars on the command line) still get cleaned up; content that shows
the feature (objects, items, terminals, quests) stays and is listed in CLAUDE.md. CK tests that copy the user's project must
not assume it is empty (t_m3/t_m65 use a pre-demo copy). Related: [[hl2rpm-ck-plan-m1-m2-status]].
