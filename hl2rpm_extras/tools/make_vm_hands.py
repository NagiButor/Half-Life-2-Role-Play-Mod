# -*- coding: utf-8 -*-
"""The arms of the stock Half-Life 2 view models in the colours of every suit.

The view models of HL2 (pistol, SMG, crowbar...) carry their arms in their own mesh - an H.E.V.
glove and sleeve, one texture for all of them: models/weapons/v_hand/v_hand_sheet. Most of them
have no arm skeleton a separate hand model could be bone-merged to (the right arm of the SMG, both
arms of the .357, the crossbow, the RPG... are bound to the weapon's bones), so the melee weapons'
way (hand_viewmodel, make_hands.py) cannot work there. Instead the sheet is repainted per suit and
the client swaps the textures of that one material (client/hl2rpm_vmhands.cpp):

- the parts of the sheet: the UV triangles of v_hand_sheet in all stock view models -> glove
  (the hand, the strap), cuff (the knitted band), sleeve (the forearm panels), plate (the orange
  armour of the H.E.V.);
- the colours of a suit: its first-person hands model (the hands of the melee weapons - cut out of
  the suit's body, or the Garry's Mod hands of an addon): the triangles of the hand / of the forearm
  in its texture -> the biggest square inside them -> a patch, tiled (mirrored) over the part;
- the creases, seams and scratches of the original stay: its brightness over a blurred copy of it
  multiplies the patch (more on the leather of a glove, less on bare skin); the normal map is the
  original's, flattened where the new material is smooth (skin, rubber).

    python make_vm_hands.py [suits...] [--preview out.png]

-> materials/models/weapons/v_hand/hl2rpm/v_hand_sheet_<suit>.vtf (+ _normal), DXT1 / DXT5.
"""
import io
import math
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "dialog_editor"))
sys.path.insert(0, os.path.join(HERE, "pom"))
import hd_textures  # noqa: E402  (DXT VTF writer)
import make_menu_assets as mm  # noqa: E402  (blur, smoothstep)
import mdl_decompile as md  # noqa: E402
import vtf as pvtf  # noqa: E402
from dlgedit.world import gamefs  # noqa: E402

MOD = md.MOD
OUT = os.path.join(MOD, "materials", "models", "weapons", "v_hand", "hl2rpm")
N = 1024

# the suits: their first-person hands model, its skin, how the parts are painted
#   glove/sleeve: "hand" | "arm" = the patch of that part of the hands model, "keep" = the original
#   skin_glove: bare hands (flatter normals, softer creases)
SUITS = {
    "citizen": dict(model="models/hl2rpm/hands/citizen.mdl", glove="hand", sleeve="arm", plate="arm", skin_glove=True),
    "hev": dict(keep=True),
    "pcv": dict(model="models/hl2rpm/hands/hev.mdl", skin=1, glove="keep", sleeve="keep", plate="arm", plate_tint=True),
    "cp": dict(model="models/hl2rpm/hands/cp.mdl", glove="hand", sleeve="arm", plate="arm"),
    "soldier": dict(model="models/hl2rpm/hands/soldier.mdl", glove="hand", sleeve="arm", plate="arm"),
    "prison": dict(model="models/hl2rpm/hands/prison.mdl", glove="hand", sleeve="arm", plate="arm"),
    "elite": dict(model="models/hl2rpm/hands/elite.mdl", glove="hand", sleeve="arm", plate="arm"),
    "worker": dict(model="models/hl2rpm/hands/worker.mdl", glove="hand", sleeve="arm", plate="arm", rubber_glove=True),
    "hazmat": dict(model="models/hl2rpm/hands/hazmat.mdl", glove="hand", sleeve="arm", plate="arm", rubber_glove=True),
}
VIEW_MODELS = "pistol grenade smg1 shotgun bugbait crowbar 357 irifle crossbow rpg physcannon stunstick slam superphyscannon".split()

_fs = None


def fs():
    global _fs
    if _fs is None:
        _fs = gamefs.GameFS(MOD, md.SDK)
    return _fs


def decode_vtf(data, max_size=1024):
    tmp = os.path.join(os.environ.get("TEMP", "."), "hl2rpm_vmhands_tmp.vtf")
    with open(tmp, "wb") as f:
        f.write(data)
    img, _fmt, _size = pvtf.load(tmp, max_size=max_size)
    return img


def material_texture(cdmaterials, name, key="$basetexture"):
    """the texture (RGBA uint8) of a model material, found as the engine finds it"""
    for cd in cdmaterials + [""]:
        p = ("materials/%s/%s.vmt" % (cd.strip("/"), name)).replace("//", "/").lower()
        t = fs().read(p)
        if t is None:
            continue
        t = re.sub(r"//[^\n]*", "", t.decode("latin-1"))
        m = re.search(r'"?%s"?\s+"?([^"\s]+)' % re.escape(key), t, re.I)
        if not m:
            return None
        d = fs().read("materials/%s.vtf" % m.group(1).replace("\\", "/").lower())
        return decode_vtf(d) if d else None
    return None


# ------------------------------------------------------------------ the parts of the sheet
def sheet_parts(orig):
    """masks (N x N float) of the parts of v_hand_sheet: glove, cuff, sleeve, plate"""
    mask = Image.new("L", (N, N), 0)
    d = ImageDraw.Draw(mask)
    for w in VIEW_MODELS:
        m = md.Model(fs(), "models/weapons/v_%s.mdl" % w)
        hs = [i for i, t in enumerate(m.textures) if t.lower() == "v_hand_sheet"]
        for _bn, _base, models in m.bodyparts():
            for _mn, meshes in models:
                for (skinref, _P, _N, U, I, _W, _B) in meshes:
                    if m.skins[0][skinref] not in hs:
                        continue
                    for t in I.reshape(-1, 3):
                        d.polygon([(U[v][0] * N, U[v][1] * N) for v in t], fill=255)
    used = np.asarray(mask, np.float32) / 255
    y, x = np.mgrid[0:N, 0:N].astype(np.float32) / N
    # (by where the islands lie on the sheet - see the docstring; the boxes fit v_hand_sheet's layout)
    glove = ((y < 0.42) & (x < 0.88)) | ((x < 0.19) & (y >= 0.33) & (y < 0.57))
    cuff = (x >= 0.26) & (x < 0.83) & (y >= 0.41) & (y < 0.56)
    r, g, b = [orig[..., c].astype(np.float32) for c in range(3)]
    mx, mn = np.maximum(np.maximum(r, g), b), np.minimum(np.minimum(r, g), b)
    sat = (mx - mn) / np.maximum(mx, 1)
    orange = (r > g * 1.25) & (g > b * 1.2) & (sat > 0.35) & (mx > 70)
    plate = mm.blur(orange.astype(np.float32), 1.5) > 0.5
    parts = {"glove": glove & ~cuff, "cuff": cuff & ~glove}
    parts["plate"] = plate & ~parts["glove"] & ~parts["cuff"]
    parts["sleeve"] = ~(parts["glove"] | parts["cuff"] | parts["plate"])
    out = {k: mm.blur(v.astype(np.float32), 1.2) for k, v in parts.items()}
    tot = sum(out.values())
    return {k: v / np.maximum(tot, 1e-4) for k, v in out.items()}, used


# ------------------------------------------------------------------ the colours of a suit
def largest_square(mask):
    """(y0, x0, size) of a big square of True in a bool array (iterative erosion)"""
    m = mask.copy()
    k = 0
    while m.any():
        prev = m
        m = m & np.roll(m, 1, 0) & np.roll(m, -1, 0) & np.roll(m, 1, 1) & np.roll(m, -1, 1)
        m[0, :] = m[-1, :] = m[:, 0] = m[:, -1] = False
        k += 1
    ys, xs = np.nonzero(prev)
    cy, cx = int(ys.mean()), int(xs.mean())
    # the surviving pixel nearest to the centroid of the last ones
    i = np.argmin((ys - cy) ** 2 + (xs - cx) ** 2)
    cy, cx = ys[i], xs[i]
    s = max(4, int(k * 2 * 0.9))
    return cy - s // 2, cx - s // 2, s


def suit_patches(spec):
    """{"hand": RGB patch, "arm": RGB patch} from the suit's hands model"""
    m = md.Model(fs(), spec["model"])
    skin = spec.get("skin", 0)
    fam = m.skins[min(skin, len(m.skins) - 1)]
    low = [b.lower() for b in m.bone_names]

    def part(bi):
        n = low[bi]
        if "hand" in n or "finger" in n:
            return "hand"
        if "forearm" in n:
            return "arm"            # (not the upper arm: shoulder patches, emblems)
        return None
    areas = {}      # (part, texture) -> [triangles in uv]
    for _bn, _base, models in m.bodyparts():
        if not models:
            continue
        for (skinref, _P, _N, U, I, W, B) in models[0][1]:
            tex = m.textures[fam[skinref]]
            dom = B[np.arange(len(B)), np.argmax(W, 1)]
            for t in I.reshape(-1, 3):
                ks = [part(int(dom[v])) for v in t]
                k = max(set(ks), key=ks.count)
                if k:
                    areas.setdefault((k, tex), []).append([(U[v][0], U[v][1]) for v in t])
    out = {}
    for k in ("hand", "arm"):
        cands = [(sum(abs((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])) for a, b, c in tris), tex, tris)
                 for (kk, tex), tris in areas.items() if kk == k]
        if not cands:
            continue
        for _area, tex, tris in sorted(cands, reverse=True):
            img = material_texture(m.cdmaterials, tex)
            if img is None:
                continue
            h, w = img.shape[:2]
            mk = Image.new("L", (w, h), 0)
            dd = ImageDraw.Draw(mk)
            for tri in tris:
                dd.polygon([((u % 1.0) * w, (v % 1.0) * h) for u, v in tri], fill=255)
            mask = np.asarray(mk) > 0
            y0, x0, s = largest_square(mask)
            patch = img[max(0, y0):y0 + s, max(0, x0):x0 + s, :3].astype(np.float32)
            if patch.size == 0:
                continue
            px = img[..., :3][mask].astype(np.float32)
            lum = px.mean(1)
            lo, hi = np.percentile(lum, 25), np.percentile(lum, 75)
            col = px[(lum >= lo) & (lum <= hi)].mean(0)
            out[k] = (patch, tex, s, col)
            break
    return out


def tile(patch, size, scale_px):
    """the patch mirrored and repeated over size x size, each copy scale_px wide"""
    p = np.asarray(Image.fromarray(np.clip(patch, 0, 255).astype(np.uint8)).resize((scale_px, scale_px), Image.LANCZOS), np.float32)
    pm = np.concatenate([p, p[:, ::-1]], 1)
    pm = np.concatenate([pm, pm[::-1]], 0)
    reps = size // (2 * scale_px) + 2
    big = np.tile(pm, (reps, reps, 1))
    return big[:size, :size]


def balance(patch):
    """the patch without its own large shading (light and shadow belong to the sheet's detail)"""
    lum = patch.mean(2)
    low = mm.blur(lum, max(2.0, patch.shape[0] / 6))
    return patch * (lum.mean() / np.maximum(low, 1))[..., None]


# ------------------------------------------------------------------ painting
def paint(spec, orig, onrm, parts):
    if spec.get("keep"):
        return orig[..., :3].astype(np.float32), onrm
    patches = suit_patches(spec)
    lum = orig[..., :3].astype(np.float32).mean(2)
    detail = np.clip(lum / np.maximum(mm.blur(lum, 10), 1), 0.45, 1.6)
    res = np.zeros((N, N, 3), np.float32)
    flat = np.zeros((N, N), np.float32)            # how much the normals flatten
    o = orig[..., :3].astype(np.float32)
    for name in ("glove", "cuff", "sleeve", "plate"):
        src = {"glove": spec.get("glove"), "cuff": spec.get("sleeve"), "sleeve": spec.get("sleeve"), "plate": spec.get("plate")}[name]
        if src == "keep" or src not in patches:
            layer = o
            if name == "plate" and spec.get("plate_tint") and "arm" in patches:
                # the P.C.V.: the orange armour in the suit's own colour, its shading kept
                col = balance(patches["arm"][0]).reshape(-1, 3).mean(0)
                layer = (lum / max(lum[parts["plate"] > 0.5].mean(), 1))[..., None] * col[None, None, :]
        else:
            # the suit's colour (the middle half of the part's pixels: no emblems, no seams) with
            # the fine grain of its texture (a high-pass of the patch, mirrored tiles)
            p = patches[src][0]
            pl = p.mean(2)
            grain = np.clip(pl / np.maximum(mm.blur(pl, 3), 1) - 1, -0.15, 0.15)
            g = tile(np.dstack([grain * 127 + 128] * 3), N, 96)[..., 0] / 127 - 128 / 127
            base = patches[src][3][None, None, :] * (1 + 0.6 * g)[..., None]
            strength = {"glove": 0.9, "cuff": 0.7, "sleeve": 0.8, "plate": 0.8}[name]
            if name == "glove" and (spec.get("skin_glove") or spec.get("rubber_glove")):
                strength = 0.35 if spec.get("skin_glove") else 0.5
            if name == "cuff":
                base = base * 0.82          # the band of the sleeve's end: a little darker
            layer = base * detail[..., None] ** strength
            if name == "glove" and spec.get("skin_glove"):
                flat += parts[name] * 0.75
            elif name == "glove" and spec.get("rubber_glove"):
                flat += parts[name] * 0.4
            elif name in ("sleeve", "plate", "cuff"):
                flat += parts[name] * 0.25
        res += layer * parts[name][..., None]
    n = onrm.astype(np.float32)
    nx, ny = n[..., 0] / 127.5 - 1, n[..., 1] / 127.5 - 1
    k = 1 - np.clip(flat, 0, 0.9)
    nx, ny = nx * k, ny * k
    nz = np.sqrt(np.clip(1 - nx * nx - ny * ny, 0, 1))
    nrm = np.dstack([(nx + 1) * 127.5, (ny + 1) * 127.5, nz * 127.5 + 127.5, n[..., 3] if n.shape[2] > 3 else np.full((N, N), 255)])
    return np.clip(res, 0, 255), nrm, patches


def main(argv):
    pick = [a for a in argv if not a.startswith("--") and a in SUITS] or list(SUITS)
    orig = decode_vtf(fs().read("materials/models/weapons/v_hand/v_hand_sheet.vtf"))
    onrm = decode_vtf(fs().read("materials/models/weapons/v_hand/v_hand_sheet_normal.vtf"))
    if orig.shape[0] != N:
        orig = np.asarray(Image.fromarray(orig).resize((N, N), Image.LANCZOS))
    if onrm.shape[0] != N:
        onrm = np.asarray(Image.fromarray(onrm).resize((N, N), Image.LANCZOS))
    print("parts of the sheet...")
    parts, _used = sheet_parts(orig)
    os.makedirs(OUT, exist_ok=True)
    previews = []
    for s in pick:
        spec = SUITS[s]
        r = paint(spec, orig, onrm, parts)
        rgb, nrm = r[0], r[1]
        info = ", ".join("%s <- %s (%d px)" % (k, v[1], v[2]) for k, v in (r[2].items() if len(r) > 2 else []))
        print("  %-8s %s" % (s, info or "the original"))
        hd_textures.write_vtf(os.path.join(OUT, "v_hand_sheet_%s.vtf" % s), np.dstack([rgb, np.full((N, N), 255)]).astype(np.uint8), 0x4 | 0x8, False)
        hd_textures.write_vtf(os.path.join(OUT, "v_hand_sheet_%s_normal.vtf" % s), np.clip(nrm, 0, 255).astype(np.uint8), 0x4 | 0x8 | 0x80000, True)
        previews.append((s, rgb))
    if "--preview" in argv:
        out = argv[argv.index("--preview") + 1]
        cols = min(3, len(previews))
        rows = math.ceil(len(previews) / cols)
        sheet = Image.new("RGB", (cols * 400, rows * 420), (30, 30, 30))
        dr = ImageDraw.Draw(sheet)
        for i, (s, rgb) in enumerate(previews):
            im = Image.fromarray(rgb.astype(np.uint8)).resize((400, 400), Image.LANCZOS)
            sheet.paste(im, ((i % cols) * 400, (i // cols) * 420 + 20))
            dr.text(((i % cols) * 400 + 6, (i // cols) * 420 + 4), s, fill=(255, 255, 255))
        sheet.save(out)
    print("done:", OUT)


if __name__ == "__main__":
    main(sys.argv[1:])
