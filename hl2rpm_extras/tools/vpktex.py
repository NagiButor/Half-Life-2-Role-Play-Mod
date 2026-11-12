"""Read Valve VPK (v1/v2) directories and turn VTF textures into PNG previews.

usage:
  python vpktex.py list <dir.vpk> <substring>
  python vpktex.py png  <dir.vpk> <path/in/vpk.vtf> <out.png> [maxsize]
  python vpktex.py sheet <dir.vpk> <out.png> <path1.vtf> <path2.vtf> ...   (labelled grid)
  python vpktex.py mdlbox <dir.vpk> <models/x.mdl> ...                      (hull min/max of a model)

The HL2 VPKs: <Source SDK Base 2013 Singleplayer>/hl2/hl2_textures_dir.vpk (materials),
hl2_misc_dir.vpk (models). DXT1/DXT5 go through a DDS header to PIL. Read-only reference use.
"""
import io
import os
import struct
import sys

from PIL import Image, ImageDraw


def read_cstr(f):
    b = bytearray()
    while True:
        c = f.read(1)
        if not c or c == b"\0":
            return b.decode("latin-1")
        b += c


def load_dir(path):
    entries = {}
    with open(path, "rb") as f:
        sig, ver, tree = struct.unpack("<III", f.read(12))
        assert sig == 0x55AA1234, "not a vpk"
        if ver == 2:
            f.read(16)
        header = f.tell()
        while True:
            ext = read_cstr(f)
            if not ext:
                break
            while True:
                d = read_cstr(f)
                if not d:
                    break
                while True:
                    name = read_cstr(f)
                    if not name:
                        break
                    crc, preload, arch, off, length, term = struct.unpack("<IHHIIH", f.read(18))
                    pre = f.read(preload)
                    full = (d.strip() + "/" if d.strip() else "") + name + "." + ext
                    entries[full.lower()] = (arch, off, length, pre)
        data_start = header + tree
    return entries, data_start


def read_file(dirpath, entries, data_start, name):
    arch, off, length, pre = entries[name.lower()]
    if arch == 0x7FFF:
        with open(dirpath, "rb") as f:
            f.seek(data_start + off)
            return pre + f.read(length)
    base = dirpath[: -len("_dir.vpk")]
    with open("%s_%03d.vpk" % (base, arch), "rb") as f:
        f.seek(off)
        return pre + f.read(length)


FMT = {0: ("RGBA8888", 4), 2: ("RGB888", 3), 3: ("BGR888", 3), 12: ("BGRA8888", 4),
       13: ("DXT1", 0.5), 14: ("DXT3", 1), 15: ("DXT5", 1), 16: ("BGRX8888", 4)}


def mip_size(w, h, fmt):
    name, bpp = FMT[fmt]
    if name.startswith("DXT"):
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * (8 if name == "DXT1" else 16)
    return w * h * int(bpp)


def vtf_to_image(data):
    sig, vmaj, vmin, hsize, w, h, flags, frames, first = struct.unpack("<4sIIIHHIHH", data[:28])
    assert sig == b"VTF\0"
    fmt, mips, lrfmt, lrw, lrh = struct.unpack("<iBiBB", data[52:63])
    offset = hsize
    if vmin < 3:
        offset += mip_size(lrw, lrh, lrfmt) if lrfmt >= 0 else 0
    else:
        # resources: find the high-res image resource (tag 0x30)
        nres = struct.unpack("<I", data[68:72])[0]
        for i in range(nres):
            tag, rflags, roff = struct.unpack("<3sBI", data[80 + i * 8: 88 + i * 8])
            if tag == b"\x30\0\0":
                offset = roff
    # skip the smaller mips (stored smallest first)
    for m in range(mips - 1, 0, -1):
        mw, mh = max(1, w >> m), max(1, h >> m)
        offset += mip_size(mw, mh, fmt) * frames
    size = mip_size(w, h, fmt)
    raw = data[offset: offset + size]
    name = FMT[fmt][0]
    if name.startswith("DXT"):
        fourcc = name.encode()
        hdr = struct.pack("<4s7I44s2I4s5I5I", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000,
                          h, w, len(raw), 0, 1, b"\0" * 44, 32, 0x4, fourcc, 0, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0)
        return Image.open(io.BytesIO(hdr + raw)).convert("RGB")
    mode = {"RGBA8888": ("RGBA", "RGBA"), "RGB888": ("RGB", "RGB"), "BGR888": ("RGB", "BGR"),
            "BGRA8888": ("RGBA", "BGRA"), "BGRX8888": ("RGBA", "BGRX")}[name]
    return Image.frombytes(mode[0], (w, h), raw, "raw", mode[1]).convert("RGB")


def main():
    cmd, dirpath = sys.argv[1], sys.argv[2]
    entries, ds = load_dir(dirpath)
    if cmd == "list":
        sub = sys.argv[3].lower()
        for k in sorted(entries):
            if sub in k:
                print(k)
    elif cmd == "png":
        img = vtf_to_image(read_file(dirpath, entries, ds, sys.argv[3]))
        mx = int(sys.argv[5]) if len(sys.argv) > 5 else 1024
        img.thumbnail((mx, mx))
        img.save(sys.argv[4])
        print(img.size)
    elif cmd == "mdlbox":
        for n in sys.argv[3:]:
            b = read_file(dirpath, entries, ds, n)
            print("%-55s hull %s .. %s" % (n, tuple(round(x) for x in struct.unpack("<3f", b[104:116])),
                                           tuple(round(x) for x in struct.unpack("<3f", b[116:128]))))
    elif cmd == "sheet":
        out, names = sys.argv[3], sys.argv[4:]
        cell = 256
        cols = 6
        rows = (len(names) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * cell, rows * (cell + 16)), (30, 30, 30))
        dr = ImageDraw.Draw(sheet)
        for i, n in enumerate(names):
            try:
                img = vtf_to_image(read_file(dirpath, entries, ds, n))
                label = "%s %dx%d" % (os.path.basename(n)[:-4], img.width, img.height)
                img.thumbnail((cell, cell))
            except Exception as e:  # noqa
                img, label = Image.new("RGB", (cell, cell), (80, 0, 0)), os.path.basename(n) + " ERR " + str(e)[:20]
            x, y = (i % cols) * cell, (i // cols) * (cell + 16)
            sheet.paste(img, (x, y + 16))
            dr.text((x + 2, y + 2), label, fill=(255, 255, 0))
        sheet.save(out)
        print(out)


if __name__ == "__main__":
    main()
