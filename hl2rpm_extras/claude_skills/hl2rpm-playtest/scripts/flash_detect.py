"""One-frame flashes in a recorded movie (walking camera).

    python flash_detect.py <dir> <prefix> <out_prefix> [tile] [thr] [first last]

Per tile (8x8) of luma: frame i is compared with the mean of frames i-1 and i+1.
Smooth motion makes i-1 -> i -> i+1 roughly linear, so |F_i - avg| stays small
compared to |F_i+1 - F_i-1|; a one-frame event (flash, pop) does not. Prints the
number of flashing tiles per frame (only frames with any), writes a heat map of
where they happened and the frame triples of the worst ones (x2 zoom of the tile
area) for looking at.
"""
import glob
import os
import sys

import numpy as np
from PIL import Image


def luma(path):
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    return a[..., 0] * 0.2126 + a[..., 1] * 0.7152 + a[..., 2] * 0.0722


def tiles(l, t):
    h, w = l.shape
    h2, w2 = h // t * t, w // t * t
    b = l[:h2, :w2].reshape(h2 // t, t, w2 // t, t)
    return b.mean(axis=(1, 3)), b.std(axis=(1, 3))


def main(argv):
    d, prefix, out = argv[1], argv[2], argv[3]
    t = int(argv[4]) if len(argv) > 4 else 8
    thr = float(argv[5]) if len(argv) > 5 else 6.0
    files = sorted(glob.glob(os.path.join(d, prefix + "*.tga")))[2:]
    if len(argv) > 7:
        files = files[int(argv[6]):int(argv[7])]
    if len(files) < 5:
        print("need 5+ frames, have", len(files))
        return 1
    TS = [tiles(luma(f), t) for f in files]
    T = [m for m, s in TS]
    S = [s for m, s in TS]
    flat_only = os.environ.get("FLAT", "0") == "1"
    heat = np.zeros_like(T[0])
    events = []
    for i in range(1, len(T) - 1):
        avg = (T[i - 1] + T[i + 1]) * 0.5
        dev = np.abs(T[i] - avg)
        mot = np.abs(T[i + 1] - T[i - 1])
        flash = (dev > thr) & (dev > 2.0 * mot)
        if flat_only:
            # only tiles without edges in any of the 3 frames: lighting, not geometry moving
            flash &= np.maximum(np.maximum(S[i - 1], S[i]), S[i + 1]) < 3.0
        n = int(flash.sum())
        heat += flash
        if n:
            ys, xs = np.nonzero(flash)
            events.append((n, i, float(dev[flash].max()), int(ys.mean()), int(xs.mean())))
    total = sum(e[0] for e in events)
    print("%s: %d frames, %d frames with flashes, %d flashing tiles in total" % (prefix, len(T), len(events), total))
    for n, i, mx, y, x in sorted(events, reverse=True)[:12]:
        print("  frame %4d (%s): %4d tiles, max dev %.1f, around tile (%d,%d)" % (i, os.path.basename(files[i]), n, mx, x, y))
    hm = np.clip(heat * 60, 0, 255).astype(np.uint8)
    Image.fromarray(hm).resize((hm.shape[1] * t, hm.shape[0] * t), Image.NEAREST).save(out + "_heat.png")
    for k, (n, i, mx, y, x) in enumerate(sorted(events, reverse=True)[:3]):
        ims = [Image.open(files[j]).convert("RGB") for j in (i - 1, i, i + 1)]
        w, h = ims[0].size
        cx, cy = x * t, y * t
        box = (max(0, cx - 120), max(0, cy - 80), min(w, cx + 120), min(h, cy + 80))
        crops = [im.crop(box).resize(((box[2] - box[0]) * 2, (box[3] - box[1]) * 2), Image.NEAREST) for im in ims]
        sheet = Image.new("RGB", (crops[0].width * 3 + 8, crops[0].height))
        for j, c in enumerate(crops):
            sheet.paste(c, (j * (c.width + 4), 0))
        sheet.save("%s_ev%d.png" % (out, k))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
