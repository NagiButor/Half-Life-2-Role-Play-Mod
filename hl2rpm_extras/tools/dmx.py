"""Binary DMX (versions 2..5) reader/writer with full fidelity (element ids,
attribute order and types). Used to build HL2RPM particle files (.pcf).

    from dmx import DMX, Element, Attr
    d = DMX.load("rain_fx.pcf")
    root = d.root                        # first element
    for e in d.elements: ...             # every element (e.type, e.name, e.id, e.attrs)
    e.get("name"); e.set("max_particles", 2000, "int")
    d.save("out.pcf")                    # same encoding version as loaded

Attribute types: element, int, float, bool, string, binary, time, color,
vector2, vector3, vector4, qangle, quaternion, matrix (arrays: "<type>_array").
"""
import struct
import uuid

TYPES = {1: "element", 2: "int", 3: "float", 4: "bool", 5: "string", 6: "binary", 7: "time", 8: "color",
         9: "vector2", 10: "vector3", 11: "vector4", 12: "qangle", 13: "quaternion", 14: "matrix"}
TYPE_IDS = {v: k for k, v in TYPES.items()}


class Attr:
    __slots__ = ("name", "type", "value")

    def __init__(self, name, type_, value):
        self.name = name
        self.type = type_          # e.g. "float", "vector3", "element_array"
        self.value = value         # element attributes hold Element objects (or None)

    def __repr__(self):
        return "Attr(%s:%s=%r)" % (self.name, self.type, self.value)


class Element:
    __slots__ = ("type", "name", "id", "attrs")

    def __init__(self, type_, name, id_=None):
        self.type = type_
        self.name = name
        self.id = id_ if id_ is not None else uuid.uuid4().bytes
        self.attrs = []            # ordered list of Attr

    def attr(self, name):
        for a in self.attrs:
            if a.name == name:
                return a
        return None

    def get(self, name, default=None):
        a = self.attr(name)
        return a.value if a else default

    def set(self, name, value, type_=None):
        a = self.attr(name)
        if a:
            a.value = value
            if type_:
                a.type = type_
            return a
        if type_ is None:
            raise ValueError("new attribute %s needs a type" % name)
        a = Attr(name, type_, value)
        self.attrs.append(a)
        return a

    def remove(self, name):
        self.attrs = [a for a in self.attrs if a.name != name]

    def __repr__(self):
        return "<%s %r>" % (self.type, self.name)


class _R:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def i16(self):
        v = struct.unpack_from("<h", self.d, self.p)[0]
        self.p += 2
        return v

    def i32(self):
        v = struct.unpack_from("<i", self.d, self.p)[0]
        self.p += 4
        return v

    def raw(self, n):
        v = self.d[self.p:self.p + n]
        self.p += n
        return v

    def cstr(self):
        e = self.d.index(b"\0", self.p)
        s = self.d[self.p:e].decode("latin-1")
        self.p = e + 1
        return s


class DMX:
    def __init__(self, header, version, elements):
        self.header = header        # the "<!-- dmx encoding binary N format pcf 1 -->" line
        self.version = version
        self.elements = elements

    @property
    def root(self):
        return self.elements[0]

    # ------------------------------------------------------------------ read
    @classmethod
    def load(cls, path):
        data = open(path, "rb").read()
        r = _R(data)
        header = r.cstr()
        ver = int(header.split("binary ")[1].split(" ")[0])
        strings = []
        if ver >= 2:
            n = r.i32() if ver >= 4 else r.i16()
            strings = [r.cstr() for _ in range(n)]

        def sidx():
            return r.i32() if ver >= 5 else r.i16()

        ne = r.i32()
        elems = []
        for _ in range(ne):
            typ = strings[sidx()] if ver >= 2 else r.cstr()
            name = strings[sidx()] if ver >= 4 else r.cstr()
            gid = r.raw(16)
            elems.append(Element(typ, name, gid))

        def value(t, in_array):
            if t == 1:
                i = r.i32()
                return elems[i] if 0 <= i < len(elems) else None
            if t == 2:
                return r.i32()
            if t == 3:
                return struct.unpack_from("<f", r.raw(4))[0]
            if t == 4:
                return bool(r.u8())
            if t == 5:
                if ver >= 4 and not in_array:
                    return strings[sidx()]
                return r.cstr()
            if t == 6:
                return r.raw(r.i32())
            if t == 7:
                return r.i32()          # time in 1/10000 s, kept raw
            if t == 8:
                return tuple(r.raw(4))
            n = {9: 2, 10: 3, 11: 4, 12: 3, 13: 4, 14: 16}[t]
            return struct.unpack("<%df" % n, r.raw(4 * n))

        for e in elems:
            na = r.i32()
            for _ in range(na):
                an = strings[sidx()] if ver >= 2 else r.cstr()
                t = r.u8()
                if t >= 15:
                    bt = t - 14
                    cnt = r.i32()
                    e.attrs.append(Attr(an, TYPES[bt] + "_array", [value(bt, True) for _ in range(cnt)]))
                else:
                    e.attrs.append(Attr(an, TYPES[t], value(t, False)))
        return cls(header, ver, elems)

    # ----------------------------------------------------------------- write
    def reachable(self):
        """elements reachable from the root, in a stable order (root first)"""
        seen = {}
        order = []
        stack = [self.root]
        while stack:
            e = stack.pop(0)
            if e is None or id(e) in seen:
                continue
            seen[id(e)] = True
            order.append(e)
            for a in e.attrs:
                if a.type == "element":
                    stack.append(a.value)
                elif a.type == "element_array":
                    stack.extend(a.value)
        return order

    def save(self, path, only_reachable=True):
        ver = self.version
        elems = self.reachable() if only_reachable else list(self.elements)
        index = {id(e): i for i, e in enumerate(elems)}

        # string table: element types, attribute names (+ names/strings for v4+)
        strings = []
        sindex = {}

        def sid(s):
            if s not in sindex:
                sindex[s] = len(strings)
                strings.append(s)
            return sindex[s]

        for e in elems:
            sid(e.type)
            if ver >= 4:
                sid(e.name)
            for a in e.attrs:
                sid(a.name)
                if ver >= 4 and a.type == "string":
                    sid(a.value)

        out = bytearray()
        out += self.header.encode("latin-1") + b"\0"

        def w16(v):
            out.extend(struct.pack("<h", v))

        def w32(v):
            out.extend(struct.pack("<i", v))

        def wsid(s):
            if ver >= 5:
                w32(sindex[s])
            else:
                w16(sindex[s])

        def wstr(s):
            out.extend(s.encode("latin-1") + b"\0")

        if ver >= 2:
            if ver >= 4:
                w32(len(strings))
            else:
                w16(len(strings))
            for s in strings:
                wstr(s)
        w32(len(elems))
        for e in elems:
            if ver >= 2:
                wsid(e.type)
            else:
                wstr(e.type)
            if ver >= 4:
                wsid(e.name)
            else:
                wstr(e.name)
            out.extend(e.id)

        def wvalue(t, v, in_array):
            if t == 1:
                w32(index[id(v)] if v is not None and id(v) in index else -1)
            elif t == 2:
                w32(int(v))
            elif t == 3:
                out.extend(struct.pack("<f", float(v)))
            elif t == 4:
                out.append(1 if v else 0)
            elif t == 5:
                if ver >= 4 and not in_array:
                    wsid(v)
                else:
                    wstr(v)
            elif t == 6:
                w32(len(v))
                out.extend(v)
            elif t == 7:
                w32(int(v))
            elif t == 8:
                out.extend(bytes(v))
            else:
                n = {9: 2, 10: 3, 11: 4, 12: 3, 13: 4, 14: 16}[t]
                out.extend(struct.pack("<%df" % n, *[float(x) for x in v]))

        for e in elems:
            w32(len(e.attrs))
            for a in e.attrs:
                if ver >= 2:
                    wsid(a.name)
                else:
                    wstr(a.name)
                if a.type.endswith("_array"):
                    bt = TYPE_IDS[a.type[:-6]]
                    out.append(bt + 14)
                    w32(len(a.value))
                    for v in a.value:
                        wvalue(bt, v, True)
                else:
                    t = TYPE_IDS[a.type]
                    out.append(t)
                    wvalue(t, a.value, False)
        with open(path, "wb") as f:
            f.write(out)
        return len(out)


def clone(e, memo=None, rename=None):
    """Deep copy of an element graph with new ids. rename(name, element) -> new name."""
    if memo is None:
        memo = {}
    if e is None:
        return None
    if id(e) in memo:
        return memo[id(e)]
    c = Element(e.type, rename(e.name, e) if rename else e.name)
    memo[id(e)] = c
    for a in e.attrs:
        if a.type == "element":
            v = clone(a.value, memo, rename)
        elif a.type == "element_array":
            v = [clone(x, memo, rename) for x in a.value]
        elif isinstance(a.value, list):
            v = list(a.value)
        else:
            v = a.value
        c.attrs.append(Attr(a.name, a.type, v))
    return c
