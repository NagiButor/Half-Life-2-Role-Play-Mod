---
name: feedback-heredoc-escapes
description: "In this environment a Bash heredoc that feeds Python turns \"\\\\n\" / \"\\\\t\" / \"\\\\f\" inside the Python source into real control characters - edits with escapes must go through the Edit/Write tools"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 687673d6-217f-430a-8587-89acd53b9a64
  modified: 2026-11-03T07:56:00.962Z
---

Python snippets run as `python - <<'EOF' ... EOF` through the Bash tool lose escape sequences: a `"\\n"` meant to stay `\n` in a C++ or Python string arrives as a real newline (also `\\t`, `"...\\f_..."` -> form feed in a path). It broke C++ `Msg(...)` strings and Python files four times on 2026-11-03.

**Why:** the tool's command transport unescapes once more than expected, even with a quoted heredoc.

**How to apply:** for any replacement text that contains backslash escapes, use the Edit tool (or Write a script file and run it). Heredoc Python is fine only for text without backslashes. After a heredoc edit, grep the touched line with `cat -A` if it had escapes. Related: [[hl2rpm-weather-testing]].
