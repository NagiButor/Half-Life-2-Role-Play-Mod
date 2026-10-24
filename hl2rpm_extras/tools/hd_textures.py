"""HD versions of the stock world textures for HL2RPM (x2, written into the mod's materials).

    python hd_textures.py <list.txt | material ...> [--out <mod materials dir>] [--force] [--dry]

A list line is a material ("concrete/concretewall071f") or a .bsp: every world material of the map.
For each base texture ($basetexture, $basetexture2 of a blend) that is 512 or smaller:

  1. the texture (top mip) is padded by wrapping it around - the upscaler then sees a tile that
     continues, and the result tiles without seams;
  2. Real-ESRGAN x4plus (E:/SourceModding/UsefulStuff/realesrgan, ncnn-vulkan, on the GPU) makes it
     4x; that is brought down to 2x (Lanczos);
  3. only its fine detail is kept: the result is the original's own low frequencies (its colors and
     tones - the upscaler darkens and saturates) plus the upscaled texture's high frequencies
     (sharp joints, edges, grain) - "detail transfer";
  4. mip maps (box filter in linear light), DXT1 (DXT5 with an alpha channel), a VTF 7.2 with the
     original's flags, into <mod>/materials/hd/<same path>.vtf. Not over the stock file: the size of
     a brush texture's repeat is its resolution (a 1024 version would draw every brick twice as
     large) - the client gives the deferred material $hdtexture, $basetexture keeps the mapping.

Bump maps, models, translucent and alpha tested textures are left alone. --dry lists what it would do.

    python hd_textures.py --textures <list.txt> [--inplace] [--min 64]

A list of textures themselves (not materials) - the faces of the characters, the sheets of the
weapons, the decals. --inplace writes over their paths in the mod's materials (models: their texture
coordinates are normalized, a 2x texture only sharpens them - a texture the mod has of its own is
left alone); without it into materials/hd (decals: the size of a decal is its texture's - the client
gives DEFERRED_DECAL $hdtexture).
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

import numpy as np
from PIL import Image, ImageFilter

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "pom"))
import vtf  # noqa: E402

REF = r"E:\SourceModding\UsefulStuff\Source sdk 2013 HL2 unpacked vpk"
GAMES = ["ep2", "episodic", "hl2"]
MOD = r"E:\Steam\steamapps\sourcemods\hl2rpm"
ESRGAN = r"E:\SourceModding\UsefulStuff\realesrgan\realesrgan-ncnn-vulkan.exe"
PAD = 32
SIGMA = 1.6          # the split of low / high frequencies (pixels of the 2x texture)
HIGH_GAIN = 0.9      # how much of the upscaled detail


# ------------------------------------------------------------------ finding things
def find_file(rel):
    for g in GAMES:
        p = os.path.join(REF, g, rel)
        if os.path.exists(p):
            return p
    return None


def read_vmt(name):
    p = find_file(os.path.join("materials", name + ".vmt"))
    if not p:
        return None
    return re.sub(r"//[^\n]*", "", open(p, encoding="latin-1").read())


def vmt_key(t, key):
    m = re.search(r'"?' + re.escape(key) + r'"?\s+"([^"]+)"', t, re.I) or re.search(r'"?' + re.escape(key) + r'"?\s+(\S+)', t, re.I)
    return m.group(1).replace("\\", "/").lower() if m else None


def bsp_materials(path):
    data = open(path, "rb").read()

    def lump(i):
        off, ln = struct.unpack_from("<ii", data, 8 + i * 16)
        return data[off:off + ln]
    strdata, strtab = lump(43), lump(44)
    names = set()
    for i in range(len(strtab) // 4):
        off = struct.unpack_from("<i", strtab, i * 4)[0]
        n = strdata[off:strdata.index(b"\0", off)].decode("latin-1").lower()
        n = re.sub(r"^maps/[^/]+/", "", n)
        n = re.sub(r"(_wvt_patch|_-?\d+_-?\d+_-?\d+)$", "", n)
        names.add(n)
    return sorted(names)


# ------------------------------------------------------------------ DXT and VTF
def _to565(c):
    c = np.clip(np.rint(c), 0, 255).astype(np.int32)
    return ((c[..., 0] >> 3) << 11) | ((c[..., 1] >> 2) << 5) | (c[..., 2] >> 3)


def _from565(v):
    r = ((v >> 11) & 31) * 255 // 31
    g = ((v >> 5) & 63) * 255 // 63
    b = (v & 31) * 255 // 31
    return np.stack([r, g, b], -1).astype(np.float32)


def encode_dxt1_colors(blocks):
    """blocks: (n, 16, 3) float -> (n, 8) uint8 color blocks (4-color mode)"""
    n = blocks.shape[0]
    mean = blocks.mean(axis=1, keepdims=True)
    centered = blocks - mean
    cov = np.einsum("nki,nkj->nij", centered, centered)
    # principal axis by a few power iterations (stable for flat blocks: falls back to the diagonal)
    axis = np.ones((n, 3), np.float32)
    for _ in range(6):
        axis = np.einsum("nij,nj->ni", cov, axis)
        norm = np.linalg.norm(axis, axis=1, keepdims=True)
        axis = np.where(norm > 1e-6, axis / np.maximum(norm, 1e-6), np.float32(0.57735))
    proj = np.einsum("nki,ni->nk", centered, axis)
    lo = mean[:, 0] + axis * proj.min(axis=1, keepdims=True)
    hi = mean[:, 0] + axis * proj.max(axis=1, keepdims=True)
    c0 = _to565(hi)
    c1 = _to565(lo)
    # 4-color mode needs c0 > c1
    swap = c0 < c1
    c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
    p0, p1 = _from565(c0), _from565(c1)
    pal = np.stack([p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3], 1)  # n,4,3
    d = ((blocks[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1)    # n,16,4
    idx = d.argmin(-1).astype(np.uint32)
    same = c0 == c1
    idx[same] = 0
    bits = np.zeros(n, np.uint32)
    for i in range(16):
        bits |= idx[:, i] << np.uint32(2 * i)
    out = np.zeros((n, 8), np.uint8)
    out[:, 0] = c0 & 255
    out[:, 1] = c0 >> 8
    out[:, 2] = c1 & 255
    out[:, 3] = c1 >> 8
    for i in range(4):
        out[:, 4 + i] = (bits >> np.uint32(8 * i)) & 255
    return out


def encode_dxt5_alpha(alpha):
    """alpha: (n, 16) float -> (n, 8) uint8 alpha blocks (8-value mode)"""
    n = alpha.shape[0]
    a0 = np.clip(np.rint(alpha.max(1)), 0, 255).astype(np.int32)
    a1 = np.clip(np.rint(alpha.min(1)), 0, 255).astype(np.int32)
    flat = a0 == a1
    a0 = np.where(flat & (a0 < 255), a0 + 1, a0)
    a1 = np.where(flat & (a0 >= 255), a1 - 1, a1).clip(0, 255)
    vals = np.stack([a0, a1] + [((7 - k) * a0 + k * a1) / 7.0 for k in range(1, 7)], 1)  # n,8
    d = np.abs(alpha[:, :, None] - vals[:, None, :])
    idx = d.argmin(-1).astype(np.uint64)
    bits = np.zeros(n, np.uint64)
    for i in range(16):
        bits |= idx[:, i] << np.uint64(3 * i)
    out = np.zeros((n, 8), np.uint8)
    out[:, 0] = a0
    out[:, 1] = a1
    for i in range(6):
        out[:, 2 + i] = ((bits >> np.uint64(8 * i)) & np.uint64(255)).astype(np.uint8)
    return out


def to_blocks(img):
    h, w = img.shape[:2]
    ph, pw = max(4, (h + 3) // 4 * 4), max(4, (w + 3) // 4 * 4)
    if (ph, pw) != (h, w):
        img = np.pad(img, ((0, ph - h), (0, pw - w), (0, 0)), mode="edge")
    c = img.shape[2]
    return img.reshape(ph // 4, 4, pw // 4, 4, c).transpose(0, 2, 1, 3, 4).reshape(-1, 16, c)


def encode_dxt(img, with_alpha):
    blocks = to_blocks(img.astype(np.float32))
    col = encode_dxt1_colors(blocks[:, :, :3])
    if not with_alpha:
        return col.tobytes()
    alp = encode_dxt5_alpha(blocks[:, :, 3])
    return np.concatenate([alp, col], 1).tobytes()


def srgb_to_lin(x):
    x = x / 255.0
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(x):
    x = np.clip(x, 0, 1)
    return np.where(x <= 0.0031308, x * 12.92, 1.055 * np.power(x, 1 / 2.4) - 0.055) * 255.0


def mip_chain(img):
    """RGBA uint8 top level -> list of levels (largest first), box filtered in linear light"""
    levels = [img]
    cur = img.astype(np.float32)
    while cur.shape[0] > 1 or cur.shape[1] > 1:
        h, w = cur.shape[:2]
        nh, nw = max(1, h // 2), max(1, w // 2)
        lin = np.concatenate([srgb_to_lin(cur[..., :3]), cur[..., 3:] / 255.0], -1)
        if h > 1 and w > 1:
            lin = lin.reshape(nh, 2, nw, 2, 4).mean((1, 3))
        elif h > 1:
            lin = lin.reshape(nh, 2, 1, 4).mean(1)
        else:
            lin = lin.reshape(1, nw, 2, 4).mean(2)
        cur = np.concatenate([lin_to_srgb(lin[..., :3]), lin[..., 3:] * 255.0], -1)
        levels.append(np.clip(np.rint(cur), 0, 255).astype(np.uint8))
    return levels


def write_vtf(path, img, flags, with_alpha):
    h, w = img.shape[:2]
    # (the alpha flags as the new contents have it; mip maps always)
    flags &= ~(0x1000 | 0x2000 | 0x100)
    if with_alpha:
        flags |= 0x2000
    levels = mip_chain(img)
    fmt = 15 if with_alpha else 13          # DXT5 / DXT1
    lin = srgb_to_lin(img[..., :3].astype(np.float32)).reshape(-1, 3).mean(0)
    thumb = np.array(Image.fromarray(levels[0][..., :3]).resize((16, 16), Image.BOX))
    header = struct.pack("<4sII", b"VTF\0", 7, 2)
    header += struct.pack("<I", 80)
    header += struct.pack("<HHIHH", w, h, flags, 1, 0)
    header += b"\0" * 4
    header += struct.pack("<3f", *lin)
    header += b"\0" * 4
    header += struct.pack("<f", 1.0)
    header += struct.pack("<iBiBB", fmt, len(levels), 13, 16, 16)
    header += struct.pack("<H", 1)
    header += b"\0" * (80 - len(header))
    body = encode_dxt(np.concatenate([thumb, np.full((16, 16, 1), 255, np.uint8)], -1), False)
    for lv in reversed(levels):
        body += encode_dxt(lv, with_alpha)
    if os.path.dirname(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(header + body)


def vtf_flags(path):
    with open(path, "rb") as f:
        h = f.read(32)
    return struct.unpack_from("<I", h, 20)[0]


# ------------------------------------------------------------------ the upscale
BATCH = 8            # textures per run of the upscaler: what is done is written (a stopped run resumes)


INPLACE_LIST = os.path.join(MOD, "materials", "hd_inplace.txt")


def inplace_written():
    if not os.path.exists(INPLACE_LIST):
        return set()
    return set( l.strip() for l in open(INPLACE_LIST, encoding="utf-8") if l.strip() )


def upscale(textures, force=False, dry=False, out_root=None, inplace=False, min_size=64):
    out_root = out_root or os.path.join(MOD, "materials", "hd")
    jobs = []
    for tex in textures:
        p = find_file(os.path.join("materials", tex + ".vtf"))
        if not p:
            continue
        out = os.path.join(out_root, tex.replace("/", os.sep) + ".vtf")
        if inplace:
            # (a texture the mod has of its own is not the stock one: left alone - with --force only
            # what this script wrote there before, listed in materials/hd_inplace.txt)
            if os.path.exists(out) and not ( force and tex in inplace_written() ):
                continue
        elif os.path.exists(out) and not force:
            continue
        with open(p, "rb") as f:
            hdr = f.read(64)
        w, h = struct.unpack_from("<HH", hdr, 16)
        if max(w, h) > 512 or min(w, h) < min_size:
            continue
        jobs.append((tex, p, out))
        print("  %-48s %dx%d" % (tex, w, h))
    if dry or not jobs:
        return jobs
    t0 = time.time()
    for first in range(0, len(jobs), BATCH):
        upscale_batch(jobs[first:first + BATCH])
        done = min(first + BATCH, len(jobs))
        spent = time.time() - t0
        print("[%d/%d] %.0f s, ~%.0f s left" % (done, len(jobs), spent, spent / done * (len(jobs) - done)), flush=True)
    return jobs


def upscale_batch(jobs):
    work = tempfile.mkdtemp(prefix="hl2rpm_hd_")
    src_dir, dst_dir = os.path.join(work, "in"), os.path.join(work, "out")
    os.makedirs(src_dir)
    os.makedirs(dst_dir)
    loaded = []
    for tex, p, out in jobs:
        a, fmt, size = vtf.load(p, max_size=4096)
        has_alpha = fmt in ("DXT5", "DXT3", "BGRA8888", "RGBA8888") and a[..., 3].min() < 250
        key = tex.replace("/", "#")
        Image.fromarray(np.pad(a[..., :3], ((PAD, PAD), (PAD, PAD), (0, 0)), mode="wrap")).save(os.path.join(src_dir, key + ".png"))
        loaded.append((tex, key, a, has_alpha, vtf_flags(p), out))
    subprocess.run([ESRGAN, "-i", src_dir, "-o", dst_dir, "-n", "realesrgan-x4plus", "-s", "4", "-f", "png"],
                   cwd=os.path.dirname(ESRGAN), check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for tex, key, a, has_alpha, flags, out in loaded:
        h, w = a.shape[:2]
        up = Image.open(os.path.join(dst_dir, key + ".png")).convert("RGB")
        up = up.crop((PAD * 4, PAD * 4, PAD * 4 + w * 4, PAD * 4 + h * 4)).resize((w * 2, h * 2), Image.LANCZOS)
        base = Image.fromarray(a[..., :3]).resize((w * 2, h * 2), Image.LANCZOS)
        # detail transfer: the original's low frequencies + the upscale's high ones (wrapped blur:
        # the texture tiles)
        def blur(im):
            arr = np.array(im).astype(np.float32)
            padded = np.pad(arr, ((16, 16), (16, 16), (0, 0)), mode="wrap")
            b = np.array(Image.fromarray(np.clip(padded, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(SIGMA))).astype(np.float32)
            return b[16:-16, 16:-16]
        upf = np.array(up).astype(np.float32)
        res = blur(base) + HIGH_GAIN * (upf - blur(up))
        rgb = np.clip(np.rint(res), 0, 255).astype(np.uint8)
        alpha = np.array(Image.fromarray(a[..., 3]).resize((w * 2, h * 2), Image.LANCZOS)) if has_alpha else np.full((h * 2, w * 2), 255, np.uint8)
        write_vtf(out, np.concatenate([rgb, alpha[..., None]], -1), flags, has_alpha)
        print("wrote", out)
        if out.lower().startswith(os.path.join(MOD, "materials").lower() + os.sep) and not out.lower().startswith(os.path.join(MOD, "materials", "hd").lower() + os.sep):
            if tex not in inplace_written():
                with open(INPLACE_LIST, "a", encoding="utf-8") as f:
                    f.write(tex + "\n")
    shutil.rmtree(work, ignore_errors=True)


def textures_of(materials):
    out = []
    for mat in materials:
        t = read_vmt(mat)
        if not t:
            continue
        shader = re.match(r'\s*"?([A-Za-z0-9_]+)', t).group(1).lower()
        if shader not in ("lightmappedgeneric", "worldvertextransition"):
            continue
        if re.search(r'\$(translucent|alphatest|additive|decal)"?\s+"?1', t, re.I):
            continue
        for key in ("$basetexture", "$basetexture2"):
            v = vmt_key(t, key)
            if v and v not in out:
                out.append(v)
    return out


def main(argv):
    force = "--force" in argv
    dry = "--dry" in argv
    out_root = None
    if "--out" in argv:
        out_root = argv[argv.index("--out") + 1]
    if "--textures" in argv:
        lst = argv[argv.index("--textures") + 1]
        textures = []
        for line in open(lst, encoding="utf-8"):
            line = line.split("//")[0].strip().lower().replace("\\", "/")
            if line and line not in textures:
                textures.append(line)
        inplace = "--inplace" in argv
        min_size = int(argv[argv.index("--min") + 1]) if "--min" in argv else 64
        if inplace:
            out_root = os.path.join(MOD, "materials")
        print("%d textures%s" % (len(textures), " (in place)" if inplace else ""))
        upscale(textures, force=force, dry=dry, out_root=out_root, inplace=inplace, min_size=min_size)
        return
    items = [a for a in argv[1:] if not a.startswith("--") and a != out_root]
    materials = []
    for it in items:
        if it.lower().endswith(".bsp"):
            materials += bsp_materials(it)
        elif it.lower().endswith(".txt") and os.path.exists(it):
            for line in open(it, encoding="utf-8"):
                line = line.strip()
                if line and not line.startswith("//"):
                    materials += bsp_materials(line) if line.lower().endswith(".bsp") else [line.lower()]
        else:
            materials.append(it.lower())
    textures = textures_of(sorted(set(materials)))
    print("%d materials -> %d base textures" % (len(set(materials)), len(textures)))
    upscale(textures, force=force, dry=dry, out_root=out_root)


if __name__ == "__main__":
    main(sys.argv)
