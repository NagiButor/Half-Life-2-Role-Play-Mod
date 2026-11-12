"""Garry's Mod addons (.gma) into the mod: models, materials, sounds - not the Lua.

    python import_gma.py <addon.gma | extracted folder> [--only <prefix>...] [--skip <prefix>...]
                         [--force] [--dry] [--list]

- files that already exist in the game (HL2 / episodes / Mapbase content, the mod) are not
  overwritten unless --force: addons often carry their own copies of stock files;
- lua/, spawn icons (materials/spawnicons, materials/entities) and the addon's .json are left out;
- every model copied is checked: MDL version (48 loads; 49 is patched to 48 when the layout
  allows - see fix_mdl_version), materials found through the game's search paths;
- a log of what was copied goes to hl2rpm_extras/imported/<addon>.txt (to undo or to see later).
"""
import os
import shutil
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "dialog_editor"))
from dlgedit.world import gamefs  # noqa: E402

MOD = r"E:\Steam\steamapps\sourcemods\hl2rpm"
SDK = r"E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer"
LOGDIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "imported")
SKIP = ("lua/", "materials/spawnicons/", "materials/entities/", "addon.json", "data/")


def read_cstr(f):
    b = bytearray()
    while True:
        c = f.read(1)
        if not c or c == b"\0":
            return b.decode("utf-8", "replace")
        b += c


def gma_files(path):
    """[(name, bytes)] of a .gma"""
    out = []
    with open(path, "rb") as f:
        assert f.read(4) == b"GMAD", "not a gma"
        ver = f.read(1)[0]
        f.read(16)
        if ver > 1:
            while read_cstr(f):
                pass
        name = read_cstr(f)
        read_cstr(f)
        read_cstr(f)
        f.read(4)
        entries = []
        while struct.unpack("<I", f.read(4))[0]:
            fn = read_cstr(f)
            size = struct.unpack("<q", f.read(8))[0]
            f.read(4)
            entries.append((fn, size))
        for fn, size in entries:
            out.append((fn.replace("\\", "/"), f.read(size)))
    return name, out


def folder_files(path):
    out = []
    for root, _, files in os.walk(path):
        for fn in files:
            p = os.path.join(root, fn)
            rel = os.path.relpath(p, path).replace("\\", "/")
            with open(p, "rb") as f:
                out.append((rel, f.read()))
    return os.path.basename(path.rstrip("/\\")), out


def fix_mdl_version(data):
    """a note for versions this engine does not load (v49 is left out before: see main). Returns (data, note)."""
    ver = struct.unpack_from("<i", data, 4)[0]
    if ver not in (44, 45, 46, 47, 48):
        return data, "version %d (not loaded by this engine)" % ver
    return data, ""


def fix_vtf_version(data):
    """VTF 7.5 (newer VTFEdit) -> 7.4: "Error reading texture header" in this engine; the layout of
    7.5 is that of 7.4 (a resource dictionary after a 96 byte header)."""
    if data[:4] == b"VTF\0" and struct.unpack_from("<II", data, 4) == (7, 5):
        return data[:8] + struct.pack("<I", 4) + data[12:], "VTF 7.5 -> 7.4"
    return data, ""


def model_materials(data):
    """texture names x cdmaterials of an MDL"""
    def cstr(o):
        e = data.find(b"\0", o)
        return data[o:e].decode("utf-8", "replace")
    ntex, texidx, ncd, cdidx = struct.unpack_from("<iiii", data, 204)
    tex = []
    for i in range(ntex):
        b = texidx + i * 64
        tex.append(cstr(b + struct.unpack_from("<i", data, b)[0]).replace("\\", "/").lower())
    cds = [cstr(struct.unpack_from("<i", data, cdidx + i * 4)[0]).replace("\\", "/").lower().strip("/") for i in range(ncd)]
    return tex, cds


def main(argv):
    if not argv:
        print(__doc__)
        return
    src = argv[0]
    only = [argv[i + 1] for i, a in enumerate(argv) if a == "--only"]
    skip = [argv[i + 1] for i, a in enumerate(argv) if a == "--skip"]
    force, dry, listing = "--force" in argv, "--dry" in argv, "--list" in argv
    name, files = gma_files(src) if src.lower().endswith(".gma") else folder_files(src)
    fs = gamefs.GameFS(MOD, SDK)
    copied, kept, skipped, notes = [], [], [], []
    # MDL v49 is not only a number: its .vtx has longer strip group headers - rebuilt by make_characters.py
    v49 = {rel.lower()[:-4] for rel, data in files if rel.lower().endswith(".mdl") and struct.unpack_from("<i", data, 4)[0] >= 49}
    for rel, data in files:
        low = rel.lower()
        if low.startswith("models/") and low.split(".")[0] in v49:
            if low.endswith(".mdl"):
                notes.append("%s: MDL v49 - not copied, rebuild it (mdl_decompile.py / make_characters.py)" % rel)
            skipped.append(rel)
            continue
        if low.startswith(SKIP) or not low.startswith(("models/", "materials/", "sound/", "particles/", "scripts/", "resource/")):
            skipped.append(rel)
            continue
        if only and not any(low.startswith(o.lower()) for o in only):
            continue
        if skip and any(low.startswith(s.lower()) for s in skip):
            skipped.append(rel)
            continue
        if fs.exists(low) and not force:
            kept.append(rel)
            continue
        if low.endswith(".mdl"):
            data, note = fix_mdl_version(data)
            if note:
                notes.append("%s: %s" % (rel, note))
        if low.endswith(".vtf"):
            data, note = fix_vtf_version(data)
            if note:
                notes.append("%s: %s" % (rel, note))
        if listing:
            print(rel)
        if not dry:
            dst = os.path.join(MOD, rel.replace("/", os.sep))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as f:
                f.write(data)
        copied.append(rel)
    # materials of the models
    fs = gamefs.GameFS(MOD, SDK)
    missing = []
    for rel, data in files:
        if not rel.lower().endswith(".mdl") or rel not in copied:
            continue
        tex, cds = model_materials(data)
        for t in tex:
            if not any(fs.exists("materials/%s/%s.vmt" % (cd, t) if cd else "materials/%s.vmt" % t) for cd in (cds or [""])):
                if not any(r.lower() == ("materials/%s/%s.vmt" % (cd, t)).lower() for cd in cds for r, _ in files):
                    missing.append("%s: %s (in %s)" % (rel, t, ", ".join(cds)))
    print("%s: %d copied, %d left as they are (already in the game), %d skipped (lua, icons...)" % (name, len(copied), len(kept), len(skipped)))
    for n in notes:
        print("  " + n)
    if kept:
        print("  kept the game's own:", ", ".join(kept[:12]) + (" ..." if len(kept) > 12 else ""))
    if missing:
        print("  MISSING materials:")
        for m in missing[:40]:
            print("    " + m)
    if not dry:
        os.makedirs(LOGDIR, exist_ok=True)
        safe = "".join(c if c.isalnum() or c in "-_." else "_" for c in os.path.splitext(os.path.basename(src))[0])[:80]
        with open(os.path.join(LOGDIR, safe + ".txt"), "w", encoding="utf-8") as f:
            f.write("# %s -> %s\n" % (src, MOD))
            f.write("\n".join(copied) + "\n")
            if kept:
                f.write("# kept (already in the game):\n# " + "\n# ".join(kept) + "\n")
            if missing:
                f.write("# missing materials:\n# " + "\n# ".join(missing) + "\n")


if __name__ == "__main__":
    main(sys.argv[1:])
