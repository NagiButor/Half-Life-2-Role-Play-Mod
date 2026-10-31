"""Materials (.vmt) of the render window: base texture(s), alpha test / translucency,
water - and the textures themselves (cached per name)."""
import re

from .. import kv
from . import vtf


class MaterialInfo:
    __slots__ = ("name", "shader", "base", "base2", "alphatest", "translucent", "additive", "nocull",
                 "tint", "water", "fogcolor", "found")

    def __init__(self, name):
        self.name = name
        self.shader = ""
        self.base = ""
        self.base2 = ""
        self.alphatest = False
        self.translucent = False
        self.additive = False
        self.nocull = False
        self.tint = (1.0, 1.0, 1.0)
        self.water = False
        self.fogcolor = (0.25, 0.32, 0.35)
        self.found = False


def _color(s, default):
    if not s:
        return default
    nums = [float(x) for x in re.findall(r"-?\d*\.?\d+", s)]
    if len(nums) < 3:
        return default
    if "{" in s or max(nums[:3]) > 1.0:
        nums = [x / 255.0 for x in nums]
    return tuple(max(0.0, min(1.0, x)) for x in nums[:3])


def _flag(block, key):
    v = block.get(key, "0")
    try:
        return float(v) != 0
    except ValueError:
        return False


class MaterialSystem:
    def __init__(self, fs, max_texture=512):
        self.fs = fs
        self.max_texture = max_texture
        self.infos = {}
        self.textures = {}

    def _read_vmt(self, name, depth=0):
        data = self.fs.read("materials/%s.vmt" % name)
        if data is None or depth > 3:
            return None
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            text = data.decode("cp1251", "replace")
        root = kv.parse(text)
        blocks = root.blocks()
        if not blocks:
            return None
        b = blocks[0]
        if b.name.lower() == "patch":
            inc = b.get("include", "")
            inc = re.sub(r"^materials/", "", inc.replace("\\", "/").lower())
            inc = re.sub(r"\.vmt$", "", inc)
            base = self._read_vmt(inc, depth + 1)
            if base is None:
                return None
            for sect in ("insert", "replace"):
                s = b.block(sect)
                if s:
                    for c in s.values():
                        existing = [x for x in base.children if x.value is not None and x.name.lower() == c.name.lower()]
                        if existing:
                            existing[0].value = c.value
                        else:
                            base.add(c.name, c.value)
            return base
        return b

    def info(self, name):
        name = name.replace("\\", "/").lower()
        mi = self.infos.get(name)
        if mi is not None:
            return mi
        mi = MaterialInfo(name)
        b = self._read_vmt(name)
        if b is not None:
            mi.found = True
            mi.shader = b.name.lower()
            mi.base = (b.get("$basetexture", "") or "").replace("\\", "/").lower()
            mi.base2 = (b.get("$basetexture2", "") or "").replace("\\", "/").lower()
            mi.alphatest = _flag(b, "$alphatest")
            mi.translucent = _flag(b, "$translucent")
            mi.additive = _flag(b, "$additive")
            mi.nocull = _flag(b, "$nocull")
            mi.tint = _color(b.get("$color", ""), (1.0, 1.0, 1.0))
            mi.water = mi.shader == "water"
            if mi.water:
                mi.fogcolor = _color(b.get("$fogcolor", ""), mi.fogcolor)
                mi.translucent = True
            if not mi.base and mi.shader in ("unlitgeneric", "vertexlitgeneric", "lightmappedgeneric") and b.get("$envmap"):
                mi.tint = (0.6, 0.65, 0.7)
        self.infos[name] = mi
        return mi

    def texture(self, tex):
        """vtf.Texture of "materials/<tex>.vtf" (or None)."""
        if not tex:
            return None
        tex = tex.replace("\\", "/").lower()
        if tex.endswith(".vtf"):
            tex = tex[:-4]
        if tex in self.textures:
            return self.textures[tex]
        t = None
        data = self.fs.read("materials/%s.vtf" % tex)
        if data is not None:
            try:
                t = vtf.load(data, self.max_texture)
            except Exception:
                t = None
        self.textures[tex] = t
        return t
