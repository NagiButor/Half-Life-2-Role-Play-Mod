---
name: hl2rpm-original-mechanisms
description: "When an asset doesn't work through the engine's own path, fix the asset (re-encode, rebuild) rather than writing a custom replacement player/system - the user rejects \"crutches\""
metadata:
  node_type: memory
  type: feedback
  originSessionId: 687673d6-217f-430a-8587-89acd53b9a64
  modified: 2026-11-02T16:10:43.653Z
---

On 2026-11-02 the user's intro (4K Bink) did not play in SDK 2013; I wrote a custom frame format + player in the main menu. The user: "Да не чувак, хуита какая-то. Надо без твоих костылей чтобы как в оригинале было" - he wants the stock mechanism (the engine's startup `media/valve.bik`), with the asset converted to what the engine accepts.

**Why:** a custom path is unfamiliar to him, behaves differently from the original (plays after load instead of during it) and is code to maintain for a one-off asset.

**How to apply:** when an asset fails in the engine, first find the format the engine needs and convert the asset (tools may need the user's permission to install - ask early, prepare the inputs); only propose a custom system if no conversion is possible, and ask before building it. Related: [[hl2rpm-menu-content-2026-11-02-status]].
