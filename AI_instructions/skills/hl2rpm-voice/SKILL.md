---
name: hl2rpm-voice
description: Voice the NPCs of the HL2RPM Creation Kit project - dialog lines, returning lines, shared topics and their greeting, NPC barks and bark sets - with cloned HL2 voices (Civil Protection, Combine, citizens) and voices made by description, through Fish Audio (main) or Qwen3-TTS on Colab (fallback); radio/respirator processing, placing the files and lip sync through the CK export, checking them in the game. Use when asked to voice characters, redo a voice, add a voice for a new NPC, or compare TTS engines.
---

# HL2RPM: voicing NPCs

This procedure works with any AI model. Read `../../PROJECT.md` and
`../../CURRENT_STATUS.md`; background, decisions and traps: `../../topics/voice.md`.
Resolve source paths relative to `sp/src`; run the generation commands below from
`voice_gen`, using its indicated Python environment. No Claude-specific tool is needed.
Working folder (not git): `E:\SourceModding\HL2RPMSOURCE\voice_gen`; scripts are also
in git: `sp/src/hl2rpm_extras/tools/voice/` (copy them into `voice_gen` if missing).

## Keys
- Fish Audio: `FISH_AUDIO_API_KEY` in HKCU\Environment (the user sets it). Read it by the
  exact name, only into the `Authorization` header; never print it or list env vars by pattern.
- Colab (fallback): the colab CLI in `voice_gen\colab\venv`, token in `~/.config/colab-cli`.

## Procedure (ck_test example)
1. **Who says what**: `python cktest_build.py` — walks the CK project, writes
   `job_cktest/job.json` (unique lines) + `targets.json` (every place a line goes:
   owner, language, key, post-processing). Voices per NPC: tables `NPC_PROFILE`,
   `COMBINE`, `SET_PROFILE` at the top; spoken forms (`ГО` -> `гэ-о`, codes, times): `SPEAK`.
   A new voice: a reference clip of 10-20 s + its transcript in `job*/refs` and `job.json`
   (stock HL2 voices: build from the VPKs — `cktest_build.NEW_REFS`, the Russian dub has CP,
   Overwatch, soldiers, citizens; a new character: VoiceDesign on Colab or ask the user for a sample).
2. **Fish spoken form with emotions**: `python fish_cktest_build.py` -> `job_cktest_fish/`
   (cues `[angry]`, `[shouting]`, ... chosen by bark event / text — tables `BARK_CUE`, `TEXT_CUE`).
3. **Generate**: `voice_gen\fish_venv\Scripts\python.exe fish_job.py job_cktest_fish cktest_fish\raw`
   (free model `s2.1-pro-free`, ~3-15 s a line, already done files are skipped; run in the background).
   Fallback: `python colab\drive.py <job dir> <results>` (Qwen3-TTS; see voice.md for its traps).
4. **Into the game and the project**: `python cktest_apply.py --job job_cktest_fish --raw cktest_fish\raw`
   (loudness, radio for CP/Combine, respirator, canonical files in testmaps, attach, CK export = lip sync).
   Then `ck_demo_content.py --export` (CK venv python) must still report `voices attached` and
   only a few lip syncs (it keeps the voices on re-runs).
5. **Check in the game** (`hl2rpm-playtest`): steps `developer 1; snd_showstart 1`,
   `rpm_bark <npc> greet`, `dialog_force <npc>`; console.log must show
   `DynamicSound dialogs\<npc>\<lang>\<key>.wav` and the scene `scenes\dialogs\...vcd`.
6. **Listening page for the user** before replacing anything big (`compare_page.py`, `colab/post.py`),
   and an archive of what is replaced (`E:\SourceModding\HL2RPMSOURCE\backups`).
