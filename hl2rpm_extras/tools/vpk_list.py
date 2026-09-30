"""List (and optionally extract) files from Valve VPK v1/v2 directories.
usage: vpk_list.py <dir.vpk> [substring filter] [--extract outdir]
"""
import struct, sys, os

def read_cstr(f):
    b = bytearray()
    while True:
        c = f.read(1)
        if not c or c == b"\0":
            return b.decode("latin-1")
        b += c

def entries(path):
    with open(path, "rb") as f:
        sig, ver = struct.unpack("<II", f.read(8))
        assert sig == 0x55AA1234, path
        tree_size, = struct.unpack("<I", f.read(4))
        if ver == 2:
            f.read(16)
            header = 28
        else:
            header = 12
        while True:
            ext = read_cstr(f)
            if not ext:
                break
            while True:
                p = read_cstr(f)
                if not p:
                    break
                while True:
                    name = read_cstr(f)
                    if not name:
                        break
                    crc, preload, arc, off, length, term = struct.unpack("<IHHIIH", f.read(18))
                    pre = f.read(preload)
                    full = (p.strip() + "/" if p.strip() != "" else "") + name + "." + ext
                    yield full, arc, off, length, pre, header + tree_size

def extract(dirpath, item, outdir):
    full, arc, off, length, pre, dataoff = item
    data = pre
    if length:
        if arc == 0x7FFF:
            with open(dirpath, "rb") as f:
                f.seek(dataoff + off); data += f.read(length)
        else:
            arcpath = dirpath.replace("_dir.vpk", "_%03d.vpk" % arc)
            with open(arcpath, "rb") as f:
                f.seek(off); data += f.read(length)
    dst = os.path.join(outdir, full)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "wb") as f:
        f.write(data)
    return dst

if __name__ == "__main__":
    vpk = sys.argv[1]
    flt = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith("--") else ""
    out = None
    if "--extract" in sys.argv:
        out = sys.argv[sys.argv.index("--extract") + 1]
    for e in entries(vpk):
        if flt.lower() in e[0].lower():
            if out:
                print("extracted", extract(vpk, e, out))
            else:
                print(e[0], e[3] + len(e[4]))
