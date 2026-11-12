---
name: gemini-image-gen
description: "Image generation for HL2RPM via Google Gemini (Nano Banana 2.1): hl2rpm_extras/tools/gen_image.py, key in GEMINI_API_KEY (user registry env), billing state"
metadata:
  node_type: memory
  type: reference
  originSessionId: 687673d6-217f-430a-8587-89acd53b9a64
  modified: 2026-11-02T18:38:19.656Z
---

2026-11-02: the user set `GEMINI_API_KEY` with `setx` (HKCU\Environment; a session started before that does not see it in the env - `gen_image.py` reads the registry too). Never print or write the key; it goes only into the `x-goog-api-key` header.

`hl2rpm_extras/tools/gen_image.py` (stdlib only): `--list` shows image models; default model `gemini-nano-banana-2.1` ("Nano Banana 2.1"; also available: gemini-3.1-flash-image = Nano Banana 2, gemini-3-pro-image = Pro, gemini-3.1-flash-lite-image, gemini-2.5-flash-image). Options `--aspect`, `--size 1K|2K|4K`, `--ref` (edit / follow a picture), `--prompt-file`.

State on 2026-11-02: the key's project was on the FREE tier - every image model answered 429 RESOURCE_EXHAUSTED with `limit: 0`; the user has to enable billing for the project in AI Studio (a Google AI Pro subscription does not cover the API). Check with one cheap 1K request before a batch.

I recommended my own script over third-party MCP servers (their model IDs go stale, and they hold the key); the user went along and saved the key as I suggested. Related: [[hl2rpm-original-mechanisms]].
