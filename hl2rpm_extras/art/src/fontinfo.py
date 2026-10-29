# -*- coding: utf-8 -*-
"""Minimal TrueType inspector (no fontTools needed).

Prints, for each .ttf given on the command line:
  - names as Windows GDI sees them (name table, platform 3 / enc 1 / lang 0x409):
    ID 1 family (what CreateFont / clientscheme.res "name" must match), ID 2 subfamily,
    ID 4 full name, ID 6 PostScript name, ID 16/17 typographic family/subfamily;
  - OS/2 weight, fsType (embedding bits), fsSelection, code page bits (Latin-1, Cyrillic);
  - Cyrillic coverage of the cmap (basic Russian + Ukrainian/Belarusian letters + typographic signs).

Usage: python -I fontinfo.py font1.ttf [font2.ttf ...]   (add --json for machine output)
"""
import json
import struct
import sys


def read_tables(data):
    num = struct.unpack('>H', data[4:6])[0]
    tables = {}
    for i in range(num):
        off = 12 + 16 * i
        tag, _chk, toff, tlen = struct.unpack('>4sIII', data[off:off + 16])
        tables[tag.decode('latin-1')] = (toff, tlen)
    return tables


def names(data, tables):
    toff, _ = tables['name']
    _fmt, count, sto = struct.unpack('>HHH', data[toff:toff + 6])
    out = {}
    fallback = {}
    for i in range(count):
        r = toff + 6 + 12 * i
        pid, eid, lid, nid, ln, so = struct.unpack('>HHHHHH', data[r:r + 12])
        raw = data[toff + sto + so: toff + sto + so + ln]
        if pid == 3 and eid in (0, 1):
            s = raw.decode('utf-16-be', 'replace')
            if lid == 0x409:
                out[nid] = s
            else:
                fallback.setdefault(nid, s)
        elif pid == 1 and eid == 0:
            fallback.setdefault(nid, raw.decode('mac_roman', 'replace'))
    for k, v in fallback.items():
        out.setdefault(k, v)
    return out


def os2(data, tables):
    toff, tlen = tables['OS/2']
    ver = struct.unpack('>H', data[toff:toff + 2])[0]
    weight = struct.unpack('>H', data[toff + 4:toff + 6])[0]
    fstype = struct.unpack('>H', data[toff + 8:toff + 10])[0]
    fssel = struct.unpack('>H', data[toff + 62:toff + 64])[0]
    cp1 = struct.unpack('>I', data[toff + 78:toff + 82])[0] if ver >= 1 and tlen >= 82 else 0
    return {'version': ver, 'weight': weight, 'fsType': fstype, 'fsSelection': fssel,
            'cp_latin1': bool(cp1 & 1), 'cp_cyrillic1251': bool(cp1 & (1 << 2))}


def cmap_chars(data, tables):
    toff, _ = tables['cmap']
    n = struct.unpack('>H', data[toff + 2:toff + 4])[0]
    best = None
    for i in range(n):
        pid, eid, so = struct.unpack('>HHI', data[toff + 4 + 8 * i: toff + 12 + 8 * i])
        fmt = struct.unpack('>H', data[toff + so: toff + so + 2])[0]
        if pid == 3 and eid == 10 and fmt == 12:
            best = (fmt, toff + so)
            break
        if pid == 3 and eid == 1 and fmt == 4 and best is None:
            best = (fmt, toff + so)
    chars = set()
    if best is None:
        return chars
    fmt, o = best
    if fmt == 4:
        segx2 = struct.unpack('>H', data[o + 6:o + 8])[0]
        seg = segx2 // 2
        ends = struct.unpack('>%dH' % seg, data[o + 14:o + 14 + segx2])
        starts = struct.unpack('>%dH' % seg, data[o + 16 + segx2:o + 16 + 2 * segx2])
        deltas = struct.unpack('>%dh' % seg, data[o + 16 + 2 * segx2:o + 16 + 3 * segx2])
        ro_base = o + 16 + 3 * segx2
        ros = struct.unpack('>%dH' % seg, data[ro_base:ro_base + segx2])
        for s, e, d, ro, k in zip(starts, ends, deltas, ros, range(seg)):
            for c in range(s, e + 1):
                if c == 0xFFFF:
                    continue
                if ro == 0:
                    g = (c + d) & 0xFFFF
                else:
                    gi = ro_base + 2 * k + ro + 2 * (c - s)
                    g = struct.unpack('>H', data[gi:gi + 2])[0]
                    if g:
                        g = (g + d) & 0xFFFF
                if g:
                    chars.add(c)
    else:
        ng = struct.unpack('>I', data[o + 12:o + 16])[0]
        for i in range(ng):
            s, e, _g = struct.unpack('>III', data[o + 16 + 12 * i:o + 28 + 12 * i])
            chars.update(range(s, e + 1))
    return chars


RUSSIAN = [c for c in range(0x0410, 0x0450)] + [0x0401, 0x0451]
EXTRA_CYR = [0x0404, 0x0406, 0x0407, 0x040E, 0x0454, 0x0456, 0x0457, 0x045E, 0x0490, 0x0491]
SIGNS = [0x2116, 0x00AB, 0x00BB, 0x2014, 0x2013, 0x201E, 0x201C, 0x2026, 0x039B, 0x03BB, 0x00B2, 0x00B7, 0x2022]


def inspect(path):
    data = open(path, 'rb').read()
    t = read_tables(data)
    nm = names(data, t)
    o = os2(data, t)
    ch = cmap_chars(data, t)
    miss_ru = [chr(c) for c in RUSSIAN if c not in ch]
    miss_ex = [chr(c) for c in EXTRA_CYR if c not in ch]
    miss_sg = ['U+%04X' % c for c in SIGNS if c not in ch]
    return {
        'file': path,
        'family_id1': nm.get(1), 'subfamily_id2': nm.get(2), 'full_id4': nm.get(4),
        'postscript_id6': nm.get(6), 'typo_family_id16': nm.get(16), 'typo_sub_id17': nm.get(17),
        'version_id5': nm.get(5), 'license_url_id14': nm.get(14),
        'os2': o, 'glyph_chars': len(ch),
        'cyrillic_block_chars': len([c for c in ch if 0x0400 <= c <= 0x04FF]),
        'missing_russian': miss_ru, 'missing_ukr_bel': miss_ex, 'missing_signs': miss_sg,
        'has_variations': 'fvar' in t,
    }


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if a != '--json']
    res = [inspect(p) for p in args]
    if '--json' in sys.argv:
        print(json.dumps(res, ensure_ascii=False, indent=1))
    else:
        for r in res:
            print(r['file'])
            for k, v in r.items():
                if k != 'file':
                    print('   %-22s %s' % (k, v))
