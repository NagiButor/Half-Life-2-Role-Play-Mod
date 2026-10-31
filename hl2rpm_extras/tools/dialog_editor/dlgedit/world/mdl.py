"""Studio models (.mdl v44-49 + .vvd + .dx90.vtx): LOD 0 of the default body in the
reference pose, split by material, skins, bounding box, sequence and activity names."""
import struct

import numpy as np


def _cstr(data, ofs):
    if ofs <= 0 or ofs >= len(data):
        return ""
    e = data.find(b"\0", ofs)
    return data[ofs:e if e >= 0 else len(data)].decode("utf-8", "replace")


class Mesh:
    __slots__ = ("material", "pos", "nrm", "uv", "idx")

    def __init__(self, material, pos, nrm, uv, idx):
        self.material, self.pos, self.nrm, self.uv, self.idx = material, pos, nrm, uv, idx


class Model:
    def __init__(self, name):
        self.name = name
        self.ok = False
        self.meshes = []            # Mesh with material = skin-ref index
        self.materials = []         # texture names (full: "models/props_c17/furniturebed001a")
        self.skins = [[]]           # skin family -> list of material indices per skin ref
        self.mins = np.array([-16, -16, 0], dtype=np.float32)
        self.maxs = np.array([16, 16, 72], dtype=np.float32)
        self.sequences = []         # (name, activity)
        self.includes = []          # included .mdl files (animations of humans etc.)
        self.error = ""

    def material_for(self, skinref, skin=0):
        fam = self.skins[skin] if 0 <= skin < len(self.skins) else self.skins[0]
        ti = fam[skinref] if skinref < len(fam) else skinref
        return self.materials[ti] if 0 <= ti < len(self.materials) else ""

    def triangles(self):
        if not self.meshes:
            return np.zeros((0, 3, 3), dtype=np.float32)
        return np.concatenate([m.pos[m.idx.reshape(-1)] for m in self.meshes]).reshape(-1, 3, 3)


def read_header(data, model):
    if data[:4] != b"IDST":
        raise ValueError("not an MDL")
    version = struct.unpack_from("<i", data, 4)[0]
    hull_min = struct.unpack_from("<3f", data, 104)
    hull_max = struct.unpack_from("<3f", data, 116)
    view_min = struct.unpack_from("<3f", data, 128)
    view_max = struct.unpack_from("<3f", data, 140)
    if any(view_min) or any(view_max):
        model.mins, model.maxs = np.array(view_min, np.float32), np.array(view_max, np.float32)
    else:
        model.mins, model.maxs = np.array(hull_min, np.float32), np.array(hull_max, np.float32)
    return version


def read_sequences(data):
    out = []
    numseq, seqindex = struct.unpack_from("<ii", data, 188)
    for i in range(min(numseq, 4096)):
        base = seqindex + i * 212
        if base + 12 > len(data):
            break
        lab, act = struct.unpack_from("<ii", data, base + 4)
        out.append((_cstr(data, base + lab), _cstr(data, base + act)))
    return out


def read_includes(data):
    n, idx = struct.unpack_from("<ii", data, 336)
    out = []
    for i in range(min(n, 64)):
        base = idx + i * 8
        lab, name = struct.unpack_from("<ii", data, base)
        s = _cstr(data, base + name).replace("\\", "/").lower()
        if s:
            out.append(s)
    return out


def load(fs, name):
    """name: "models/x/y.mdl" (game path)."""
    name = name.replace("\\", "/").lower()
    model = Model(name)
    data = fs.read(name)
    if data is None:
        model.error = "no mdl"
        return model
    try:
        version = read_header(data, model)
        model.sequences = read_sequences(data)
        model.includes = read_includes(data)
        # textures and their folders
        ntex, texidx, ncd, cdidx = struct.unpack_from("<iiii", data, 204)
        texnames = []
        for i in range(ntex):
            base = texidx + i * 64
            off = struct.unpack_from("<i", data, base)[0]
            texnames.append(_cstr(data, base + off).replace("\\", "/").lower())
        cds = []
        for i in range(ncd):
            off = struct.unpack_from("<i", data, cdidx + i * 4)[0]
            cds.append(_cstr(data, off).replace("\\", "/").lower())
        for t in texnames:
            found = ""
            for cd in cds or [""]:
                cand = (cd.rstrip("/") + "/" + t).lstrip("/") if cd else t
                if fs.exists("materials/%s.vmt" % cand):
                    found = cand
                    break
            model.materials.append(found or ((cds[0].rstrip("/") + "/" + t).lstrip("/") if cds else t))
        nref, nfam, skinidx = struct.unpack_from("<iii", data, 220)
        model.skins = []
        for f in range(max(1, nfam)):
            row = list(struct.unpack_from("<%dh" % nref, data, skinidx + f * nref * 2)) if nref else []
            model.skins.append(row)
        nbody, bodyidx = struct.unpack_from("<ii", data, 232)

        vvd = fs.read(name[:-4] + ".vvd")
        vtx = None
        for ext in (".dx90.vtx", ".dx80.vtx", ".sw.vtx", ".vtx"):
            vtx = fs.read(name[:-4] + ext)
            if vtx is not None:
                break
        if vvd is None or vtx is None:
            model.error = "no vvd/vtx"
            return model
        verts = _vvd_vertices(vvd)
        _build_meshes(model, data, nbody, bodyidx, vtx, verts)
        model.ok = bool(model.meshes)
    except Exception as e:      # a broken model must not break the map view
        model.error = str(e)
        model.ok = False
    return model


def _vvd_vertices(vvd):
    if vvd[:4] != b"IDSV":
        raise ValueError("not a VVD")
    numlods = struct.unpack_from("<i", vvd, 12)[0]
    lodverts = struct.unpack_from("<8i", vvd, 16)
    nfix, fixstart, vstart, tstart = struct.unpack_from("<iiii", vvd, 48)
    n0 = lodverts[0]
    dt = np.dtype([("w", "<f4", 3), ("b", "u1", 3), ("nb", "u1"), ("pos", "<f4", 3), ("nrm", "<f4", 3), ("uv", "<f4", 2)])
    total = (tstart - vstart) // 48 if tstart > vstart else n0
    allv = np.frombuffer(vvd, dtype=dt, count=max(total, n0), offset=vstart)
    if nfix > 0:
        parts = []
        for i in range(nfix):
            lod, src, num = struct.unpack_from("<iii", vvd, fixstart + i * 12)
            if lod >= 0:
                parts.append(allv[src:src + num])
        allv = np.concatenate(parts) if parts else allv[:n0]
    else:
        allv = allv[:n0]
    return allv


def _build_meshes(model, mdl, nbody, bodyidx, vtx, verts):
    version, cache, mbs, mbt, mbv, checksum, numlods, mrl, vnbody, vbodyofs = struct.unpack_from("<iiHHiiiiii", vtx, 0)
    for bi in range(min(nbody, vnbody)):
        bp = bodyidx + bi * 16
        bname, nmodels, bbase, modelidx = struct.unpack_from("<iiii", mdl, bp)
        if nmodels <= 0:
            continue
        mp = bp + modelidx          # model 0 = default
        nmeshes, meshidx, nverts, vertindex = struct.unpack_from("<iiii", mdl, mp + 72)
        model_vert0 = vertindex // 48
        # vtx side
        vbp = vbodyofs + bi * 8
        vnm, vmodelofs = struct.unpack_from("<ii", vtx, vbp)
        vmp = vbp + vmodelofs
        vnlod, vlodofs = struct.unpack_from("<ii", vtx, vmp)
        vlp = vmp + vlodofs        # LOD 0
        vnmesh, vmeshofs, switch = struct.unpack_from("<iif", vtx, vlp)
        for mi in range(min(nmeshes, vnmesh)):
            mmp = mp + meshidx + mi * 116
            material, mmodelidx, mnverts, mvertoffset = struct.unpack_from("<iiii", mdl, mmp)
            vmeshp = vlp + vmeshofs + mi * 9
            nsg, sgofs, mflags = struct.unpack_from("<iiB", vtx, vmeshp)
            pos_l, nrm_l, uv_l, idx_l = [], [], [], []
            base = 0
            for sgi in range(nsg):
                sgp = vmeshp + sgofs + sgi * 25
                nv, vofs, ni, iofs, ns, sofs, sgflags = struct.unpack_from("<iiiiiiB", vtx, sgp)
                if nv <= 0 or ni <= 0:
                    continue
                vt = np.frombuffer(vtx, dtype=np.dtype([("bw", "u1", 3), ("nb", "u1"), ("orig", "<u2"), ("bid", "i1", 3)]), count=nv, offset=sgp + vofs)
                ind = np.frombuffer(vtx, dtype="<u2", count=ni, offset=sgp + iofs).astype(np.int32)
                src = model_vert0 + mvertoffset + vt["orig"].astype(np.int64)
                src = np.clip(src, 0, len(verts) - 1)
                v = verts[src]
                pos_l.append(v["pos"])
                nrm_l.append(v["nrm"])
                uv_l.append(v["uv"])
                idx_l.append(ind[: (len(ind) // 3) * 3] + base)
                base += nv
            if not pos_l:
                continue
            model.meshes.append(Mesh(material, np.ascontiguousarray(np.concatenate(pos_l), dtype=np.float32),
                                     np.ascontiguousarray(np.concatenate(nrm_l), dtype=np.float32),
                                     np.ascontiguousarray(np.concatenate(uv_l), dtype=np.float32),
                                     np.ascontiguousarray(np.concatenate(idx_l), dtype=np.uint32)))


def sequence_names(fs, name, depth=0):
    """(sequence, activity) of a model and of the models it includes ($includemodel)."""
    data = fs.read(name.replace("\\", "/").lower())
    if data is None or data[:4] != b"IDST":
        return []
    out = read_sequences(data)
    if depth < 3:
        for inc in read_includes(data):
            out += sequence_names(fs, inc, depth + 1)
    return out
