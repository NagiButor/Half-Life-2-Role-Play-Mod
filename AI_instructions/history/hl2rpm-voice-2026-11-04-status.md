---
name: hl2rpm-voice-2026-11-04-status
description: "NPC voice-over (2026-11-03/04) - ck_test fully voiced with Fish Audio (free s2.1-pro-free, cloned HL2 voices + designed voices, emotion cues); Qwen3-TTS on Colab is the fallback; what failed and the traps; NOT committed"
metadata:
  node_type: memory
  type: project
  originSessionId: 687673d6-217f-430a-8587-89acd53b9a64
  modified: 2026-11-04T01:23:42.159Z
---

Done 2026-11-04: every speaking character of ck_test is voiced with **Fish Audio** (121 lines / 195 places, in game,
checked). Pipeline, decisions and traps: `sp/src/claude_docs/voice.md`; procedure: skill `hl2rpm-voice`. CK got topic
voices (`Topic.voices`, `topic_greeting_voices`), `dlgedit/voiceattach.py`, per-voice copies of the citizens' bark set,
ck_demo_content part 9. Archive of the Qwen tries + comparison page: `backups\voice_tests_2026-11-04.zip`.

Open with the user: whether Fish follows Russian stress marks (listening page `voice_gen\fish_stress_check.html` - an
energy measure could not tell); UI / RPG sound effects (Fish has no SFX - offered own synthesis or open SFX models on
Colab); nothing committed.

Key facts: Fish key `FISH_AUDIO_API_KEY` (the user once set a variable whose NAME was the key; it leaked when I listed
env vars by pattern - read keys by exact name only); free Colab T4 runs out per day ("Service Unavailable"); ElevenLabs
geo-blocked, Alibaba needs an ID, voicy CUDA 13 build crashes on T4 / 960M, Kaggle and local CPU dropped by the user.

Related: [[gemini-image-gen]], [[hl2rpm-keep-demo-content]], [[hl2rpm-hammer-ck-2026-11-04-status]].
