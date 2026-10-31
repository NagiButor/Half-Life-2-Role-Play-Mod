"""Render window of the Creation Kit: the compiled map in 3D (textured world, static
props) with everything the project placed on it. Fly like in GECK/Hammer: hold the
right mouse button and use WASD (Q/E down/up, Shift faster), the wheel moves forward,
the middle button pans. Left click selects, dragging moves the object over the
surfaces, Alt+drag turns it, [ and ] turn by 15 degrees."""
import math
import time

import numpy as np
from PySide6.QtCore import QPoint, QPointF, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QFont, QPainter, QPen, QSurfaceFormat
from PySide6.QtOpenGLWidgets import QOpenGLWidget

from ..worlddata import MARKER_COLORS, NPC_DEFAULT_MODEL, PACKAGE_COLORS, PACKAGE_NAMES
from ..world.scene import ITEM_MODELS, is_npc_class, model_matrix
from . import render_gl as rgl
from .render_gl import GL


def _color(hexstr, a=1.0):
    c = QColor(hexstr)
    return (c.redF(), c.greenF(), c.blueF(), a)


def perspective(fovy, aspect, n, f):
    t = 1.0 / math.tan(math.radians(fovy) / 2)
    m = np.zeros((4, 4), dtype=np.float32)
    m[0, 0] = t / aspect
    m[1, 1] = t
    m[2, 2] = (f + n) / (n - f)
    m[2, 3] = 2 * f * n / (n - f)
    m[3, 2] = -1
    return m


class Camera:
    def __init__(self):
        self.pos = np.array([0.0, 0.0, 128.0])
        self.yaw = 0.0          # degrees, 0 = +x, 90 = +y (Source)
        self.pitch = -15.0      # degrees, up positive
        self.fov = 75.0

    def forward(self):
        y, p = math.radians(self.yaw), math.radians(self.pitch)
        return np.array([math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), math.sin(p)])

    def right(self):
        y = math.radians(self.yaw)
        return np.array([math.sin(y), -math.cos(y), 0.0])

    def up(self):
        return np.cross(self.right(), self.forward())

    def view(self):
        f = self.forward()
        r = self.right()
        u = np.cross(r, f)
        m = np.eye(4, dtype=np.float32)
        m[0, :3], m[1, :3], m[2, :3] = r, u, -f
        m[0, 3] = -r @ self.pos
        m[1, 3] = -u @ self.pos
        m[2, 3] = f @ self.pos
        return m

    def look_at(self, target, dist=None):
        target = np.asarray(target, float)
        d = target - self.pos
        if dist is not None:
            f = self.forward()
            self.pos = target - f * dist
            return
        ln = np.linalg.norm(d)
        if ln < 1:
            return
        self.yaw = math.degrees(math.atan2(d[1], d[0]))
        self.pitch = math.degrees(math.asin(max(-1, min(1, d[2] / ln))))


class WorldView(QOpenGLWidget):
    selected = Signal(object)            # ("ref", id) / ("pathpt", id, i) / ("hammer", index) / ("nav", i) / None
    edited = Signal(str)                 # what changed (for undo / status)
    placeRequested = Signal(object, object)
    contextRequested = Signal(object, object, object)   # target, global QPoint, world pos or None
    activated = Signal(object)           # double click on a target
    deleteRequested = Signal()
    duplicateRequested = Signal()
    status = Signal(str)

    def __init__(self, ctx, parent=None):
        super().__init__(parent)
        fmt = QSurfaceFormat()
        fmt.setVersion(3, 3)
        fmt.setProfile(QSurfaceFormat.CoreProfile)
        fmt.setDepthBufferSize(24)
        fmt.setSamples(4)
        self.setFormat(fmt)
        self.setFocusPolicy(Qt.StrongFocus)
        self.setMouseTracking(True)
        self.ctx = ctx
        self.cam = Camera()
        self.scene = None
        self.layer = None
        self.sel = None
        self.mode = "select"
        self.mode_cb = None
        self.mode_text = ""
        self.hour = 12.0
        self.focus_npc = ""
        self.opts = {"labels": True, "hammer": True, "nav": True, "packages": True, "props": True}
        self.gl_ready = False
        self.prog = self.lprog = None
        self.matcache = None
        self.world = None
        self.models = {}             # name -> ModelMesh / None
        self.groups = []             # static prop instance groups
        self.lines = None
        self.keys = set()
        self.speed = 700.0
        self.drag = None
        self.press_pos = None
        self.press_button = None
        self.last_mouse = QPoint()
        self.hover_world = None
        self.overlay_dirty = True
        self._overlay_ranges = {}
        self._labels = []
        self.timer = QTimer(self)
        self.timer.setInterval(16)
        self.timer.timeout.connect(self._tick)
        self._last_tick = time.perf_counter()
        self.frame_ms = 0.0
        self.error = ""

    # ------------------------------------------------------------------ setup
    def initializeGL(self):
        try:
            self.prog = rgl.Program(rgl.VS_MESH, rgl.FS_MESH)
            self.lprog = rgl.Program(rgl.VS_LINE, rgl.FS_LINE)
            self.lines = rgl.LineBuffer()
            self.gl_ready = True
        except Exception as e:
            self.error = "OpenGL 3.3: %s" % e
        if self.scene is not None:
            self._build_gpu()

    def set_map(self, scene, layer, matsys=None):
        self.makeCurrent()
        self._free_gpu()
        self.scene = scene
        self.layer = layer
        self.sel = None
        if scene is not None:
            self.matcache = rgl.MaterialCache(matsys or scene.materials) if self.gl_ready else None
            if layer is not None and layer.camera:
                c = layer.camera
                self.cam.pos = np.array(c[:3], float)
                self.cam.pitch, self.cam.yaw = c[3], c[4]
            else:
                o, a = scene.player_start()
                self.cam.pos = np.array(o, float) + [0, 0, 72]
                self.cam.yaw = a[1]
                self.cam.pitch = -10
            if self.gl_ready:
                self._build_gpu()
        self.overlay_dirty = True
        self.doneCurrent()
        self.update()

    def set_layer(self, layer):
        self.layer = layer
        self.overlay_dirty = True
        self.update()

    def _build_gpu(self):
        if self.scene is None or not self.gl_ready:
            return
        if self.matcache is None:
            self.matcache = rgl.MaterialCache(self.scene.materials)
        b = self.scene.bsp
        self.world = rgl.WorldMesh(b.batches)
        self.water = rgl.WorldMesh(b.water) if b.water else None
        for name, _, _ in self.world.ranges:
            self.matcache.get(name)
        # static props grouped by (model, skin)
        groups = {}
        for p in self.scene.props:
            groups.setdefault((p.model, p.skin), []).append(p.matrix)
        self.groups = []
        for (name, skin), mats in sorted(groups.items()):
            mm = self._model_mesh(name)
            if mm is None:
                continue
            self.groups.append(rgl.InstanceGroup(mm, skin, mats))

    def _model_mesh(self, name):
        if not name:
            return None
        name = name.replace("\\", "/").lower()
        if name in self.models:
            return self.models[name]
        mm = None
        m = self.scene.model(name) if self.scene else None
        if m is not None and m.ok:
            try:
                mm = rgl.ModelMesh(m)
                for i in range(len(m.materials)):
                    pass
                for skinref, _, _ in mm.parts:
                    for s in range(len(m.skins)):
                        self.matcache.get(m.material_for(skinref, s))
            except Exception:
                mm = None
        self.models[name] = mm
        return mm

    def _free_gpu(self):
        if not self.gl_ready:
            return
        for g in self.groups:
            g.delete()
        self.groups = []
        for m in self.models.values():
            if m is not None:
                m.delete()
        self.models = {}
        if self.world:
            self.world.delete()
            self.world = None
        if getattr(self, "water", None):
            self.water.delete()
            self.water = None
        if self.matcache:
            self.matcache.delete()
            self.matcache = None

    # ------------------------------------------------------------------ matrices / picking
    def proj(self):
        w, h = max(1, self.width()), max(1, self.height())
        return perspective(self.cam.fov, w / h, 4.0, 60000.0)

    def vp(self):
        return self.proj() @ self.cam.view()

    def ray(self, x, y):
        w, h = max(1, self.width()), max(1, self.height())
        nx = 2.0 * x / w - 1.0
        ny = 1.0 - 2.0 * y / h
        inv = np.linalg.inv(self.vp().astype(np.float64))
        a = inv @ np.array([nx, ny, -1.0, 1.0])
        b = inv @ np.array([nx, ny, 1.0, 1.0])
        a = a[:3] / a[3]
        b = b[:3] / b[3]
        d = b - a
        d /= np.linalg.norm(d)
        return self.cam.pos.copy(), d

    def cast(self, x, y):
        if self.scene is None:
            return None
        o, d = self.ray(x, y)
        r = self.scene.cast(o, d, 60000)
        if r is None:
            return None
        return o + d * r[0], r[1]

    def project(self, p):
        v = self.vp() @ np.array([p[0], p[1], p[2], 1.0])
        if v[3] <= 1e-3:
            return None
        x = (v[0] / v[3] * 0.5 + 0.5) * self.width()
        y = (0.5 - v[1] / v[3] * 0.5) * self.height()
        return QPointF(x, y), v[3]

    def _ref_center(self, r):
        if r.type == "npc":
            return [r.pos[0], r.pos[1], r.pos[2] + 40]
        if r.type in ("container", "prop", "item"):
            return [r.pos[0], r.pos[1], r.pos[2] + 12]
        return [r.pos[0], r.pos[1], r.pos[2] + 8]

    def pick(self, x, y):
        """The target under the mouse: refs (by screen distance or model box), path points,
        Hammer NPCs, nav nodes."""
        if self.scene is None:
            return None
        best = None
        mp = QPointF(x, y)
        o, d = self.ray(x, y)
        wall = self.scene.cast(o, d, 60000)
        wall_t = wall[0] if wall else 1e9

        def consider(target, world_pt, radius_px=16, t_hint=None):
            nonlocal best
            pr = self.project(world_pt)
            if pr is None:
                return
            sp, depth = pr
            dist = math.hypot(sp.x() - mp.x(), sp.y() - mp.y())
            if dist > radius_px:
                return
            t = np.linalg.norm(np.asarray(world_pt) - o)
            if t > wall_t + 48:
                return
            score = dist * 0.02 + t * 0.0005
            if best is None or score < best[0]:
                best = (score, target)
        if self.layer is not None:
            for r in self.layer.refs:
                if r.type == "path":
                    for i, pt in enumerate(r.points):
                        consider(("pathpt", r.id, i), [pt.pos[0], pt.pos[1], pt.pos[2] + 8], 12)
                    continue
                rad = 26 if r.type == "npc" else 18
                consider(("ref", r.id), self._ref_center(r), rad)
                if r.type == "npc":
                    consider(("ref", r.id), [r.pos[0], r.pos[1], r.pos[2] + 64], 18)
                    consider(("ref", r.id), [r.pos[0], r.pos[1], r.pos[2] + 16], 18)
            if self.mode == "nav" and self.opts["nav"]:
                for i, p in enumerate(self.layer.nav):
                    consider(("nav", i), p, 8)
        if self.opts["hammer"] and best is None:
            for (i, cls, o2, a2, e) in self.scene.entities:
                if is_npc_class(cls) or cls in ("info_player_start",):
                    consider(("hammer", i), [o2[0], o2[1], o2[2] + 40], 22)
        return best[1] if best else None

    # ------------------------------------------------------------------ painting
    def paintGL(self):
        t0 = time.perf_counter()
        GL.glClearColor(0.56, 0.63, 0.71, 1.0)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT | GL.GL_DEPTH_BUFFER_BIT)
        if not self.gl_ready or self.scene is None or self.world is None:
            self._paint_message(self.error or "Карта не открыта — выберите карту сверху")
            return
        GL.glEnable(GL.GL_DEPTH_TEST)
        GL.glDepthFunc(GL.GL_LEQUAL)
        GL.glDisable(GL.GL_CULL_FACE)
        GL.glDisable(GL.GL_BLEND)
        GL.glDepthMask(True)
        vp = self.vp()
        p = self.prog
        p.use()
        p.mat4("uVP", vp)
        p.v3("uCam", self.cam.pos)
        p.f("uFog", 9000.0)
        p.i("uTex", 0)
        p.i("uTex2", 1)
        p.i("uUnlit", 0)
        p.v3("uHighlight", (0, 0, 0))
        left = self.matcache.process(0.02)
        translucent = []
        # world
        p.i("uInstanced", 0)
        p.mat4("uModel", np.eye(4, dtype=np.float32))
        GL.glBindVertexArray(self.world.vao)
        for name, first, count in self.world.ranges:
            m = self.matcache.get(name)
            if m.translucent:
                translucent.append(("world", m, first, count))
                continue
            self._bind_material(m)
            GL.glDrawArrays(GL.GL_TRIANGLES, first, count)
        # static props
        if self.opts["props"]:
            p.i("uInstanced", 1)
            p.i("uBlend", 0)
            for g in self.groups:
                if np.linalg.norm(g.center - self.cam.pos) - g.radius > 14000:
                    continue
                GL.glBindVertexArray(g.vao)
                mdl = g.mesh.model
                for skinref, first, count in g.mesh.parts:
                    m = self.matcache.get(mdl.material_for(skinref, g.skin))
                    if m.translucent:
                        translucent.append(("group", m, g, first, count))
                        continue
                    self._bind_material(m, model=True)
                    GL.glDrawElementsInstanced(GL.GL_TRIANGLES, count, GL.GL_UNSIGNED_INT, rgl.ctypes.c_void_p(first * 4), g.count)
        # placed objects and Hammer NPCs
        p.i("uInstanced", 0)
        self._draw_ref_models(translucent)
        # translucent: water, glass
        GL.glEnable(GL.GL_BLEND)
        GL.glBlendFunc(GL.GL_SRC_ALPHA, GL.GL_ONE_MINUS_SRC_ALPHA)
        GL.glDepthMask(False)
        if self.water is not None:
            GL.glBindVertexArray(self.water.vao)
            p.mat4("uModel", np.eye(4, dtype=np.float32))
            for name, first, count in self.water.ranges:
                m = self.matcache.get(name)
                self._bind_material(m)
                GL.glDrawArrays(GL.GL_TRIANGLES, first, count)
        for t in translucent:
            if t[0] == "world":
                _, m, first, count = t
                p.i("uInstanced", 0)
                p.mat4("uModel", np.eye(4, dtype=np.float32))
                GL.glBindVertexArray(self.world.vao)
                self._bind_material(m)
                GL.glDrawArrays(GL.GL_TRIANGLES, first, count)
            elif t[0] == "group":
                _, m, g, first, count = t
                p.i("uInstanced", 1)
                GL.glBindVertexArray(g.vao)
                self._bind_material(m, model=True)
                GL.glDrawElementsInstanced(GL.GL_TRIANGLES, count, GL.GL_UNSIGNED_INT, rgl.ctypes.c_void_p(first * 4), g.count)
            else:
                _, m, mm, mat, first, count, hl = t
                p.i("uInstanced", 0)
                p.mat4("uModel", mat)
                GL.glBindVertexArray(mm.single_vao)
                self._bind_material(m, model=True)
                p.v3("uHighlight", hl)
                GL.glDrawElements(GL.GL_TRIANGLES, count, GL.GL_UNSIGNED_INT, rgl.ctypes.c_void_p(first * 4))
                p.v3("uHighlight", (0, 0, 0))
        GL.glDepthMask(True)
        # overlay lines / points
        self._draw_overlay(vp)
        GL.glBindVertexArray(0)
        GL.glDisable(GL.GL_BLEND)
        GL.glDisable(GL.GL_DEPTH_TEST)
        self.frame_ms = (time.perf_counter() - t0) * 1000
        self._paint_labels()
        # materials met while drawing (refs, props) were queued after `left` was taken
        if left or self.matcache.queue:
            QTimer.singleShot(0, self.update)

    def _bind_material(self, m, model=False):
        p = self.prog
        GL.glActiveTexture(GL.GL_TEXTURE0)
        GL.glBindTexture(GL.GL_TEXTURE_2D, m.tex)
        p.i("uHasTex", 1 if m.tex else 0)
        if m.tex2 and not model:
            GL.glActiveTexture(GL.GL_TEXTURE1)
            GL.glBindTexture(GL.GL_TEXTURE_2D, m.tex2)
            p.i("uBlend", 1)
            GL.glActiveTexture(GL.GL_TEXTURE0)
        else:
            p.i("uBlend", 0)
        p.i("uAlphaTest", 1 if m.alphatest else 0)
        p.v4("uTint", m.tint)

    def _ref_model_name(self, r):
        if r.type == "npc":
            npc = self.ctx.project.npc(r.npc) if self.ctx.project else None
            if npc is not None:
                return npc.actor.model_or_default()
            return NPC_DEFAULT_MODEL.get("npc_citizen")
        if r.type in ("container", "prop"):
            return r.model
        if r.type == "item":
            return ITEM_MODELS.get(r.classname, "")
        return ""

    def _draw_ref_models(self, translucent):
        p = self.prog
        items = []
        if self.layer is not None:
            for r in self.layer.refs:
                name = self._ref_model_name(r)
                if not name:
                    continue
                sel = self.sel is not None and self.sel[0] == "ref" and self.sel[1] == r.id
                hl = (0.28, 0.16, 0.0) if sel else (0, 0, 0)
                skin = r.skin
                if r.type == "npc":
                    npc = self.ctx.project.npc(r.npc)
                    skin = npc.actor.skin if npc else 0
                items.append((name, model_matrix(r.pos, [0, r.angles[1], 0] if r.type == "npc" else r.angles), skin, hl, 1.0))
        if self.opts["hammer"]:
            for (i, cls, o, a, e) in self.scene.entities:
                if not is_npc_class(cls):
                    continue
                name = e.get("model", "") or NPC_DEFAULT_MODEL.get(cls, "")
                if not name.endswith(".mdl"):
                    continue
                sel = self.sel is not None and self.sel[0] == "hammer" and self.sel[1] == i
                hl = (0.2, 0.12, 0.0) if sel else (0, 0, 0)
                items.append((name, model_matrix(o, [0, a[1], 0]), 0, hl, 0.8))
        for name, mat, skin, hl, shade in items:
            mm = self._model_mesh(name)
            if mm is None:
                continue
            p.mat4("uModel", mat)
            p.v3("uHighlight", hl)
            GL.glBindVertexArray(mm.single_vao)
            for skinref, first, count in mm.parts:
                m = self.matcache.get(mm.model.material_for(skinref, skin))
                if m.translucent:
                    translucent.append(("ref", m, mm, mat, first, count, hl))
                    continue
                self._bind_material(m, model=True)
                if shade != 1.0:
                    p.v4("uTint", (m.tint[0] * shade, m.tint[1] * shade, m.tint[2] * shade, m.tint[3]))
                GL.glDrawElements(GL.GL_TRIANGLES, count, GL.GL_UNSIGNED_INT, rgl.ctypes.c_void_p(first * 4))
        p.v3("uHighlight", (0, 0, 0))

    # ------------------------------------------------------------------ overlay
    def invalidate_overlay(self):
        self.overlay_dirty = True
        self.update()

    def _circle(self, out, c, r, col, seg=24, z=0.0):
        for k in range(seg):
            a0 = 2 * math.pi * k / seg
            a1 = 2 * math.pi * (k + 1) / seg
            out.append((c[0] + r * math.cos(a0), c[1] + r * math.sin(a0), c[2] + z) + col)
            out.append((c[0] + r * math.cos(a1), c[1] + r * math.sin(a1), c[2] + z) + col)

    def _arrow(self, out, c, yaw, length, col, z=4.0):
        y = math.radians(yaw)
        f = (math.cos(y), math.sin(y))
        s = (-f[1], f[0])
        tip = (c[0] + f[0] * length, c[1] + f[1] * length, c[2] + z)
        base = (c[0], c[1], c[2] + z)
        out.append(base + col)
        out.append(tip + col)
        for sg in (1, -1):
            out.append(tip + col)
            out.append((tip[0] - f[0] * 9 + s[0] * 6 * sg, tip[1] - f[1] * 9 + s[1] * 6 * sg, tip[2]) + col)

    def _box(self, out, lo, hi, mat, col):
        cs = []
        for x in (lo[0], hi[0]):
            for y in (lo[1], hi[1]):
                for z in (lo[2], hi[2]):
                    cs.append(mat[:3, :3] @ np.array([x, y, z]) + mat[:3, 3])
        for a, b in ((0, 1), (2, 3), (4, 5), (6, 7), (0, 2), (1, 3), (4, 6), (5, 7), (0, 4), (1, 5), (2, 6), (3, 7)):
            out.append(tuple(cs[a]) + col)
            out.append(tuple(cs[b]) + col)

    def _build_overlay(self):
        lines, points, big = [], [], []
        L = self.layer
        proj = self.ctx.project
        if L is not None:
            for r in L.refs:
                sel = self.sel is not None and self.sel[1] == r.id and self.sel[0] in ("ref", "pathpt")
                if r.type == "marker":
                    col = _color(MARKER_COLORS.get(r.kind, "#8a96a6"), 1.0)
                    if sel:
                        col = (1.0, 0.72, 0.3, 1.0)
                    self._circle(lines, r.pos, 16, col, 20, 2)
                    self._circle(lines, r.pos, 10, col, 14, 2)
                    self._arrow(lines, r.pos, r.angles[1], 30, col)
                    lines.append((r.pos[0], r.pos[1], r.pos[2]) + col)
                    lines.append((r.pos[0], r.pos[1], r.pos[2] + 40) + col)
                elif r.type == "path":
                    col = (0.95, 0.45, 0.4, 1.0) if not sel else (1.0, 0.75, 0.3, 1.0)
                    for i, pt in enumerate(r.points):
                        a = pt.pos
                        big.append((a[0], a[1], a[2] + 6) + ((1.0, 0.85, 0.3, 1.0) if (sel and self.sel[0] == "pathpt" and self.sel[2] == i) else col))
                        if i + 1 < len(r.points):
                            b = r.points[i + 1].pos
                            lines.append((a[0], a[1], a[2] + 6) + col)
                            lines.append((b[0], b[1], b[2] + 6) + col)
                        if pt.yaw is not None:
                            self._arrow(lines, a, pt.yaw, 24, col, 6)
                    if len(r.points) > 2:
                        a, b = r.points[-1].pos, r.points[0].pos
                        dim = (col[0], col[1], col[2], 0.35)
                        lines.append((a[0], a[1], a[2] + 6) + dim)
                        lines.append((b[0], b[1], b[2] + 6) + dim)
                elif r.type == "npc":
                    col = (0.4, 0.85, 1.0, 1.0) if not sel else (1.0, 0.72, 0.3, 1.0)
                    self._circle(lines, r.pos, 22, col, 20, 2)
                    self._arrow(lines, r.pos, r.angles[1], 40, col)
                elif r.type == "entity":
                    col = (0.8, 0.8, 0.85, 1.0) if not sel else (1.0, 0.72, 0.3, 1.0)
                    self._box(lines, (-8, -8, 0), (8, 8, 16), model_matrix(r.pos, r.angles), col)
                if sel and r.type in ("container", "prop", "item", "npc"):
                    name = self._ref_model_name(r)
                    m = self.scene.model(name) if name else None
                    if m is not None:
                        ang = [0, r.angles[1], 0] if r.type == "npc" else r.angles
                        self._box(lines, m.mins, m.maxs, model_matrix(r.pos, ang), (1.0, 0.72, 0.3, 1.0))
            # what the selected NPC does (packages): lines to their places at the chosen hour
            if self.opts["packages"] and proj is not None:
                for r in L.refs:
                    if r.type != "npc":
                        continue
                    npc = proj.npc(r.npc)
                    if npc is None or not npc.actor.packages:
                        continue
                    focus = (self.sel is not None and self.sel[0] == "ref" and self.sel[1] == r.id) or (self.focus_npc and self.focus_npc.lower() == r.npc.lower())
                    if not focus:
                        continue
                    for pkg in npc.actor.packages:
                        active = pkg is npc.actor.package_at(self.hour)
                        col = _color(PACKAGE_COLORS.get(pkg.type, "#ffffff"), 1.0 if active else 0.35)
                        for tgt in self._package_places(L, r, pkg):
                            lines.append((r.pos[0], r.pos[1], r.pos[2] + 50) + col)
                            lines.append((tgt[0], tgt[1], tgt[2] + 30) + col)
                        if pkg.type in ("wander", "sandbox") and (pkg.radius or True):
                            c = self._package_places(L, r, pkg)
                            c = c[0] if c else r.pos
                            self._circle(lines, c, pkg.radius or (512 if pkg.type == "wander" else 768), col, 48, 4)
            if self.opts["nav"] and L.nav:
                ncol = (1.0, 0.86, 0.25, 0.9) if self.mode == "nav" else (0.95, 0.8, 0.2, 0.55)
                for i, pnt in enumerate(L.nav):
                    if self.sel is not None and self.sel[0] == "nav" and self.sel[1] == i:
                        big.append((pnt[0], pnt[1], pnt[2] + 2) + (1.0, 0.4, 0.2, 1.0))
                    else:
                        points.append((pnt[0], pnt[1], pnt[2] + 2) + ncol)
        if self.opts["hammer"] and self.scene is not None:
            for (i, cls, o, a, e) in self.scene.entities:
                if cls == "info_player_start":
                    col = (0.4, 1.0, 0.45, 1.0)
                    self._circle(lines, o, 18, col, 16, 2)
                    self._arrow(lines, o, a[1], 36, col)
                elif is_npc_class(cls) and self.sel is not None and self.sel[0] == "hammer" and self.sel[1] == i:
                    self._circle(lines, o, 22, (1.0, 0.72, 0.3, 1.0), 20, 2)
        data = lines + points + big
        self._overlay_ranges = {"lines": (0, len(lines)), "points": (len(lines), len(points)), "big": (len(lines) + len(points), len(big))}
        self.lines.upload(np.array(data, dtype=np.float32).reshape(-1, 7) if data else np.zeros((0, 7), np.float32))
        self.overlay_dirty = False

    def _package_places(self, L, npc_ref, pkg):
        """Where a package sends the NPC on this map (for the lines)."""
        out = []
        if pkg.type == "patrol" and pkg.path:
            for r in L.refs:
                if r.type == "path" and r.name == pkg.path and r.points:
                    out.append(r.points[0].pos)
            return out
        if pkg.location == "ref" and pkg.ref:
            for r in L.refs:
                if r.name == pkg.ref:
                    out.append(r.pos)
            return out
        if pkg.type == "use_marker" and pkg.ref:
            for r in L.refs:
                if r.name == pkg.ref:
                    out.append(r.pos)
            return out
        if pkg.location == "point":
            return [pkg.point]
        if pkg.type in ("sleep", "eat"):
            kind = "sleep" if pkg.type == "sleep" else "eat"
            owned = [r for r in L.refs if r.type == "marker" and r.kind == kind and r.owner.lower() == npc_ref.npc.lower()]
            if owned:
                return [owned[0].pos]
            free = [r for r in L.refs if r.type == "marker" and r.kind == kind and not r.owner]
            if free:
                free.sort(key=lambda r: (r.pos[0] - npc_ref.pos[0]) ** 2 + (r.pos[1] - npc_ref.pos[1]) ** 2)
                return [free[0].pos]
        if pkg.location == "self":
            return [npc_ref.pos]
        return out

    def _draw_overlay(self, vp):
        if self.overlay_dirty:
            self._build_overlay()
        lp = self.lprog
        lp.use()
        lp.mat4("uVP", vp)
        GL.glEnable(GL.GL_PROGRAM_POINT_SIZE)
        GL.glEnable(GL.GL_BLEND)
        GL.glBlendFunc(GL.GL_SRC_ALPHA, GL.GL_ONE_MINUS_SRC_ALPHA)
        a, n = self._overlay_ranges.get("lines", (0, 0))
        lp.f("uPointSize", 1.0)
        # visible through walls a little: twice, the hidden part faint
        GL.glDepthFunc(GL.GL_GREATER)
        GL.glDepthMask(False)
        GL.glBlendColor(0, 0, 0, 0.25)
        GL.glBlendFunc(GL.GL_CONSTANT_ALPHA, GL.GL_ONE_MINUS_CONSTANT_ALPHA)
        self.lines.draw(GL.GL_LINES, a, n)
        GL.glBlendFunc(GL.GL_SRC_ALPHA, GL.GL_ONE_MINUS_SRC_ALPHA)
        GL.glDepthFunc(GL.GL_LEQUAL)
        self.lines.draw(GL.GL_LINES, a, n)
        a, n = self._overlay_ranges.get("points", (0, 0))
        lp.f("uPointSize", 5.0)
        self.lines.draw(GL.GL_POINTS, a, n)
        a, n = self._overlay_ranges.get("big", (0, 0))
        lp.f("uPointSize", 11.0)
        GL.glDepthFunc(GL.GL_ALWAYS)
        self.lines.draw(GL.GL_POINTS, a, n)
        GL.glDepthFunc(GL.GL_LEQUAL)
        GL.glDepthMask(True)

    def _paint_message(self, text):
        qp = QPainter(self)
        qp.setPen(QColor("#e8ecf1"))
        qp.setFont(QFont("Segoe UI", 12))
        qp.drawText(self.rect(), Qt.AlignCenter, text)
        qp.end()

    def _label_items(self):
        out = []
        L = self.layer
        proj = self.ctx.project
        lang = getattr(self.ctx, "display_lang", "english")
        base = proj.base_language if proj else "english"
        if L is not None:
            for r in L.refs:
                if r.type == "npc":
                    npc = proj.npc(r.npc) if proj else None
                    disp = ""
                    if npc is not None:
                        disp = npc.display_name.get(lang) or npc.display_name.get(base, "")
                    txt = disp or r.npc
                    sub = r.npc if disp else ""
                    if npc is not None and npc.actor.packages:
                        pk = npc.actor.package_at(self.hour)
                        sub = (sub + " · " if sub else "") + ("%02d:%02d %s" % (int(self.hour), int(self.hour * 60) % 60, pk.name if pk else "нет пакета"))
                    out.append(([r.pos[0], r.pos[1], r.pos[2] + 82], txt, sub, "#7fd6ff"))
                elif r.type == "marker":
                    from ..worlddata import MARKER_NAMES
                    txt = r.name or MARKER_NAMES.get(r.kind, r.kind)
                    sub = MARKER_NAMES.get(r.kind, r.kind) + ((" · " + r.owner) if r.owner else "")
                    out.append(([r.pos[0], r.pos[1], r.pos[2] + 46], txt, sub, MARKER_COLORS.get(r.kind, "#c8d0da")))
                elif r.type == "path" and r.points:
                    p0 = r.points[0].pos
                    out.append(([p0[0], p0[1], p0[2] + 30], r.name or "маршрут", "%d точек" % len(r.points), "#f2867e"))
                elif r.type in ("container", "item", "prop", "entity"):
                    sub = {"container": "контейнер", "item": "предмет", "prop": "проп", "entity": "сущность"}[r.type]
                    out.append(([r.pos[0], r.pos[1], r.pos[2] + 40], r.label(), sub, "#d8dde4"))
        if self.opts["hammer"] and self.scene is not None:
            for (i, cls, o, a, e) in self.scene.entities:
                if is_npc_class(cls):
                    tn = e.get("targetname", "")
                    out.append(([o[0], o[1], o[2] + 82], tn or cls, "Hammer · " + cls if tn else "Hammer", "#a9b2bd"))
        return out

    def _paint_labels(self):
        qp = QPainter(self)
        qp.setRenderHint(QPainter.TextAntialiasing)
        if self.opts["labels"]:
            f1 = QFont("Segoe UI", 9, QFont.Bold)
            f2 = QFont("Segoe UI", 8)
            items = []
            for pos, t, sub, col in self._label_items():
                d = np.linalg.norm(np.asarray(pos) - self.cam.pos)
                if d > 4500:
                    continue
                if d > 1600:
                    sub = ""          # far away: names only
                if d > 2400 and col not in ("#7fd6ff",):
                    continue          # far away: NPC names only
                pr = self.project(pos)
                if pr is None:
                    continue
                items.append((d, pr[0], t, sub, col))
            items.sort(key=lambda x: x[0])
            for d, sp, t, sub, col in items[:160]:
                qp.setFont(f1)
                qp.setPen(QColor(0, 0, 0, 170))
                qp.drawText(QPointF(sp.x() - 1 + 1, sp.y() + 1), t)
                qp.setPen(QColor(col))
                qp.drawText(QPointF(sp.x() - 1, sp.y()), t)
                if sub:
                    qp.setFont(f2)
                    qp.setPen(QColor(0, 0, 0, 160))
                    qp.drawText(QPointF(sp.x(), sp.y() + 14), sub)
                    qp.setPen(QColor("#d5dbe2"))
                    qp.drawText(QPointF(sp.x() - 1, sp.y() + 13), sub)
        # mode / hints
        qp.setFont(QFont("Segoe UI", 9))
        hint = self.mode_text or "ПКМ + WASD — полёт · колесо — вперёд/назад · ЛКМ — выделить и тащить · Alt+тащить — повернуть"
        qp.setPen(QColor(0, 0, 0, 150))
        qp.drawText(11, self.height() - 9, hint)
        qp.setPen(QColor("#ffd27a") if self.mode != "select" else QColor("#eef2f6"))
        qp.drawText(10, self.height() - 10, hint)
        qp.setPen(QColor("#eef2f6"))
        info = "%s · x %.0f y %.0f z %.0f · %.0f мс" % (self.scene.name if self.scene else "", self.cam.pos[0], self.cam.pos[1], self.cam.pos[2], self.frame_ms)
        qp.drawText(10, 18, info)
        qp.end()

    # ------------------------------------------------------------------ modes
    def set_mode(self, mode, text="", cb=None):
        self.mode = mode
        self.mode_text = text
        self.mode_cb = cb
        self.setCursor(Qt.CrossCursor if mode != "select" else Qt.ArrowCursor)
        self.overlay_dirty = True
        self.update()
        self.status.emit(text)

    def cancel_mode(self):
        if self.mode == "path" and self.mode_cb:
            self.mode_cb(None)
        self.set_mode("select")

    def select(self, target, emit=True):
        self.sel = target
        self.overlay_dirty = True
        self.update()
        if emit:
            self.selected.emit(target)

    def focus_on(self, pos, dist=360):
        pos = np.asarray(pos, float)
        f = self.cam.forward()
        self.cam.pos = pos - f * dist + [0, 0, 40]
        self.cam.look_at(pos)
        self.update()

    def focus_selected(self):
        p = self.target_pos(self.sel)
        if p is not None:
            self.focus_on([p[0], p[1], p[2] + 40])

    def target_pos(self, t):
        if t is None or self.scene is None:
            return None
        if t[0] == "ref" and self.layer and self.layer.ref(t[1]):
            r = self.layer.ref(t[1])
            if r.type == "path" and r.points:
                return r.points[0].pos
            return r.pos
        if t[0] == "pathpt" and self.layer and self.layer.ref(t[1]):
            r = self.layer.ref(t[1])
            return r.points[t[2]].pos if t[2] < len(r.points) else None
        if t[0] == "hammer":
            for (i, cls, o, a, e) in self.scene.entities:
                if i == t[1]:
                    return o
        if t[0] == "nav" and self.layer and t[1] < len(self.layer.nav):
            return self.layer.nav[t[1]]
        return None

    # ------------------------------------------------------------------ input
    def _tick(self):
        now = time.perf_counter()
        dt = min(0.1, now - self._last_tick)
        self._last_tick = now
        move = np.zeros(3)
        flying = self.press_button == Qt.RightButton
        k = self.keys
        f = self.cam.forward()
        r = self.cam.right()
        if flying:
            if Qt.Key_W in k:
                move += f
            if Qt.Key_S in k:
                move -= f
            if Qt.Key_D in k:
                move += r
            if Qt.Key_A in k:
                move -= r
            if Qt.Key_E in k:
                move += [0, 0, 1]
            if Qt.Key_Q in k:
                move -= [0, 0, 1]
        if Qt.Key_Up in k:
            move += f
        if Qt.Key_Down in k:
            move -= f
        if Qt.Key_Right in k:
            move += r
        if Qt.Key_Left in k:
            move -= r
        if np.linalg.norm(move) > 0:
            sp = self.speed * (3.0 if Qt.Key_Shift in k else 1.0) * (0.3 if Qt.Key_Control in k else 1.0)
            self.cam.pos += move / np.linalg.norm(move) * sp * dt
            self.update()
        elif not flying:
            self.timer.stop()

    def keyPressEvent(self, e):
        key = e.key()
        if e.isAutoRepeat() and key in self.keys:
            return
        self.keys.add(key)
        if key in (Qt.Key_W, Qt.Key_A, Qt.Key_S, Qt.Key_D, Qt.Key_Q, Qt.Key_E, Qt.Key_Up, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right):
            if not self.timer.isActive():
                self._last_tick = time.perf_counter()
                self.timer.start()
            return
        if key == Qt.Key_Escape:
            self.cancel_mode()
            return
        if key in (Qt.Key_Return, Qt.Key_Enter) and self.mode == "path":
            self.cancel_mode()
            return
        if key in (Qt.Key_BracketLeft, Qt.Key_BracketRight):
            self.rotate_selected((15 if key == Qt.Key_BracketLeft else -15) / (3 if e.modifiers() & Qt.ShiftModifier else 1))
            return
        if key == Qt.Key_Delete:
            self.deleteRequested.emit()
            return
        if key == Qt.Key_F:
            self.focus_selected()
            return
        if key == Qt.Key_D and e.modifiers() & Qt.ControlModifier:
            self.duplicateRequested.emit()
            return
        super().keyPressEvent(e)

    def keyReleaseEvent(self, e):
        if not e.isAutoRepeat():
            self.keys.discard(e.key())
        super().keyReleaseEvent(e)

    def focusOutEvent(self, e):
        self.keys.clear()
        super().focusOutEvent(e)

    def rotate_selected(self, delta):
        r = self._sel_ref()
        if r is None or r.type == "path":
            return
        r.angles[1] = (r.angles[1] + delta) % 360
        self.overlay_dirty = True
        self.update()
        self.edited.emit("rotate")

    def _sel_ref(self):
        if self.sel is not None and self.sel[0] == "ref" and self.layer is not None:
            return self.layer.ref(self.sel[1])
        return None

    def mousePressEvent(self, e):
        self.setFocus()
        self.press_pos = e.position()
        self.press_button = e.button()
        self.last_mouse = e.position().toPoint()
        self.drag = None
        if e.button() == Qt.RightButton:
            self._last_tick = time.perf_counter()
            self.timer.start()
            return
        if e.button() != Qt.LeftButton or self.scene is None:
            return
        x, y = e.position().x(), e.position().y()
        if self.mode == "place":
            hit = self.cast(x, y)
            if hit is not None:
                self.placeRequested.emit([float(c) for c in hit[0]], [float(c) for c in hit[1]])
                if not (e.modifiers() & Qt.ShiftModifier):
                    self.set_mode("select")
            return
        if self.mode in ("pick", "path"):
            hit = self.cast(x, y)
            if hit is not None and self.mode_cb:
                self.mode_cb([float(c) for c in hit[0]])
                if self.mode == "pick":
                    self.set_mode("select")
                self.overlay_dirty = True
                self.update()
            return
        if self.mode == "nav":
            t = self.pick(x, y)
            if t is not None and t[0] == "nav":
                self.select(t)
                self.drag = {"target": t, "moved": False}
                return
            hit = self.cast(x, y)
            if hit is not None and hit[1][2] > 0.6 and self.layer is not None:
                self.layer.nav.append([float(hit[0][0]), float(hit[0][1]), float(hit[0][2]) + 1])
                self.select(("nav", len(self.layer.nav) - 1))
                self.edited.emit("nav")
            return
        t = self.pick(x, y)
        self.select(t)
        if t is not None and t[0] in ("ref", "pathpt"):
            self.drag = {"target": t, "moved": False, "rotate": bool(e.modifiers() & Qt.AltModifier)}

    def mouseMoveEvent(self, e):
        pos = e.position().toPoint()
        dx = pos.x() - self.last_mouse.x()
        dy = pos.y() - self.last_mouse.y()
        self.last_mouse = pos
        if self.press_button == Qt.RightButton and e.buttons() & Qt.RightButton:
            self.cam.yaw -= dx * 0.22
            self.cam.pitch = max(-89.0, min(89.0, self.cam.pitch - dy * 0.22))
            self.update()
            return
        if self.press_button == Qt.MiddleButton and e.buttons() & Qt.MiddleButton:
            s = self.speed * 0.004
            self.cam.pos += -self.cam.right() * dx * s + self.cam.up() * dy * s
            self.update()
            return
        if self.drag and e.buttons() & Qt.LeftButton:
            if not self.drag["moved"] and (e.position() - self.press_pos).manhattanLength() < 4:
                return
            self.drag["moved"] = True
            hit = self.cast(e.position().x(), e.position().y())
            if hit is None:
                return
            p = [float(c) for c in hit[0]]
            t = self.drag["target"]
            if t[0] == "ref":
                r = self.layer.ref(t[1])
                if r is None:
                    return
                if self.drag.get("rotate"):
                    r.angles[1] = math.degrees(math.atan2(p[1] - r.pos[1], p[0] - r.pos[0])) % 360
                else:
                    if e.modifiers() & Qt.ControlModifier:
                        p[0] = round(p[0] / 16) * 16
                        p[1] = round(p[1] / 16) * 16
                    r.pos = p
            elif t[0] == "pathpt":
                r = self.layer.ref(t[1])
                if r is not None and t[2] < len(r.points):
                    r.points[t[2]].pos = p
            elif t[0] == "nav":
                self.layer.nav[t[1]] = [p[0], p[1], p[2] + 1]
            self.overlay_dirty = True
            self.update()

    def mouseReleaseEvent(self, e):
        if e.button() == Qt.RightButton:
            moved = self.press_pos is not None and (e.position() - self.press_pos).manhattanLength() > 4
            self.press_button = None
            if not (self.keys & {Qt.Key_Up, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right}):
                self.timer.stop()
            if not moved:
                if self.mode == "path":
                    self.cancel_mode()
                    return
                x, y = e.position().x(), e.position().y()
                t = self.pick(x, y)
                hit = self.cast(x, y)
                if t is not None:
                    self.select(t)
                self.contextRequested.emit(t, e.globalPosition().toPoint(), [float(c) for c in hit[0]] if hit else None)
            return
        if e.button() == Qt.LeftButton and self.drag and self.drag["moved"]:
            self.edited.emit("move")
        self.drag = None
        self.press_button = None

    def mouseDoubleClickEvent(self, e):
        if e.button() == Qt.LeftButton and self.sel is not None:
            self.activated.emit(self.sel)

    def wheelEvent(self, e):
        steps = e.angleDelta().y() / 120.0
        if self.press_button == Qt.RightButton:
            self.speed = max(60.0, min(6000.0, self.speed * (1.25 ** steps)))
            self.status.emit("Скорость полёта: %.0f" % self.speed)
            return
        self.cam.pos += self.cam.forward() * steps * max(64.0, self.speed * 0.35)
        self.update()

    def camera_state(self):
        return [float(self.cam.pos[0]), float(self.cam.pos[1]), float(self.cam.pos[2]), float(self.cam.pitch), float(self.cam.yaw)]
