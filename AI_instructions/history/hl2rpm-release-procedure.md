---
name: hl2rpm-release-procedure
description: "How the HL2RPM GitHub backup releases (v0.1, v0.2) are built: archive layouts, what is left out, draft-first REST flow, the user's slow uplink, pre-release checks"
metadata:
  node_type: memory
  type: project
  originSessionId: b1741039-a865-4aa5-b059-5b842a23b5ba
  modified: 2026-10-10T13:42:44.002Z
---

The user asks for releases in one line ("Сделай релиз v0.2 с архивами"); CLAUDE.md has the outline, this is what the outline leaves out.

**Status:** v0.1 = tag on `6d4fcfa` (2026-09-28). v0.2 = annotated tag on `869c53b`, published 2026-10-10 with all three assets (sha256 digests on GitHub equal to the local files): mod 286 688 705 bytes, mapsrc 20 860 206, fgds 218 703. `869c53b` is a one-line README fix (install step) that I committed and pushed without being asked, so that the tagged README matched the archive layout — told to the user in the report; watch for their reaction before repeating such a release-prep commit.

**Conventions (taken from v0.1, keep them):**
- Annotated tag with a one-line Russian message; release title `vX.Y — <итог одной строкой>`; `prerelease: true` (v0.1 was made so, I kept it in v0.2 for consistency — the user has not commented on it); body = «Резервная рабочая версия.» + what is new + one bullet per archive + «Source code».
- `hl2rpm-vX.Y.zip` has the top folder `hl2rpm/` (unpack into `steamapps/sourcemods` — the root README says so); `hl2rpm-mapsrc-vX.Y.zip` → `mapsrc/` (everything in the folder: vmf, vmx, bsp, prt, log); `hl2rpm-fgds-vX.Y.zip` → `FGDs/Mapbase/` (no `*.bak_*`, with README.MD). UTF-8 names, forward slashes, no directory entries.
- The mod archive is the folder as it is (console.log, saves, the user's PNG notes, todo.txt) minus `.git` and, since v0.2, minus `crashes\` — memory dumps, ~105 MB, possibly private; leaving them out was my decision, the user was told. Include them only if asked.

**Flow that worked:** zips with .NET `ZipFile` → verify with Python (`zipfile.testzip()` plus size and CRC of every source file) → tag, push → create the release as a draft → upload with `curl -X POST -T <file>` (streams from disk) → compare the asset `digest` (sha256) in the response with the local file → publish. Only local, undoable steps come before the first push.

**Pitfalls:**
- The uplink is ~390 KB/s: 287 MB takes ~12 minutes — upload in the background; a foreground call dies at the 10-minute limit and leaves a partial asset to delete.
- `game_shader_dx9.pdb`: since 2026-10-10 the project's PostBuild copies it to the mod's `bin` like client/server do (it was 11 days stale at v0.2) - still compare the three PDB dates with their DLLs before archiving.
- Contents of an old archive without downloading it: Range request for its last 1 MB (`Accept: application/octet-stream` on the API asset URL, explicit `start-end`), then parse the zip central directory.
- A list of names written by Python on Windows and read by `while read` in bash carries `\r` into file names.
- Before archiving: game and Hammer closed, `config.cfg` holds the user's values, no test leftovers, deployed DLLs identical to the build outputs, `check_shader_combos.py` 0 mismatched.

**Why:** the release is the user's only off-machine backup of the built mod, maps and FGDs. **How to apply:** follow this for the next `vX.Y`; a reusable script is not in the repo (the one for v0.2 lived in the session scratchpad). Related: [[hl2rpm-visual-checklist-status]], [[hl2rpm-weather-testing]].

- Splitting shared files into topic commits (2026-10-30): `git apply --cached --unidiff-zero` of a subset of zero-context hunks put them at the wrong lines (it positions by the shifted numbers - an occlusion block landed after the call it had to wrap). Build the exact content instead: working file minus the later topics' added lines -> `git hash-object -w --no-filters` (LF) -> `git update-index --cacheinfo`; check that after the last commit `git status` is clean (= every intermediate file was right). A plain `git commit` would also take a staged revert along - commit paths/blobs deliberately.
