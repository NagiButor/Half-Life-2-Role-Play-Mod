"""Temporal flicker ("crawl", "sand") metric for a series of frames.

    python crawl_stats.py <dir> <file prefix> <out.png> [x0 y0 x1 y1]

Uses the second difference |f[i+1] - 2 f[i] + f[i-1]| of the luma: about zero for
smooth motion (a shadow moving with the sun, clouds drifting), large where pixels
flicker. Prints the mean, the share of pixels above 10 per frame and the 99.9th
percentile; writes the first frame above a heat map of the mean second difference.
Record frames with `host_framerate 0.016667; startmovie <prefix> tga` (lossless:
jpeg blocks at sharp edges flicker on their own and inflate the numbers).
"""
import glob
import os
import sys

import numpy as np
from PIL import Image


def luma(path, box):
    im = Image.open(path).convert("RGB")
    if box:
        im = im.crop(box)
    a = np.asarray(im, dtype=np.float32)
    return a[..., 0] * 0.2126 + a[..., 1] * 0.7152 + a[..., 2] * 0.0722


def main(argv):
    d, prefix, out = argv[1], argv[2], argv[3]
    box = tuple(int(v) for v in argv[4:8]) if len(argv) >= 8 else None
    files = sorted(glob.glob(os.path.join(d, prefix + "*.*")))
    frames = [luma(f, box) for f in files][2:]	# the first movie frames are still settling
    if len(frames) < 3:
        print("need 5+ frames")
        return 1
    acc = np.zeros_like(frames[0])
    share = []
    for i in range(1, len(frames) - 1):
        s = np.abs(frames[i + 1] - 2 * frames[i] + frames[i - 1])
        acc += s
        share.append((s > 10).mean() * 100)
    mean = acc / (len(frames) - 2)
    print("%s: %d frames  mean2nd %.3f  px>10 per frame %.3f%% (max %.3f%%)  p99.9 %.2f" % (
        prefix, len(frames), mean.mean(), np.mean(share), np.max(share), np.percentile(mean, 99.9)))
    heat = Image.fromarray(np.clip(mean * 12, 0, 255).astype(np.uint8)).convert("RGB")
    first = Image.open(files[2]).convert("RGB")
    if box:
        first = first.crop(box)
    w, h = first.size
    sheet = Image.new("RGB", (w, h * 2))
    sheet.paste(first, (0, 0))
    sheet.paste(heat, (0, h))
    if w > 900:
        sheet = sheet.resize((w // 2, h))
    sheet.save(out)
    print("wrote", out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
