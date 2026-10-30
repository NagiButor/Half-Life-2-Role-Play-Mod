"""Assets of the suitless vitals (hl2rpm_vitals.cpp, hl2rpm_lowhealth_ps30):

materials/hl2rpm/effects/lowhealth_veins.vtf - the vessels of the eye that grow in from the
    edges of the screen as health goes down (BGRA8888 1024x512 with mips, clamped):
      r  the vessels (soft, thick at the edge, tapering to the tips)
      g  the order they show in: 0 at the roots on the edge .. 1 at the tips deep in
         (path length along the vessel and distance from the edge)
      b  blotches - low frequency noise that breaks up the inner line of the red
      a  a soft halo around the vessels (it swells with the heartbeat)
    Grown in a 16:9 space and squeezed into 2:1 - on a 16:9 screen they come out round.
    Vessels branch by Murray's law (r^3 = r1^3 + r2^3), taper to their tips, wander a
    little, keep going inward; a side branch stops where another vessel already is.

sound/hl2rpm/player/heartbeat_once.wav - one beat of HL2's player/heartbeat1.wav
    (that one loops - a cue point - and can't be played once per beat of the monitor).

python make_vitals_assets.py [--seed N] [--preview]   (preview: veins_preview.png here)
"""
import math
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageFilter

MOD = r"E:\Steam\steamapps\sourcemods\hl2rpm"
HL2 = r"E:\SourceModding\UsefulStuff\Source sdk 2013 HL2 unpacked vpk\hl2"

W, H = 1024, 576        # the space the vessels grow in (16:9)
TW, TH = 1024, 512      # the texture
SS = 2                  # supersampling of the drawing
STEP = 1.5
R_TIP = 0.45


def grow_vessels(seed):
    rng = np.random.default_rng(seed)
    cw, ch = W * SS, H * SS
    cov = np.zeros((ch, cw), np.float32)
    rev = np.ones((ch, cw), np.float32)
    cell = 5
    gw, gh = W // cell + 2, H // cell + 2
    owner = -np.ones((gh, gw), np.int64)

    def edge_dist(x, y):
        return min(x, W - x, y, H - y)

    def inward(x, y):
        d = np.array([x, W - x, y, H - y], np.float64)
        w = np.exp(-(d - d.min()) / 50.0)
        v = w[0] * np.array([1, 0]) + w[1] * np.array([-1, 0]) + w[2] * np.array([0, 1]) + w[3] * np.array([0, -1])
        return math.atan2(v[1], v[0])

    def stamp(x0, y0, x1, y1, r, op, rv):
        X0, Y0, X1, Y1, R = x0 * SS, y0 * SS, x1 * SS, y1 * SS, r * SS
        pad = R + 3
        xa, xb = int(max(0, min(X0, X1) - pad)), int(min(cw - 1, max(X0, X1) + pad))
        ya, yb = int(max(0, min(Y0, Y1) - pad)), int(min(ch - 1, max(Y0, Y1) + pad))
        if xa > xb or ya > yb:
            return
        xs = np.arange(xa, xb + 1, dtype=np.float32) + 0.5
        ys = np.arange(ya, yb + 1, dtype=np.float32)[:, None] + 0.5
        dx, dy = X1 - X0, Y1 - Y0
        L2 = dx * dx + dy * dy + 1e-6
        t = np.clip(((xs - X0) * dx + (ys - Y0) * dy) / L2, 0, 1)
        px, py = X0 + t * dx - xs, Y0 + t * dy - ys
        d = np.sqrt(px * px + py * py)
        a = np.clip((R + 1.0 - d) / (0.45 * R + 1.6), 0, 1) * op
        sub = cov[ya:yb + 1, xa:xb + 1]
        np.maximum(sub, a, out=sub)
        rsub = rev[ya:yb + 1, xa:xb + 1]
        m = a > 0.12
        rsub[m] = np.minimum(rsub[m], rv)

    counter = [0]

    def grow(x, y, ang, rad, budget, depth_max):
        stack = [(x, y, ang, rad, budget, 0.0, 0, -1)]
        while stack:
            x, y, ang, rad, budget, path, gen, parent = stack.pop()
            counter[0] += 1
            me = counter[0]
            n_steps = max(2, int(budget / STEP))
            decay = (R_TIP / rad) ** (1.0 / n_steps) if rad > R_TIP else 1.0
            angvel = 0.0
            travelled = 0.0
            phase = rng.uniform(0, 6.283)
            for i in range(n_steps):
                if rad < R_TIP * 1.05:
                    break
                dn = edge_dist(x, y) / H
                if dn > depth_max * (1.25 if gen > 0 else 1.0):
                    break
                angvel = angvel * 0.955 + rng.normal(0, 0.0105)
                target = inward(min(max(x, 1), W - 1), min(max(y, 1), H - 1))
                diff = (target - ang + math.pi) % (2 * math.pi) - math.pi
                ang += angvel + diff * (0.005 if dn > 0.06 else 0.04)
                nx, ny = x + STEP * math.cos(ang), y + STEP * math.sin(ang)
                # crowding: another vessel's cell - a side branch ends there
                cx, cy = int(nx // cell) + 1, int(ny // cell) + 1
                if 0 <= cx < gw and 0 <= cy < gh:
                    o = owner[cy, cx]
                    if gen > 0 and travelled > 10 and o >= 0 and o != me and o != parent:
                        break
                    owner[cy, cx] = me
                rv = min(1.0, 0.4 * path / (0.42 * H) + 0.6 * dn / 0.34)
                op = min(1.0, 0.3 + rad / 1.6) * (0.82 + 0.18 * math.sin(path / 19.0 + phase))
                stamp(x, y, nx, ny, rad, op, rv)
                x, y = nx, ny
                path += STEP
                travelled += STEP
                rad *= decay
                # branching: Murray's law split of the cross-section
                if gen < 5 and rad > 0.75 and travelled > 12 and rng.random() < 0.03:
                    s = rng.uniform(0.22, 0.48)
                    r_child = rad * s ** (1 / 3)
                    r_rest = rad * (1 - s) ** (1 / 3)
                    side = 1 if rng.random() < 0.5 else -1
                    remain = (n_steps - i) * STEP
                    stack.append((x, y, ang + side * rng.uniform(0.5, 1.2), r_child,
                                  remain * rng.uniform(0.55, 0.95) * (0.6 + s), path, gen + 1, me))
                    rad = r_rest
                    ang -= side * rng.uniform(0.15, 0.4)
                    left = max(2, n_steps - i)
                    decay = (R_TIP / rad) ** (1.0 / left) if rad > R_TIP else 1.0

    perim = 2 * (W + H)
    n_roots = 34
    pos = (np.arange(n_roots) + rng.uniform(0.2, 0.8, n_roots)) / n_roots * perim + rng.uniform(0, perim)
    for p in pos % perim:
        if p < W:
            x, y, a = p, -4.0, math.pi / 2
        elif p < W + H:
            x, y, a = W + 4.0, p - W, math.pi
        elif p < 2 * W + H:
            x, y, a = W - (p - W - H), H + 4.0, -math.pi / 2
        else:
            x, y, a = -4.0, H - (p - 2 * W - H), 0.0
        cx, cy = abs(x / W - 0.5) * 2, abs(y / H - 0.5) * 2
        depth = rng.uniform(0.16, 0.30) + 0.10 * min(cx, cy) ** 2      # corners reach further in
        rad = rng.uniform(3.0, 5.2)
        grow(x, y, a + rng.normal(0, 0.35), rad, depth * H * rng.uniform(1.5, 2.2), depth)

    veins = cov.reshape(H, SS, W, SS).mean((1, 3))
    # (they are in the eye - out of focus; drawn sharp they looked like twigs on the screen)
    veins = np.asarray(Image.fromarray((veins * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.6)), np.float32) / 255
    reveal = rev.reshape(H, SS, W, SS).min((1, 3))

    def value_noise(res, s):
        g = np.random.default_rng(s).random((res[1], res[0])).astype(np.float32)
        return np.asarray(Image.fromarray((g * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC), np.float32) / 255
    blotch = 0.55 * value_noise((9, 5), seed + 11) + 0.3 * value_noise((22, 12), seed + 12) + 0.15 * value_noise((56, 32), seed + 13)
    blotch = (blotch - blotch.min()) / (blotch.max() - blotch.min())

    halo = np.asarray(Image.fromarray((veins * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(4)), np.float32) / 255
    halo = np.clip(halo / max(halo.max(), 1e-3) * 1.5, 0, 1)
    print("vessels: %d branches" % counter[0])
    return veins, reveal, blotch, halo


def to_texture(a):
    im = Image.fromarray((np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8))
    return np.asarray(im.resize((TW, TH), Image.LANCZOS), np.float32) / 255


def mips(a):
    out = [a]
    while out[-1].shape[0] > 1 or out[-1].shape[1] > 1:
        p = out[-1]
        if p.shape[0] > 1:
            p = 0.5 * (p[0::2] + p[1::2])
        if p.shape[1] > 1:
            p = 0.5 * (p[:, 0::2] + p[:, 1::2])
        out.append(p)
    return out


def write_vtf_bgra(path, rgba):
    h, w = rgba.shape[:2]
    levels = mips(rgba)
    flags = 0x4 | 0x8 | 0x200 | 0x2000      # clamp s/t, no LOD (picmip), 8-bit alpha
    header = struct.pack("<4sIIIHHIHH4x3f4xfIBiBBH",
                         b"VTF\0", 7, 2, 80, w, h, flags, 1, 0,
                         0.1, 0.02, 0.02, 1.0,
                         12, len(levels), -1, 0, 0, 1).ljust(80, b"\0")
    data = bytearray()
    for lv in reversed(levels):
        b = np.clip(lv * 255 + 0.5, 0, 255).astype(np.uint8)
        data += b[..., [2, 1, 0, 3]].tobytes()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(header + data)


def read_wav(path):
    data = open(path, "rb").read()
    pos, fmt, pcm = 12, None, None
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            pcm = body
        pos += 8 + size + (size & 1)
    tag, ch, rate, _, _, bits = fmt
    assert tag == 1 and ch == 1
    if bits == 8:
        a = (np.frombuffer(pcm, np.uint8).astype(np.float32) - 128.0) / 128.0
    else:
        a = np.frombuffer(pcm, np.int16).astype(np.float32) / 32768.0
    return a, rate


def write_wav16(path, a, rate):
    pcm = np.clip(np.rint(a * 32767.0), -32768, 32767).astype(np.int16).tobytes()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(pcm)) + pcm)


def main():
    seed = 7
    if "--seed" in sys.argv:
        seed = int(sys.argv[sys.argv.index("--seed") + 1])

    veins, reveal, blotch, halo = grow_vessels(seed)
    rgba = np.stack([to_texture(veins), to_texture(reveal), to_texture(blotch), to_texture(halo)], -1)
    vtf = os.path.join(MOD, "materials", "hl2rpm", "effects", "lowhealth_veins.vtf")
    write_vtf_bgra(vtf, rgba)
    print("wrote", vtf)
    if "--preview" in sys.argv:
        Image.fromarray((rgba[..., 0] * 255).astype(np.uint8)).save("veins_preview.png")

    # one heartbeat: the first "lub-dub" of the loop, a short fade at the end
    a, rate = read_wav(os.path.join(HL2, "sound", "player", "heartbeat1.wav"))
    n = int(rate * 0.62)
    beat = a[:n].copy()
    fade = int(rate * 0.06)
    beat[-fade:] *= np.linspace(1.0, 0.0, fade) ** 2
    beat -= beat.mean()
    wav = os.path.join(MOD, "sound", "hl2rpm", "player", "heartbeat_once.wav")
    write_wav16(wav, beat, rate)
    print("wrote", wav)


if __name__ == "__main__":
    main()
