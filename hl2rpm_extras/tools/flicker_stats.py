"""Frame-to-frame flicker statistics for a series of still screenshots.

    python flicker_stats.py <dir with jpgs> <out.png> [--mask-top N]

Prints mean/95th percentile abs difference of each consecutive pair (luma) and
writes an image of the per-pixel maximum difference over the whole series
(x4 amplified) next to the first frame.
"""
import glob
import os
import sys

import numpy as np
from PIL import Image


def luma(img):
    a = np.asarray(img.convert("RGB"), dtype=np.float32)
    return a[..., 0] * 0.2126 + a[..., 1] * 0.7152 + a[..., 2] * 0.0722


def main(argv):
    d, out = argv[1], argv[2]
    files = sorted(glob.glob(os.path.join(d, "*.jpg")))
    if len(files) < 2:
        print("need 2+ jpgs")
        return 1
    frames = [luma(Image.open(f)) for f in files]
    maxdiff = np.zeros_like(frames[0])
    for i in range(1, len(frames)):
        diff = np.abs(frames[i] - frames[i - 1])
        maxdiff = np.maximum(maxdiff, diff)
        big = (diff > 12).mean() * 100
        print("%s -> %s: mean %.2f  p95 %.1f  p99.9 %.1f  pixels>12: %.2f%%" % (
            os.path.basename(files[i - 1]), os.path.basename(files[i]), diff.mean(),
            np.percentile(diff, 95), np.percentile(diff, 99.9), big))
    first = Image.open(files[0]).convert("RGB")
    heat = Image.fromarray(np.clip(maxdiff * 4, 0, 255).astype(np.uint8)).convert("RGB")
    w, h = first.size
    sheet = Image.new("RGB", (w, h * 2))
    sheet.paste(first, (0, 0))
    sheet.paste(heat, (0, h))
    sheet = sheet.resize((w // 2, h))
    sheet.save(out)
    print("wrote", out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
