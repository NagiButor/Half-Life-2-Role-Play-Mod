# Minimal VTF reader: vtf.load(path, max_size=256) -> numpy uint8 array (h, w, 4) RGBA of the
# largest mip not bigger than max_size. Supports DXT1/DXT3/DXT5, RGBA8888, BGRA8888, BGR888, RGB888, I8, IA88, BGRX8888.
import struct
import numpy as np

FMT = {0: 'RGBA8888', 1: 'ABGR8888', 2: 'RGB888', 3: 'BGR888', 5: 'I8', 6: 'IA88', 12: 'BGRA8888',
       13: 'DXT1', 14: 'DXT3', 15: 'DXT5', 16: 'BGRX8888', 20: 'DXT1_ONEBITALPHA', 24: 'UV88'}


def size_of(fmt, w, h):
    if fmt in ('DXT1', 'DXT1_ONEBITALPHA'):
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * 8
    if fmt in ('DXT3', 'DXT5'):
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * 16
    bpp = {'RGBA8888': 4, 'ABGR8888': 4, 'RGB888': 3, 'BGR888': 3, 'I8': 1, 'IA88': 2, 'BGRA8888': 4, 'BGRX8888': 4, 'UV88': 2}[fmt]
    return w * h * bpp


def _c565(c):
    r = ((c >> 11) & 31) * 255 // 31
    g = ((c >> 5) & 63) * 255 // 63
    b = (c & 31) * 255 // 31
    return np.stack([r, g, b], -1).astype(np.int32)


def decode_dxt(data, w, h, fmt):
    bw, bh = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
    n = bw * bh
    bs = 8 if fmt.startswith('DXT1') else 16
    raw = np.frombuffer(data, np.uint8, n * bs).reshape(n, bs)
    col = raw[:, -8:]
    c0 = col[:, 0].astype(np.int32) | (col[:, 1].astype(np.int32) << 8)
    c1 = col[:, 2].astype(np.int32) | (col[:, 3].astype(np.int32) << 8)
    p0, p1 = _c565(c0), _c565(c1)
    four = (c0 > c1) | (fmt != 'DXT1' and fmt != 'DXT1_ONEBITALPHA')
    p2 = np.where(four[:, None], (2 * p0 + p1) // 3, (p0 + p1) // 2)
    p3 = np.where(four[:, None], (p0 + 2 * p1) // 3, 0)
    pal = np.stack([p0, p1, p2, p3], 1)  # n,4,3
    idx = col[:, 4:8].astype(np.uint32)
    bits = idx[:, 0] | (idx[:, 1] << 8) | (idx[:, 2] << 16) | (idx[:, 3] << 24)
    sel = np.stack([(bits >> (2 * i)) & 3 for i in range(16)], 1)  # n,16
    rgb = np.take_along_axis(pal, sel[:, :, None].astype(np.int64).repeat(3, 2), 1)  # n,16,3
    alpha = np.full((n, 16), 255, np.int32)
    if fmt.startswith('DXT1'):
        alpha = np.where((~four[:, None]) & (sel == 3), 0, 255)
    elif fmt == 'DXT3':
        a = raw[:, :8]
        vals = []
        for i in range(8):
            vals.append((a[:, i] & 15) * 17)
            vals.append((a[:, i] >> 4) * 17)
        alpha = np.stack(vals, 1).astype(np.int32)
    elif fmt == 'DXT5':
        a0 = raw[:, 0].astype(np.int32)
        a1 = raw[:, 1].astype(np.int32)
        ab = raw[:, 2:8].astype(np.uint64)
        abits = np.zeros(n, np.uint64)
        for i in range(6):
            abits |= ab[:, i] << np.uint64(8 * i)
        asel = np.stack([((abits >> np.uint64(3 * i)) & np.uint64(7)).astype(np.int32) for i in range(16)], 1)
        tab = np.zeros((n, 8), np.int32)
        tab[:, 0], tab[:, 1] = a0, a1
        big = a0 > a1
        for i in range(1, 7):
            tab[:, i + 1] = np.where(big, ((7 - i) * a0 + i * a1) // 7, tab[:, i + 1])
        for i in range(1, 5):
            tab[:, i + 1] = np.where(~big, ((5 - i) * a0 + i * a1) // 5, tab[:, i + 1])
        tab[:, 6] = np.where(~big, 0, tab[:, 6])
        tab[:, 7] = np.where(~big, 255, tab[:, 7])
        alpha = np.take_along_axis(tab, asel, 1)
    px = np.concatenate([rgb, alpha[:, :, None]], 2).reshape(bh, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)
    return px[:h, :w].astype(np.uint8)


def decode(data, w, h, fmt):
    if fmt.startswith('DXT'):
        return decode_dxt(data, w, h, fmt)
    a = np.frombuffer(data, np.uint8, size_of(fmt, w, h))
    if fmt == 'RGBA8888':
        return a.reshape(h, w, 4).copy()
    if fmt == 'ABGR8888':
        x = a.reshape(h, w, 4)
        return x[:, :, ::-1].copy()
    if fmt in ('BGRA8888', 'BGRX8888'):
        x = a.reshape(h, w, 4)[:, :, [2, 1, 0, 3]].copy()
        if fmt == 'BGRX8888':
            x[:, :, 3] = 255
        return x
    if fmt == 'RGB888':
        x = a.reshape(h, w, 3)
        return np.concatenate([x, np.full((h, w, 1), 255, np.uint8)], 2)
    if fmt == 'BGR888':
        x = a.reshape(h, w, 3)[:, :, ::-1]
        return np.concatenate([x, np.full((h, w, 1), 255, np.uint8)], 2)
    if fmt == 'I8':
        x = a.reshape(h, w, 1)
        return np.concatenate([x, x, x, np.full((h, w, 1), 255, np.uint8)], 2)
    if fmt == 'IA88':
        x = a.reshape(h, w, 2)
        return np.concatenate([x[:, :, :1]] * 3 + [x[:, :, 1:]], 2)
    if fmt == 'UV88':
        x = a.reshape(h, w, 2)
        return np.concatenate([x, np.full((h, w, 1), 255, np.uint8), np.full((h, w, 1), 255, np.uint8)], 2)
    raise ValueError(fmt)


def load(path, max_size=256):
    d = open(path, 'rb').read()
    assert d[:4] == b'VTF\0', path
    major, minor, hsize = struct.unpack_from('<III', d, 4)
    w, h, flags, frames, first = struct.unpack_from('<HHIHH', d, 16)
    fmt_id, = struct.unpack_from('<i', d, 52)
    mips = d[56]
    lfmt, lw, lh = struct.unpack_from('<iBB', d, 57)
    depth = 1
    if minor >= 2:
        depth, = struct.unpack_from('<H', d, 63)
    fmt = FMT.get(fmt_id)
    if fmt is None:
        raise ValueError('format %d' % fmt_id)
    faces = 6 if (flags & 0x4000) else 1
    # data start
    off = hsize
    if minor >= 3:
        # resource dictionary
        nres, = struct.unpack_from('<I', d, 68)
        img_off = None
        for i in range(nres):
            tag, fl, val = struct.unpack_from('<3sBI', d, 80 + i * 8)
            if tag == b'\x30\x00\x00':
                img_off = val
        off = img_off
    else:
        lfmt_name = FMT.get(lfmt)
        if lfmt_name and lw and lh:
            off += size_of(lfmt_name, lw, lh)
    # mips smallest first
    sizes = []
    for m in range(mips - 1, -1, -1):
        mw, mh = max(1, w >> m), max(1, h >> m)
        sizes.append((m, mw, mh, size_of(fmt, mw, mh) * frames * faces * depth))
    pick = None
    pos = off
    for m, mw, mh, sz in sizes:
        if max(mw, mh) <= max_size or pick is None:
            pick = (pos, mw, mh)
        pos += sz
    pos, mw, mh = pick
    return decode(d[pos:pos + size_of(fmt, mw, mh)], mw, mh, fmt), fmt, (w, h)
