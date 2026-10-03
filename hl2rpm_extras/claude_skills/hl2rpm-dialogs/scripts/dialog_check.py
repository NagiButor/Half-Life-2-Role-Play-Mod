#!/usr/bin/env python3
"""Validate HL2RPM dialog scripts (scripts/dialogs/*.txt) and show their structure.

    python dialog_check.py                          all dialogs of the mod
    python dialog_check.py demo_npc_1.txt ...       given files (name or path)
    python dialog_check.py --tree demo_npc_1.txt    print the node graph
    python dialog_check.py --map <map.vmf|map.bsp>  also check that every dialog NPC exists on that map

Checks: root "Dialog", "entity", nodes, start_node, every next/auto_next/
after_next/returning_auto_next target, quest ids used by options exist in some
dialog's "quests" block, quest states (inactive/active/completed|done/failed),
characters the client payload can't carry (| ; ~ in lines and option texts),
option text <= 255 bytes (UTF-8), choreo .vcd files present in the mod,
the same entity defined by two files (the later file wins, e.g. *_rus.txt).
Exit code 1 if there are errors (warnings don't count).
"""
import glob
import os
import re
import sys

MOD = r"E:\Steam\steamapps\sourcemods\hl2rpm"
DIALOGS = os.path.join(MOD, "scripts", "dialogs")

# ---------------------------------------------------------------------------
# KeyValues (KV1) parser: quoted/unquoted tokens, // comments, [$COND] tags
# ---------------------------------------------------------------------------
_TOK = re.compile(r'"((?:[^"\\]|\\.)*)"|(\{)|(\})|(//[^\n]*)|(\[[^\]]*\])|([^\s{}"]+)')


class KV:
    def __init__(self, name, value=None):
        self.name = name
        self.value = value          # str for leaves
        self.children = []          # list of KV for blocks

    def get(self, key, default=None):
        for c in self.children:
            if c.name.lower() == key.lower() and c.value is not None:
                return c.value
        return default

    def block(self, key):
        for c in self.children:
            if c.name.lower() == key.lower() and c.value is None:
                return c
        return None

    def blocks(self):
        return [c for c in self.children if c.value is None]


def parse_kv(text):
    root = KV("<root>")
    stack = [root]
    key = None
    for m in _TOK.finditer(text):
        q, lb, rb, comment, cond, bare = m.groups()
        if comment or cond:
            continue
        if lb:
            node = KV(key if key is not None else "")
            stack[-1].children.append(node)
            stack.append(node)
            key = None
        elif rb:
            if len(stack) > 1:
                stack.pop()
            key = None
        else:
            tok = q if q is not None else bare
            if key is None:
                key = tok
            else:
                stack[-1].children.append(KV(key, tok))
                key = None
    return root


def read_text(path):
    raw = open(path, "rb").read()
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return raw.decode("utf-16")
    if raw[:3] == b"\xef\xbb\xbf":
        raw = raw[3:]
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1251")


# ---------------------------------------------------------------------------
QUEST_STATES = {"inactive", "active", "completed", "done", "failed"}
BAD_CHARS = "|;~"


def check_file(path, all_quests, problems, show_tree):
    name = os.path.basename(path)
    root = parse_kv(read_text(path))
    dlg = next((c for c in root.children if c.value is None), None)

    def err(msg):
        problems.append(("ERROR", name, msg))

    def warn(msg):
        problems.append(("warn", name, msg))

    if dlg is None or dlg.name.lower() != "dialog":
        err('root block must be "Dialog"')
        return None
    entity = dlg.get("entity") or os.path.splitext(name)[0]
    nodes_kv = dlg.block("nodes")
    if nodes_kv is None:
        err('missing "nodes" block')
        return entity
    nodes = {}
    for n in nodes_kv.blocks():
        if not re.fullmatch(r"-?\d+", n.name):
            warn("node %r: name is not a number, ignored by the game" % n.name)
            continue
        nodes[int(n.name)] = n

    def target_ok(v, where):
        try:
            t = int(v)
        except (TypeError, ValueError):
            err("%s: %r is not a node id" % (where, v))
            return
        if t != -1 and t not in nodes:
            err("%s -> node %d does not exist" % (where, t))

    start = dlg.get("start_node", "0")
    target_ok(start, "start_node")
    if dlg.get("returning_auto_next") not in (None, ""):
        target_ok(dlg.get("returning_auto_next"), "returning_auto_next")

    def text_ok(s, where, limit=None):
        if s is None:
            return
        bad = [c for c in BAD_CHARS if c in s]
        if bad:
            err("%s contains %s (breaks the client payload)" % (where, " ".join(repr(c) for c in bad)))
        if limit and len(s.encode("utf-8")) > limit:
            err("%s is %d bytes, max %d" % (where, len(s.encode("utf-8")), limit))

    text_ok(dlg.get("returning_line"), "returning_line")

    def choreo_ok(v, where):
        if v:
            p = os.path.join(MOD, v.replace("/", os.sep))
            if not p.lower().endswith(".vcd"):
                p += ".vcd"
            if not os.path.exists(p):
                warn("%s: %s not found in the mod (only loose .vcd files are checked)" % (where, v))

    choreo_ok(dlg.get("returning_choreo"), "returning_choreo")

    used_quests = []
    for nid, n in sorted(nodes.items()):
        w = "node %d" % nid
        text_ok(n.get("line"), w + " line")
        choreo_ok(n.get("choreo"), w + " choreo")
        if n.get("auto_next") not in (None, ""):
            target_ok(n.get("auto_next"), w + " auto_next")
        opts = n.block("options")
        if opts is None:
            if n.get("auto_next") in (None, "", "-1") and n.get("end", n.get("close", n.get("auto_close", "0"))) in ("0", None):
                warn("%s has no options, no auto_next and no end: the dialog waits for nothing" % w)
            continue
        for o in opts.blocks():
            ow = "%s option %s" % (w, o.name)
            text_ok(o.get("text"), ow + " text", 255)
            if o.get("next") is not None:
                target_ok(o.get("next"), ow + " next")
            if o.get("after_next") is not None:
                target_ok(o.get("after_next"), ow + " after_next")
            for key in ("grant_quest", "complete_quest", "fail_quest", "require_quest"):
                if o.get(key):
                    used_quests.append((o.get(key), ow + " " + key))
            for key in ("set_stage", "require_quest_stage", "require_quest_state"):
                v = o.get(key)
                if not v:
                    continue
                if ":" not in v:
                    err("%s %s=%r must be quest_id:value" % (ow, key, v))
                    continue
                qid, val = v.split(":", 1)
                used_quests.append((qid, ow + " " + key))
                if key == "require_quest_state" and val.lower() not in QUEST_STATES:
                    err("%s require_quest_state %r: state must be one of %s" % (ow, val, "/".join(sorted(QUEST_STATES))))
                if key != "require_quest_state" and not re.fullmatch(r"-?\d+", val):
                    err("%s %s %r: stage must be a number" % (ow, key, val))
            if o.get("after_line") is not None or o.get("after_next") is not None:
                if not o.get("complete_quest"):
                    warn("%s: after_line/after_next only work together with complete_quest" % ow)

    q = dlg.block("quests")
    if q is not None:
        for qd in q.blocks():
            all_quests[qd.name.lower()] = name
            stages = qd.block("stages")
            if stages is None:
                warn("quest %s has no stages" % qd.name)
    for qid, where in used_quests:
        problems.append(("quest-ref", name, (qid.lower(), where)))

    if show_tree:
        print("== %s  (entity %s, start %s)" % (name, entity, start))
        for nid, n in sorted(nodes.items()):
            line = (n.get("line") or "").replace("\n", " ")
            print("  [%d] %s" % (nid, line[:90]))
            if n.get("auto_next") not in (None, "", "-1"):
                print("        auto -> %s" % n.get("auto_next"))
            opts = n.block("options")
            for o in (opts.blocks() if opts else []):
                cond = [k for k in ("require_rep", "require_quest_state", "require_quest_stage", "require_quest") if o.get(k)]
                act = [k + "=" + o.get(k) for k in ("grant_quest", "complete_quest", "fail_quest", "set_stage") if o.get(k)]
                print("        (%s) %s -> %s %s %s" % (o.name, (o.get("text") or "")[:60], o.get("next", "-1"),
                                                   ("if " + ",".join(o.get(c) for c in cond)) if cond else "",
                                                   " ".join(act)))
    return entity


def map_targetnames(path):
    names = set()
    data = open(path, "rb").read().decode("utf-8", errors="replace")
    for m in re.finditer(r'"targetname"\s+"([^"]*)"', data):
        names.add(m.group(1).lower())
    return names


def main(argv):
    show_tree = "--tree" in argv
    argv = [a for a in argv if a != "--tree"]
    map_path = None
    if "--map" in argv:
        i = argv.index("--map")
        map_path = argv[i + 1]
        del argv[i:i + 2]
    files = []
    for a in argv[1:]:
        files.append(a if os.path.exists(a) else os.path.join(DIALOGS, a))
    if not files:
        files = sorted(glob.glob(os.path.join(DIALOGS, "*.txt")))
    # quests are global: collect them from every dialog of the mod
    all_files = sorted(glob.glob(os.path.join(DIALOGS, "*.txt")))
    problems = []
    all_quests = {}
    entities = {}
    for f in all_files:
        if f in files or os.path.abspath(f) in [os.path.abspath(x) for x in files]:
            continue
        sub = []
        e = check_file(f, all_quests, sub, False)
        entities.setdefault((e or "").lower(), []).append(os.path.basename(f))
    for f in files:
        e = check_file(f, all_quests, problems, show_tree)
        entities.setdefault((e or "").lower(), []).append(os.path.basename(f))

    out = []
    for kind, fname, msg in problems:
        if kind == "quest-ref":
            qid, where = msg
            if qid not in all_quests:
                out.append(("ERROR", fname, "%s: quest %r is not defined in any dialog" % (where, qid)))
        else:
            out.append((kind, fname, msg))
    for ent, fl in entities.items():
        if len(fl) > 1:
            out.append(("warn", ",".join(fl), "entity %r defined %d times: the last loaded file wins" % (ent, len(fl))))
    if map_path:
        names = map_targetnames(map_path)
        for ent, fl in entities.items():
            if ent and ent not in names:
                out.append(("info", ",".join(fl), "no entity named %r on %s" % (ent, os.path.basename(map_path))))

    for kind, fname, msg in out:
        print("%-5s %-28s %s" % (kind, fname, msg))
    n_err = sum(1 for k, _, _ in out if k == "ERROR")
    print("%d file(s), %d quest(s), %d error(s), %d other" % (len(files), len(all_quests), n_err, len(out) - n_err))
    return 1 if n_err else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
