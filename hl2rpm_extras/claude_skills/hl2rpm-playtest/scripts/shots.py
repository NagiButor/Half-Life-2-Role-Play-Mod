#!/usr/bin/env python3
"""Screenshot helpers for HL2RPM tests (needs Pillow).

    python shots.py sheet  <out.jpg> [--cols 3] [--width 800] [--labels "a;b;c"] [files...]
        grid of the given images (default: all jpg in the mod's screenshots folder, sorted)
    python shots.py probe  <image> x,y [x,y ...]          RGB values at pixels
    python shots.py crop   <image> x1,y1,x2,y2 <out>      crop a region (to look at details)
    python shots.py diff   <a> <b> [<out>]                mean abs difference + optional diff image

Read the sheet/crop with the Read tool to look at it. A full-size 1600x900
shot is expensive to view; prefer a sheet (all steps at once) or a crop.
"""
import glob
import os
import sys

from PIL import Image, ImageChops, ImageDraw

SHOTS = r"E:\Steam\steamapps\sourcemods\hl2rpm\screenshots"


def sheet(args):
    out = args[0]
    cols, width, labels, files = 3, 800, None, []
    i = 1
    while i < len(args):
        a = args[i]
        if a == "--cols":
            cols = int(args[i + 1]); i += 2
        elif a == "--width":
            width = int(args[i + 1]); i += 2
        elif a == "--labels":
            labels = args[i + 1].split(";"); i += 2
        else:
            files.append(a); i += 1
    if not files:
        files = sorted(glob.glob(os.path.join(SHOTS, "*.jpg")))
    if not files:
        raise SystemExit("no images")
    ims = [Image.open(f).convert("RGB") for f in files]
    w = width
    h = int(ims[0].height * w / ims[0].width)
    rows = (len(ims) + cols - 1) // cols
    canvas = Image.new("RGB", (w * cols, h * rows))
    for n, im in enumerate(ims):
        im = im.resize((w, h), Image.LANCZOS)
        d = ImageDraw.Draw(im)
        text = "%d: %s" % (n, labels[n] if labels and n < len(labels) else os.path.basename(files[n]))
        d.rectangle([0, 0, 8 + 7 * len(text), 22], fill=(0, 0, 0))
        d.text((5, 5), text, fill=(255, 255, 0))
        canvas.paste(im, ((n % cols) * w, (n // cols) * h))
    canvas.save(out, quality=88)
    print("wrote %s (%dx%d, %d images)" % (out, canvas.width, canvas.height, len(ims)))


def probe(args):
    im = Image.open(args[0]).convert("RGB")
    for p in args[1:]:
        x, y = (int(v) for v in p.split(","))
        print("%s -> %s" % (p, im.getpixel((x, y))))


def crop(args):
    im = Image.open(args[0]).convert("RGB")
    x1, y1, x2, y2 = (int(v) for v in args[1].split(","))
    im.crop((x1, y1, x2, y2)).save(args[2], quality=92)
    print("wrote", args[2])


def diff(args):
    a = Image.open(args[0]).convert("RGB")
    b = Image.open(args[1]).convert("RGB").resize(a.size)
    d = ImageChops.difference(a, b)
    px = list(d.getdata())
    mean = sum(sum(p) for p in px) / (3.0 * len(px))
    print("mean abs diff %.2f / 255" % mean)
    if len(args) > 2:
        d.point(lambda v: min(255, v * 4)).save(args[2])
        print("wrote", args[2])


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    {"sheet": sheet, "probe": probe, "crop": crop, "diff": diff}[sys.argv[1]](sys.argv[2:])
