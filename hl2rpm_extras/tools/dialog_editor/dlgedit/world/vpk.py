"""Valve pack files (VPK v1/v2): the directory file (<name>_dir.vpk) lists every file,
the data is in <name>_NNN.vpk archives or after the tree of the directory file."""
import os
import struct


class VPK:
    def __init__(self, dir_path):
        self.dir_path = dir_path
        self.base = dir_path[:-len("_dir.vpk")] if dir_path.lower().endswith("_dir.vpk") else dir_path[:-4]
        self.entries = {}       # "path/name.ext" (lower case, forward slashes) -> (archive, offset, length, preload)
        self._handles = {}
        self._read_tree()

    def _read_tree(self):
        with open(self.dir_path, "rb") as f:
            data = f.read()
        sig, version, tree_size = struct.unpack_from("<III", data, 0)
        if sig != 0x55AA1234:
            raise ValueError("not a VPK: %s" % self.dir_path)
        header = 12 if version == 1 else 28
        self.data_start = header + tree_size
        pos = header

        def cstr():
            nonlocal pos
            end = data.index(b"\0", pos)
            s = data[pos:end].decode("utf-8", "replace")
            pos = end + 1
            return s
        while True:
            ext = cstr()
            if not ext:
                break
            while True:
                path = cstr()
                if not path:
                    break
                while True:
                    name = cstr()
                    if not name:
                        break
                    crc, preload_len, archive, offset, length, term = struct.unpack_from("<IHHIIH", data, pos)
                    pos += 18
                    preload = data[pos:pos + preload_len] if preload_len else b""
                    pos += preload_len
                    full = (name + "." + ext) if path.strip() == "" else (path + "/" + name + "." + ext)
                    self.entries[full.lower()] = (archive, offset, length, preload)

    def has(self, rel):
        return rel.lower() in self.entries

    def read(self, rel):
        e = self.entries.get(rel.lower())
        if e is None:
            return None
        archive, offset, length, preload = e
        if length == 0:
            return preload
        if archive == 0x7FFF:
            path = self.dir_path
            offset += self.data_start
        else:
            path = "%s_%03d.vpk" % (self.base, archive)
        f = self._handles.get(path)
        if f is None:
            f = open(path, "rb")
            self._handles[path] = f
        f.seek(offset)
        return preload + f.read(length)

    def names(self):
        return self.entries.keys()

    def close(self):
        for f in self._handles.values():
            f.close()
        self._handles = {}


def dir_name(path):
    """"hl2/hl2_pak.vpk" in gameinfo means hl2/hl2_pak_dir.vpk on disk."""
    if path.lower().endswith("_dir.vpk"):
        return path
    return path[:-4] + "_dir.vpk"


def open_vpk(path):
    p = dir_name(path)
    if os.path.isfile(p):
        return VPK(p)
    if os.path.isfile(path):
        return VPK(path)
    return None
