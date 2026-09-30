"""Minimal Valve DMX binary (v2..v5) reader: dumps particle systems of a .pcf.
usage: dmx_dump.py file.pcf [system name filter]
"""
import struct, sys, uuid

class R:
    def __init__(self, data):
        self.d = data; self.p = 0
    def u8(self): v = self.d[self.p]; self.p += 1; return v
    def i32(self): v = struct.unpack_from("<i", self.d, self.p)[0]; self.p += 4; return v
    def f32(self): v = struct.unpack_from("<f", self.d, self.p)[0]; self.p += 4; return v
    def cstr(self):
        e = self.d.index(b"\0", self.p); s = self.d[self.p:e].decode("latin-1"); self.p = e + 1; return s
    def raw(self, n): v = self.d[self.p:self.p+n]; self.p += n; return v

AT = {1: "element", 2: "int", 3: "float", 4: "bool", 5: "string", 6: "binary", 7: "time", 8: "color",
      9: "vector2", 10: "vector3", 11: "vector4", 12: "qangle", 13: "quaternion", 14: "matrix"}

def read_value(r, t, strings, ver, in_array=False):
    if t == 1: return ("elem", r.i32())
    if t == 2: return r.i32()
    if t == 3: return r.f32()
    if t == 4: return bool(r.u8())
    if t == 5:
        if ver >= 4 and not in_array:
            idx = r.i32() if ver >= 5 else struct.unpack_from("<h", r.d, r.p)[0]
            if ver < 5: r.p += 2
            return strings[idx]
        return r.cstr()
    if t == 6: n = r.i32(); return r.raw(n)
    if t == 7: return r.i32() / 10000.0
    if t == 8: return tuple(r.raw(4))
    if t == 9: return struct.unpack("<2f", r.raw(8))
    if t == 10: return struct.unpack("<3f", r.raw(12))
    if t == 11: return struct.unpack("<4f", r.raw(16))
    if t == 12: return struct.unpack("<3f", r.raw(12))
    if t == 13: return struct.unpack("<4f", r.raw(16))
    if t == 14: return struct.unpack("<16f", r.raw(64))
    raise ValueError("type %d" % t)

def load(path):
    data = open(path, "rb").read()
    r = R(data)
    header = r.cstr()
    ver = int(header.split("binary ")[1].split(" ")[0])
    strings = []
    if ver >= 2:
        n = r.i32() if ver >= 4 else struct.unpack_from("<h", data, r.p)[0]
        if ver < 4: r.p += 2
        for _ in range(n): strings.append(r.cstr())
    ne = r.i32()
    elems = []
    def sidx():
        if ver >= 5: return r.i32()
        v = struct.unpack_from("<h", data, r.p)[0]; r.p += 2; return v
    for _ in range(ne):
        typ = strings[sidx()] if ver >= 2 else r.cstr()
        name = strings[sidx()] if ver >= 4 else r.cstr()
        guid = r.raw(16)
        elems.append({"_type": typ, "_name": name})
    for i in range(ne):
        na = r.i32()
        for _ in range(na):
            an = strings[sidx()] if ver >= 2 else r.cstr()
            t = r.u8()
            if t >= 15:
                bt = t - 14
                cnt = r.i32()
                val = [read_value(r, bt, strings, ver, True) for _ in range(cnt)]
            else:
                val = read_value(r, t, strings, ver)
            elems[i][an] = val
    return elems

def fmt(v, elems):
    if isinstance(v, tuple) and len(v) == 2 and v[0] == "elem":
        return "<%s>" % (elems[v[1]]["_name"] if 0 <= v[1] < len(elems) else "null")
    if isinstance(v, list):
        return "[" + ", ".join(fmt(x, elems) for x in v) + "]"
    if isinstance(v, bytes): return "<bin %d>" % len(v)
    if isinstance(v, float): return "%g" % v
    return repr(v)

if __name__ == "__main__":
    elems = load(sys.argv[1])
    flt = sys.argv[2] if len(sys.argv) > 2 else None
    for e in elems:
        if e["_type"] != "DmeParticleSystemDefinition": continue
        if flt and e["_name"] != flt: continue
        print("=== SYSTEM", e["_name"])
        for k, v in e.items():
            if k.startswith("_") or isinstance(v, list): continue
            print("   %s = %s" % (k, fmt(v, elems)))
        for key in ("renderers", "operators", "initializers", "emitters", "forces", "constraints", "children"):
            lst = e.get(key)
            if not lst: continue
            print("  [%s]" % key)
            for ref in lst:
                op = elems[ref[1]]
                print("    - %s" % op["_name"])
                for k, v in op.items():
                    if k.startswith("_") or k in ("functionName",): continue
                    s = fmt(v, elems)
                    print("        %s = %s" % (k, s[:120]))
