---
name: hl2rpm-menu-content-2026-11-02-status
description: "2026-11-02: menu/Steam/addons request, then the second batch (folded suit items, worker+hazmat suits with own work HUD, hands per suit for all weapons, graffiti lambda, new icon + game.ico, loading valve, hazmat zombie) - done and verified, NOT committed"
metadata:
  node_type: memory
  type: project
  originSessionId: 687673d6-217f-430a-8587-89acd53b9a64
  modified: 2026-11-02T18:15:07.229Z
---

Two requests of 2026-11-02, all in CLAUDE.md section "Главное меню, Steam, контент из аддонов (2026-11-02)"; nothing committed (user asks for commits himself).

First batch: Portal-2-style menu + synthesized OST, Steam art, GMod addons (HEV Mk V, beta units, workers, conceptbine, melee pack with hands, zombie skins, props).

Second batch (evening), verified in game with screenshots:
- suit items are folded piles + helmet + flattened torso armour (+ hazmat tank): `item_models/make_suit_piles.py`, 8 models;
- `item_suit_worker` / `item_suit_hazmat` with themes 5/6 and a deliberately non-combat "device" HUD + self-test boot;
- stock HL2 view models: arms recoloured per suit (`make_vm_hands.py` + client `hl2rpm_vmhands.cpp`); found and fixed a deferred-shader bug (stage command cache missed var changes after the first draw -> `NeedsCommands`);
- graffiti lambda (cut out of a fake checkerboard) without the glow halo; user's icon -> game.ico/Steam; red valve wheel spinner on the loading screen (user corrected me once: the reference valve is hexagonal with lobes, not round);
- hazmat zombie made from the classic zombie (`make_zombie_hazmat.py`), zombie body shares in `hl2rpm_zombies.cpp`; ck_test demo part 4 refs 110-115. The user rejected the first version (suit helmet poked through the headcrab, rubber gloves instead of claws) and pointed at the addon worker zombie: suit = clothes only, the head under the crab and the clawed bloody hands are the zombie's. Rebuilt that way, verified in game (with and without the crab). Second round: only the bloody skin, no gap at the wrists (zombie forearm flesh out of the sleeve, glove cuffs gone), and it walks now - decompiled animations had no root motion, the model takes $includemodel zombie/classic.mdl; verified walking next to a stock zombie.

**Intro (done later the same evening):** the user installed RAD Video Tools to `E:\RADVideo`; the intro is Bink 1 2560x1440 with sound (`radvideo64 binkc ... /Bink1` - without it binkc writes Bink 2, which SDK 2013 can't read); the engine plays it at startup, verified. The user asked why 1080p and not 2K: 2K works, so 2K is used.

**Why:** next session should not redo this or assume it is committed. **How to apply:** for a commit include sp/src + hl2rpm_extras (art/, tools, FGD copies, README); models/textures/sounds stay out of git. Related: [[hl2rpm-ck-plan-m1-m2-status]], [[hl2rpm-keep-demo-content]], [[hl2rpm-original-mechanisms]].
