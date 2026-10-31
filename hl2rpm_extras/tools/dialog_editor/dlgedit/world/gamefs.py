"""The game's file system as the engine sees it: the search paths of the mod's
gameinfo.txt (folders and VPKs, in order), so the editor finds every material,
texture and model the game would load."""
import glob
import os

from .. import kv
from .vpk import VPK, dir_name


class GameFS:
    def __init__(self, mod_dir, sdk_dir, extra_dirs=()):
        """mod_dir: the folder with gameinfo.txt; sdk_dir: Source SDK Base 2013
        Singleplayer (|all_source_engine_paths|); extra_dirs come first (the
        project's game folder, e.g. testmaps)."""
        self.mod_dir = os.path.normpath(mod_dir) if mod_dir else ""
        self.sdk_dir = os.path.normpath(sdk_dir) if sdk_dir else ""
        self.sources = []       # ("dir", path) or ("vpk", VPK)
        self._dir_cache = {}
        seen = set()

        def add_dir(p):
            p = os.path.normpath(p)
            if os.path.isdir(p) and p.lower() not in seen:
                seen.add(p.lower())
                self.sources.append(("dir", p))

        def add_vpk(p):
            d = dir_name(p)
            if os.path.isfile(d) and d.lower() not in seen:
                seen.add(d.lower())
                try:
                    self.sources.append(("vpk", VPK(d)))
                except Exception:
                    pass

        for d in extra_dirs:
            if d:
                add_dir(d)
        gi = os.path.join(self.mod_dir, "gameinfo.txt")
        paths = []
        if os.path.isfile(gi):
            try:
                root = kv.parse_file(gi)
                g = root.block("gameinfo") or (root.blocks()[0] if root.blocks() else None)
                fs = g.block("filesystem") if g else None
                sp = fs.block("searchpaths") if fs else None
                if sp:
                    for c in sp.values():
                        keys = c.name.lower().split("+")
                        if "game" in keys or "mod" in keys:
                            paths.append(c.value)
            except Exception:
                paths = []
        if not paths:
            paths = ["|gameinfo_path|.", "|all_source_engine_paths|hl2/hl2_textures.vpk",
                     "|all_source_engine_paths|hl2/hl2_misc.vpk", "|all_source_engine_paths|hl2"]
        for p in paths:
            if "|gameinfo_path|" in p:
                p = p.replace("|gameinfo_path|", self.mod_dir + os.sep)
            elif "|all_source_engine_paths|" in p:
                p = p.replace("|all_source_engine_paths|", self.sdk_dir + os.sep)
            elif not os.path.isabs(p):
                p = os.path.join(self.sdk_dir, p)
            p = os.path.normpath(p)
            if p.endswith("*"):
                for sub in sorted(glob.glob(p)):
                    if sub.lower().endswith("_dir.vpk"):
                        add_vpk(sub)
                    elif os.path.isdir(sub):
                        add_dir(sub)
            elif p.lower().endswith(".vpk"):
                add_vpk(p)
            else:
                add_dir(p)

    # ---------------------------------------------------------------- lookup
    def find(self, rel):
        """("dir", absolute path) / ("vpk", VPK) of the first source with the file."""
        rel = rel.replace("\\", "/").lstrip("/")
        low = rel.lower()
        for kind, src in self.sources:
            if kind == "dir":
                p = os.path.join(src, rel)
                if os.path.isfile(p):
                    return kind, p
            elif src.has(low):
                return kind, src
        return None, None

    def exists(self, rel):
        return self.find(rel)[0] is not None

    def read(self, rel):
        kind, src = self.find(rel)
        if kind == "dir":
            with open(src, "rb") as f:
                return f.read()
        if kind == "vpk":
            return src.read(rel.replace("\\", "/").lstrip("/"))
        return None

    def abs_path(self, rel):
        kind, src = self.find(rel)
        return src if kind == "dir" else ""

    def listdir(self, prefix, ext=None):
        """Every file under prefix ("models/props_c17/") in all sources, sorted."""
        prefix = prefix.replace("\\", "/").lower().rstrip("/") + "/"
        out = set()
        for kind, src in self.sources:
            if kind == "vpk":
                for n in src.names():
                    if n.startswith(prefix) and (ext is None or n.endswith(ext)):
                        out.add(n)
            else:
                base = os.path.join(src, prefix)
                if not os.path.isdir(base):
                    continue
                for dp, dn, fn in os.walk(base):
                    for f in fn:
                        if ext is None or f.lower().endswith(ext):
                            out.add(os.path.relpath(os.path.join(dp, f), src).replace("\\", "/").lower())
        return sorted(out)

    def describe(self):
        return ["%s: %s" % (k, s if k == "dir" else s.dir_path) for k, s in self.sources]
