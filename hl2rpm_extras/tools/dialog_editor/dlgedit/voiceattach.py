"""Pick up voice files that lie at their canonical places in the game folder
(sound/dialogs/<npc or barks_<set>>/<language>/<key>.wav) and attach them to what has no voice yet:
dialog lines, returning lines, after-lines, shared topics and the topic greeting (in that NPC's voice),
the NPC's barks and bark sets. The export then converts them if needed and makes the lip sync.
Used by voice generation tools (voice_gen) and the ck_test demo generator."""
import os

from . import audio, model, sounddata


def _attach(gd, owner, lang, key, text, voices, report):
    if not voices.get(lang) or voices[lang].is_empty():
        rel = model.canonical_wav(owner, lang, key)
        wav = os.path.join(gd, rel)
        if os.path.exists(wav):
            v = model.Voice()
            v.wav = rel
            vcd = model.canonical_vcd(owner, lang, key)
            if os.path.exists(os.path.join(gd, vcd)):  # its lip sync scene is there too: the export keeps it
                v.vcd = vcd
                v.lips_text = text
                v.lips_audio = audio.audio_hash(wav)
            voices[lang] = v
            report.append(rel)


def attach_canonical_voices(project, npc_names=None, bark_set_ids=None):
    """Returns the list of attached wav paths (game-relative)."""
    from .exporter import Exporter
    from .world_export import bark_owner
    p = project
    gd = p.settings.game_dir
    ex = Exporter(p)
    out = []
    for npc in p.npcs:
        if npc_names is not None and npc.targetname not in npc_names:
            continue
        if getattr(npc.actor, "is_template", False):
            continue
        name = npc.targetname
        for key, texts, voices, where in model.iter_voiced_texts(npc):
            for lang in p.languages:
                if (texts or {}).get(lang):
                    _attach(gd, name, lang, key, texts[lang], voices, out)
        # topics / greeting: with_topics hands out the project's own voice dicts for this NPC
        n2 = ex.with_topics(npc)
        if n2 is not npc:
            for nd in n2.nodes:
                if getattr(nd, "topic", ""):
                    for lang in p.languages:
                        if nd.line.get(lang):
                            _attach(gd, name, lang, model.media_key_node(nd.id), nd.line[lang], nd.voice, out)
        owner = bark_owner(p, npc)
        for b in npc.actor.barks or []:
            for lang in p.languages:
                if b.text.get(lang):
                    _attach(gd, owner, lang, sounddata.bark_media_key(b.id), b.text[lang], b.voices, out)
    for s in p.bark_sets:
        if bark_set_ids is not None and s.id not in bark_set_ids:
            continue
        for b in s.barks:
            for lang in p.languages:
                if b.text.get(lang):
                    _attach(gd, "barks_" + s.id, lang, sounddata.bark_media_key(b.id), b.text[lang], b.voices, out)
    return out
