"""Valve textures (.vtf 7.0-7.5): one mip level, either DXT-compressed (uploaded to
the GPU as is) or converted to RGBA8."""
import struct

import numpy as np

FMT_RGBA8888, FMT_ABGR8888, FMT_RGB888, FMT_BGR888, FMT_RGB565, FMT_I8, FMT_IA88 = 0, 1, 2, 3, 4, 5, 6
FMT_A8, FMT_RGB888_BS, FMT_BGR888_BS, FMT_ARGB8888, FMT_BGRA8888 = 8, 9, 10, 11, 12
FMT_DXT1, FMT_DXT3, FMT_DXT5, FMT_BGRX8888, FMT_BGR565 = 13, 14, 15, 16, 17
FMT_BGRX5551, FMT_BGRA4444, FMT_DXT1_1BIT, FMT_BGRA5551, FMT_UV88, FMT_UVWQ8888 = 18, 19, 20, 21, 22, 23
FMT_RGBA16F, FMT_RGBA16 = 24, 25

_BPP = {FMT_RGBA8888: 4, FMT_ABGR8888: 4, FMT_RGB888: 3, FMT_BGR888: 3, FMT_RGB565: 2, FMT_I8: 1, FMT_IA88: 2,
        FMT_A8: 1, FMT_RGB888_BS: 3, FMT_BGR888_BS: 3, FMT_ARGB8888: 4, FMT_BGRA8888: 4, FMT_BGRX8888: 4,
        FMT_BGR565: 2, FMT_BGRX5551: 2, FMT_BGRA4444: 2, FMT_BGRA5551: 2, FMT_UV88: 2, FMT_UVWQ8888: 4,
        FMT_RGBA16F: 8, FMT_RGBA16: 8}
DXT = {FMT_DXT1: "dxt1", FMT_DXT1_1BIT: "dxt1a", FMT_DXT3: "dxt3", FMT_DXT5: "dxt5"}


def mip_size(fmt, w, h):
    if fmt in (FMT_DXT1, FMT_DXT1_1BIT):
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * 8
    if fmt in (FMT_DXT3, FMT_DXT5):
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * 16
    return w * h * _BPP.get(fmt, 4)


class Texture:
    """kind "dxt1"/"dxt1a"/"dxt3"/"dxt5" (data = [(w, h, blocks)] from the picked mip level
    down) or "rgba" (data = HxWx4 uint8)."""
    __slots__ = ("width", "height", "kind", "data", "has_alpha", "flags")

    def __init__(self, width, height, kind, data, has_alpha, flags):
        self.width, self.height, self.kind, self.data, self.has_alpha, self.flags = width, height, kind, data, has_alpha, flags


TEXTUREFLAGS_ONEBITALPHA, TEXTUREFLAGS_EIGHTBITALPHA, TEXTUREFLAGS_ENVMAP = 0x1000, 0x2000, 0x4000


def load(data, max_size=512):
    if not data or data[:4] != b"VTF\0":
        return None
    major, minor, header_size = struct.unpack_from("<III", data, 4)
    width, height, flags, frames, first_frame = struct.unpack_from("<HHIHH", data, 16)
    hi_fmt = struct.unpack_from("<i", data, 52)[0]
    mips = data[56]
    lo_fmt = struct.unpack_from("<i", data, 57)[0]
    lo_w, lo_h = data[61], data[62]
    depth = struct.unpack_from("<H", data, 63)[0] if minor >= 2 else 1
    depth = max(1, depth)
    faces = 6 if flags & TEXTUREFLAGS_ENVMAP else 1
    if flags & TEXTUREFLAGS_ENVMAP and minor < 5 and first_frame == 0xFFFF:
        faces = 7
    hi_start = None
    if minor >= 3:
        num_res = struct.unpack_from("<I", data, 68)[0]
        for i in range(num_res):
            tag, rflags, ofs = struct.unpack_from("<3sBI", data, 80 + i * 8)
            if tag == b"\x30\x00\x00":
                hi_start = ofs
    if hi_start is None:
        lo_size = mip_size(lo_fmt, lo_w, lo_h) if lo_fmt >= 0 and lo_w and lo_h else 0
        hi_start = header_size + lo_size
    # mips are stored from the smallest to the largest
    sizes = []
    for m in range(mips):
        w = max(1, width >> m)
        h = max(1, height >> m)
        sizes.append((w, h, mip_size(hi_fmt, w, h) * frames * faces * max(1, depth >> m)))
    pick = 0
    while pick < mips - 1 and max(sizes[pick][0], sizes[pick][1]) > max_size:
        pick += 1
    ofs = hi_start + sum(s[2] for s in sizes[pick + 1:])
    w, h, _ = sizes[pick]
    one = mip_size(hi_fmt, w, h)
    raw = data[ofs:ofs + one]
    if len(raw) < one:
        return None
    alpha = bool(flags & (TEXTUREFLAGS_ONEBITALPHA | TEXTUREFLAGS_EIGHTBITALPHA))
    if hi_fmt in DXT:
        kind = DXT[hi_fmt]
        if hi_fmt == FMT_DXT1 and flags & TEXTUREFLAGS_ONEBITALPHA:
            kind = "dxt1a"
        # the picked level and every smaller one (stored before it): (w, h, bytes)
        levels = []
        o = ofs
        for m in range(pick, mips):
            mw, mh, total = sizes[m]
            size = mip_size(hi_fmt, mw, mh)
            if m > pick:
                o -= total
            chunk = data[o:o + size]
            if len(chunk) < size:
                break
            levels.append((mw, mh, chunk))
        return Texture(w, h, kind, levels, alpha or hi_fmt in (FMT_DXT3, FMT_DXT5), flags)
    rgba = to_rgba(hi_fmt, raw, w, h)
    if rgba is None:
        return None
    return Texture(w, h, "rgba", rgba, alpha, flags)


def to_rgba(fmt, raw, w, h):
    a = np.frombuffer(raw, dtype=np.uint8)
    n = w * h
    out = np.empty((n, 4), dtype=np.uint8)
    if fmt in (FMT_RGBA8888, FMT_ABGR8888, FMT_ARGB8888, FMT_BGRA8888, FMT_BGRX8888, FMT_UVWQ8888):
        p = a[:n * 4].reshape(n, 4)
        order = {FMT_RGBA8888: (0, 1, 2, 3), FMT_ABGR8888: (3, 2, 1, 0), FMT_ARGB8888: (1, 2, 3, 0),
                 FMT_BGRA8888: (2, 1, 0, 3), FMT_BGRX8888: (2, 1, 0, 3), FMT_UVWQ8888: (0, 1, 2, 3)}[fmt]
        out[:] = p[:, list(order)]
        if fmt == FMT_BGRX8888:
            out[:, 3] = 255
    elif fmt in (FMT_RGB888, FMT_BGR888, FMT_RGB888_BS, FMT_BGR888_BS):
        p = a[:n * 3].reshape(n, 3)
        out[:, :3] = p if fmt in (FMT_RGB888, FMT_RGB888_BS) else p[:, ::-1]
        out[:, 3] = 255
    elif fmt in (FMT_I8, FMT_A8):
        p = a[:n]
        if fmt == FMT_I8:
            out[:, 0] = out[:, 1] = out[:, 2] = p
            out[:, 3] = 255
        else:
            out[:, :3] = 255
            out[:, 3] = p
    elif fmt in (FMT_IA88, FMT_UV88):
        p = a[:n * 2].reshape(n, 2)
        out[:, 0] = out[:, 1] = out[:, 2] = p[:, 0]
        out[:, 3] = p[:, 1] if fmt == FMT_IA88 else 255
    elif fmt in (FMT_RGB565, FMT_BGR565, FMT_BGRA4444, FMT_BGRX5551, FMT_BGRA5551):
        v = np.frombuffer(raw[:n * 2], dtype="<u2").astype(np.uint32)
        if fmt in (FMT_RGB565, FMT_BGR565):
            r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
            r, g, b = r * 255 // 31, g * 255 // 63, b * 255 // 31
            if fmt == FMT_RGB565:
                r, b = b, r
            out[:, 0], out[:, 1], out[:, 2], out[:, 3] = b, g, r, 255
        elif fmt == FMT_BGRA4444:
            out[:, 0] = ((v >> 8) & 15) * 17
            out[:, 1] = ((v >> 4) & 15) * 17
            out[:, 2] = (v & 15) * 17
            out[:, 3] = ((v >> 12) & 15) * 17
        else:
            out[:, 0] = ((v >> 10) & 31) * 255 // 31
            out[:, 1] = ((v >> 5) & 31) * 255 // 31
            out[:, 2] = (v & 31) * 255 // 31
            out[:, 3] = np.where(v >> 15, 255, 0) if fmt == FMT_BGRA5551 else 255
    elif fmt in (FMT_RGBA16F,):
        p = np.frombuffer(raw[:n * 8], dtype="<f2").reshape(n, 4).astype(np.float32)
        p = p / (1.0 + p)     # a rough tone map for HDR sky textures
        out[:] = np.clip(p * 255, 0, 255).astype(np.uint8)
        out[:, 3] = 255
    else:
        return None
    return out.reshape(h, w, 4)
