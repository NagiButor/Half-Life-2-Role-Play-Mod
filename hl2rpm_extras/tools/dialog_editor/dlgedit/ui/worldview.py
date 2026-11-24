"""Render window of the Creation Kit: the compiled map in 3D (textured world, the objects
placed in Hammer) with everything the project placed on it.

Camera like Hammer: hold the right mouse button (or press Z for mouse look) and use WASD
(Q/E down/up, Shift faster, wheel while flying = speed). Left click selects (Ctrl toggles,
Shift adds, drag on empty space = frame), dragging an object moves it over the surfaces;
the gizmo moves along an axis / in a plane, a short right click on the selection switches
it to rings that turn the object. End lands the selection on what is below it."""
import math
import time

import numpy as np
from PySide6.QtCore import QPoint, QPointF, QRect, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QCursor, QFont, QPainter, QPen, QSurfaceFormat
from PySide6.QtOpenGLWidgets import QOpenGLWidget

from ..worlddata import MARKER_COLORS, NPC_DEFAULT_MODEL, PACKAGE_COLORS
from ..world import scene as scenemod
from ..world.scene import ENTITY_MODELS, ITEM_MODELS, axis_rotation, is_npc_class, matrix_angles, model_matrix
from ..world.bsp import angle_matrix
from . import render_gl as rgl
from .gizmo import Gizmo
from .render_gl import GL

scenemod.NPC_DEFAULT_MODELS.update(NPC_DEFAULT_MODEL)

SEL_COL = (1.0, 0.72, 0.3, 1.0)
LIGHT_CLASSES = ("light", "light_spot", "light_dynamic", "light_deferred", "light_deferred_spot", "light_environment",
                 "light_deferred_global", "env_projectedtexture")


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
    selected = Signal(object)            # ("ref", id) / ("pathpt", id, i) / ("obj", key) / ("nav", i) / None
    edited = Signal(str)                 # what changed (for undo / status)
    placeRequested = Signal(object, object)
    contextRequested = Signal(object, object, object)   # target, global QPoint, world pos or None
    activated = Signal(object)           # double click on a target
    deleteRequested = Signal()
    duplicateRequested = Signal()
    copyRequested = Signal()
    pasteRequested = Signal(object)      # world pos under the mouse or None
    status = Signal(str)
    gizmoModeChanged = Signal(str)
    dropped = Signal(object, object, object)    # payload, world pos, normal (dragged from the object window)

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
        self.setAcceptDrops(True)
        self.ctx = ctx
        self.cam = Camera()
        self.scene = None
        self.layer = None
        self.sel = None
        self.selset = []
        self.mode = "select"
        self.mode_cb = None
        self.mode_text = ""
        self.hour = 12.0
        self.focus_npc = ""
        self.opts = {"labels": True, "hammer": True, "nav": True, "packages": True, "props": True, "align": False, "anims": False,
                     "light": False, "night": False, "mannequins": True}
        self.mannequins = {}         # (model, sequence) -> [SkinnedMesh, frame it was posed in]
        self._frame_no = 0
        self._mannequins_seen = False
        # M7: the lighting preview (lamps of the map, the sun of the hour, cube shadows of the nearest 4)
        self.shadow_cubes = []
        self._lights = None
        self._lights_time = 0.0
        self.light_stats = (0, 0)
        # M7: NPCs of the project animated (the package of the chosen hour)
        self.skinned = {}            # ref id -> (model name, SkinnedMesh)
        self.animsets = {}           # model name -> AnimSet
        self.anim_t0 = time.perf_counter()
        self.anim_timer = QTimer(self)
        self.anim_timer.setInterval(50)
        self.anim_timer.timeout.connect(lambda: (self.opts.get("anims") or (self.opts.get("mannequins") and self._mannequins_seen))
                                        and self.isVisible() and self.update())
        self.anim_timer.start()
        self.gl_ready = False
        self.prog = self.lprog = None
        self.matcache = None
        self.world = None
        self.water = None
        self.models = {}             # name -> ModelMesh / None
        self.groups = {}             # (model, skin) -> InstanceGroup of map objects
        self._group_objs = {}        # (model, skin) -> [MapObject]
        self._dirty_groups = set()
        self.lines = None
        self.gizbuf = None
        self.gizmo = Gizmo()
        self.keys = set()
        self.speed = 700.0
        self.drag = None
        self.press_pos = None
        self.press_button = None
        self.last_mouse = QPoint()
        self.mouselook = False
        self._warp = None
        self.rubber = None
        self.overlay_dirty = True
        self._overlay_ranges = {}
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
            self.gizbuf = rgl.LineBuffer()
            self.gl_ready = True
        except Exception as e:
            self.error = "OpenGL 3.3: %s" % e
        if self.scene is not None:
            self._build_gpu()

    def set_map(self, scene, layer, matsys=None, keep_camera=False):
        self.makeCurrent()
        self._free_gpu()
        self.scene = scene
        self.layer = layer
        self.sel = None
        self.selset = []
        self._update_gizmo()
        if scene is not None:
            self.matcache = rgl.MaterialCache(matsys or scene.materials) if self.gl_ready else None
            if keep_camera:
                pass
            elif layer is not None and layer.camera:
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
        self._prune_selection()
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
        self.refresh_objects()

    def refresh_objects(self):
        """Regroup the map objects (after an add / delete / model change / undo)."""
        if not self.gl_ready or self.scene is None:
            return
        self.makeCurrent()
        for g in self.groups.values():
            g.delete()
        self.groups = {}
        self._group_objs = {}
        for ob in self.scene.objects:
            name = ob.draw_model()
            if not name or ob.is_npc():
                continue
            self._group_objs.setdefault((name, ob.skin), []).append(ob)
        self._dirty_groups = set(self._group_objs)
        self.scene.invalidate()
        self.overlay_dirty = True
        self.update()

    def object_moved(self, ob):
        name = ob.draw_model()
        if name and not ob.is_npc():
            self._dirty_groups.add((name, ob.skin))
        self.scene.invalidate()

    def _rebuild_dirty_groups(self):
        for key in list(self._dirty_groups):
            g = self.groups.pop(key, None)
            if g is not None:
                g.delete()
            objs = self._group_objs.get(key, [])
            mm = self._model_mesh(key[0])
            if objs and mm is not None:
                self.groups[key] = rgl.InstanceGroup(mm, key[1], [o.matrix for o in objs])
        self._dirty_groups = set()

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
        for g in self.groups.values():
            g.delete()
        self.groups = {}
        self._group_objs = {}
        self._dirty_groups = set()
        for _n, sm in self.skinned.values():
            sm.delete()
        self.skinned = {}
        for sm, _f in self.mannequins.values():
            sm.delete()
        self.mannequins = {}
        for c in self.shadow_cubes:
            c.delete()
        self.shadow_cubes = []
        self._lights = None
        for m in self.models.values():
            if m is not None:
                m.delete()
        self.models = {}
        if self.world:
            self.world.delete()
            self.world = None
        if self.water:
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

    def ref_models(self, skip=()):
        """[(("ref", id), model, matrix)] of the project's objects that have a model (for ray casts)."""
        out = []
        if self.layer is None:
            return out
        for r in self.layer.refs:
            key = ("ref", r.id)
            if key in skip:
                continue
            name = self._ref_model_name(r)
            if name:
                out.append((key, name, model_matrix(r.pos, [0, r.angles[1], 0] if r.type == "npc" else r.angles)))
        return out

    def cast(self, x, y, skip=()):
        """(point, normal) of the surface under the mouse (world, map objects, project objects)."""
        if self.scene is None:
            return None
        o, d = self.ray(x, y)
        r = self.scene.cast(o, d, 60000, skip=skip, extra=self.ref_models(skip))
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
        if r.type in ("container", "prop", "item", "terminal"):
            return [r.pos[0], r.pos[1], r.pos[2] + 12]
        return [r.pos[0], r.pos[1], r.pos[2] + 8]

    def _obj_visible(self, ob):
        if ob.draw_model() and not ob.is_npc():
            return self.opts["props"]
        return self.opts["hammer"]

    def pick(self, x, y):
        """The target under the mouse: a model hit by the ray (map objects, project objects),
        else the nearest marker / point / entity icon on screen."""
        if self.scene is None:
            return None
        best = None
        mp = QPointF(x, y)
        o, d = self.ray(x, y)
        hidden = tuple(ob.key for ob in self.scene.objects if not self._obj_visible(ob))
        hit = self.scene.cast(o, d, 60000, skip=hidden, extra=self.ref_models())
        wall_t = hit[0] if hit else 1e9
        if hit is not None and hit[2] is not None:
            k = hit[2]
            best = (0.0, k if k[0] == "ref" else ("obj", k))

        def consider(target, world_pt, radius_px=16):
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
            score = dist * 0.02 + t * 0.0005 - 0.5     # icons win over the surface behind them
            if best is None or score < best[0]:
                best = (score, target)
        if self.layer is not None:
            for r in self.layer.refs:
                if r.type == "path":
                    for i, pt in enumerate(r.points):
                        consider(("pathpt", r.id, i), [pt.pos[0], pt.pos[1], pt.pos[2] + 8], 12)
                    continue
                if r.type in ("marker", "entity") or not self._ref_model_name(r):
                    consider(("ref", r.id), self._ref_center(r), 18)
                elif r.type == "npc":
                    consider(("ref", r.id), [r.pos[0], r.pos[1], r.pos[2] + 64], 14)
            if self.mode == "nav" and self.opts["nav"]:
                for i, p in enumerate(self.layer.nav):
                    consider(("nav", i), p, 8)
        if self.opts["hammer"]:
            for ob in self.scene.objects:
                if ob.draw_model():
                    continue
                consider(("obj", ob.key), [ob.origin[0], ob.origin[1], ob.origin[2] + 6], 14)
        return best[1] if best else None

    # ------------------------------------------------------------------ targets
    def _obj(self, t):
        if t is not None and t[0] == "obj" and self.scene is not None:
            return self.scene.find(t[1])
        return None

    def _ref(self, t):
        if t is not None and t[0] in ("ref", "pathpt") and self.layer is not None:
            return self.layer.ref(t[1])
        return None

    def t_pos(self, t):
        if t is None:
            return None
        if t[0] == "ref":
            r = self._ref(t)
            if r is None:
                return None
            if r.type == "path":
                return list(r.points[0].pos) if r.points else None
            return list(r.pos)
        if t[0] == "pathpt":
            r = self._ref(t)
            return list(r.points[t[2]].pos) if r is not None and t[2] < len(r.points) else None
        if t[0] == "obj":
            ob = self._obj(t)
            return list(ob.origin) if ob else None
        if t[0] == "nav" and self.layer and t[1] < len(self.layer.nav):
            return list(self.layer.nav[t[1]])
        return None

    target_pos = t_pos

    def t_angles(self, t):
        if t is None:
            return None
        if t[0] == "ref":
            r = self._ref(t)
            return list(r.angles) if r is not None and r.type != "path" else None
        if t[0] == "obj":
            ob = self._obj(t)
            return list(ob.angles) if ob else None
        return None

    def t_model(self, t):
        if t is None:
            return ""
        if t[0] == "ref":
            r = self._ref(t)
            return self._ref_model_name(r) if r is not None else ""
        if t[0] == "obj":
            ob = self._obj(t)
            return ob.draw_model() if ob else ""
        return ""

    def t_editable(self, t):
        if t is None:
            return False
        if t[0] == "obj":
            ob = self._obj(t)
            return ob is not None and ob.editable
        return t[0] in ("ref", "pathpt", "nav")

    def t_yaw_only(self, t):
        if t[0] == "ref":
            r = self._ref(t)
            return r is None or r.type in ("npc", "marker", "path")
        if t[0] == "obj":
            ob = self._obj(t)
            return ob is None or ob.is_npc() or ob.classname.startswith("info_")
        return True

    def t_set(self, t, pos=None, angles=None):
        if t[0] == "ref":
            r = self._ref(t)
            if r is None:
                return
            if r.type == "path":
                if pos is not None and r.points:
                    off = np.array(pos) - np.array(r.points[0].pos)
                    for p in r.points:
                        p.pos = [float(x) for x in np.array(p.pos) + off]
                return
            if pos is not None:
                r.pos = [float(x) for x in pos]
            if angles is not None:
                r.angles = [float(x) for x in angles]
        elif t[0] == "pathpt":
            r = self._ref(t)
            if r is not None and t[2] < len(r.points) and pos is not None:
                r.points[t[2]].pos = [float(x) for x in pos]
        elif t[0] == "obj":
            ob = self._obj(t)
            if ob is not None and ob.editable:
                ob.set_transform(pos, angles)
                self.object_moved(ob)
        elif t[0] == "nav" and self.layer and t[1] < len(self.layer.nav) and pos is not None:
            self.layer.nav[t[1]] = [float(x) for x in pos]

    def t_label(self, t):
        if t[0] == "obj":
            ob = self._obj(t)
            return ob.label() if ob else ""
        r = self._ref(t)
        return r.label() if r else ""

    # ------------------------------------------------------------------ painting
    def paintGL(self):
        t0 = time.perf_counter()
        GL.glClearColor(0.56, 0.63, 0.71, 1.0)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT | GL.GL_DEPTH_BUFFER_BIT)
        if not self.gl_ready or self.scene is None or self.world is None:
            self._paint_message(self.error or "Карта не открыта — выберите карту сверху")
            return
        if self._dirty_groups:
            self._rebuild_dirty_groups()
        light = self._setup_lighting() if self.opts.get("light") else None
        if light is not None:
            sky = light[4]
            GL.glClearColor(float(sky[0]), float(sky[1]), float(sky[2]), 1.0)
            GL.glClear(GL.GL_COLOR_BUFFER_BIT | GL.GL_DEPTH_BUFFER_BIT)
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
        for k in range(4):
            p.i("uShadow%d" % k, 2 + k)
        p.v3("uFogCol", light[4] if light is not None else (0.56, 0.63, 0.71))
        self._apply_lighting(light)
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
        # map objects with models (static / dynamic / physics props), instanced
        if self.opts["props"]:
            p.i("uInstanced", 1)
            p.i("uBlend", 0)
            for g in self.groups.values():
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
        # project objects, Hammer NPCs, the selection again with a highlight
        p.i("uInstanced", 0)
        self._draw_single_models(translucent)
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
        # overlay lines / points, gizmo
        self._draw_overlay(vp)
        self._draw_gizmo(vp)
        GL.glBindVertexArray(0)
        GL.glDisable(GL.GL_BLEND)
        GL.glDisable(GL.GL_DEPTH_TEST)
        self.frame_ms = (time.perf_counter() - t0) * 1000
        self._paint_labels()
        # materials met while drawing (refs, props) were queued after `left` was taken
        if left or self.matcache.queue:
            QTimer.singleShot(0, self.update)

    # ------------------------------------------------------------------ lighting preview (M7)
    def _setup_lighting(self):
        """Picks the lamps (nearest 32, shadows for the nearest 4 that cast them), renders the shadow cubes
        that changed. Returns (lamps, slots, sun, ambient, sky...) for _apply_lighting."""
        from ..world import lights as lightmod
        now = time.perf_counter()
        if self._lights is None or now - self._lights_time > 0.5:
            self._lights = lightmod.map_lights(self.scene)
            self._lights_time = now
        cam = self.cam.pos
        lamps = sorted((l for l in self._lights if np.linalg.norm(l.pos - cam) < 4000 + l.radius),
                       key=lambda l: np.linalg.norm(l.pos - cam))[:32]
        while len(self.shadow_cubes) < 4:
            self.shadow_cubes.append(rgl.ShadowCube(512))
        if not hasattr(self, "dprog"):
            self.dprog = rgl.Program(rgl.VS_DIST, rgl.FS_DIST)
        slots = {}
        casters = [l for l in lamps if l.shadow][:4]
        rendered = False
        for k, l in enumerate(casters):
            cube = self.shadow_cubes[k]
            key = (tuple(np.round(l.pos, 1)), round(l.radius, 1))
            if cube.key != key:
                cube.render(self.dprog, l.pos, l.radius, self._draw_world_positions)
                cube.key = key
                rendered = True
            slots[id(l)] = k
        if rendered:
            GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.defaultFramebufferObject())
            dpr = self.devicePixelRatioF()
            GL.glViewport(0, 0, int(self.width() * dpr), int(self.height() * dpr))
        for k in range(len(casters)):
            GL.glActiveTexture(GL.GL_TEXTURE2 + k)
            GL.glBindTexture(GL.GL_TEXTURE_CUBE_MAP, self.shadow_cubes[k].tex)
        GL.glActiveTexture(GL.GL_TEXTURE0)
        sun = lightmod.sun(self.scene, self.hour, self.opts.get("night"))
        self.light_stats = (len(lamps), len(casters))
        return (lamps, slots) + sun

    def _draw_world_positions(self):
        GL.glEnable(GL.GL_DEPTH_TEST)
        GL.glDisable(GL.GL_CULL_FACE)
        GL.glBindVertexArray(self.world.vao)
        GL.glDrawArrays(GL.GL_TRIANGLES, 0, self.world.count)
        GL.glBindVertexArray(0)

    def _apply_lighting(self, light):
        p = self.prog
        if light is None:
            p.i("uLightMode", 0)
            return
        lamps, slots, sdir, scol, aup, adn, sky = light
        p.i("uLightMode", 1)
        p.v3("uSunDir", sdir)
        p.v3("uSunCol", scol)
        p.v3("uAmbUp", aup)
        p.v3("uAmbDown", adn)
        n = len(lamps)
        p.i("uNumLights", n)
        if not n:
            return
        pos = np.zeros((32, 4), np.float32)
        col = np.zeros((32, 4), np.float32)
        spot = np.zeros((32, 4), np.float32)
        inner = np.zeros(32, np.float32)
        sh = np.full(32, -1, np.int32)
        for i, l in enumerate(lamps):
            pos[i] = (l.pos[0], l.pos[1], l.pos[2], l.radius)
            col[i] = (l.color[0], l.color[1], l.color[2], l.power)
            if l.spot is not None:
                spot[i] = (l.spot[0], l.spot[1], l.spot[2], l.cos_outer)
                inner[i] = l.cos_inner
            else:
                spot[i] = (0, 0, 0, -2.0)
            sh[i] = slots.get(id(l), -1)
        GL.glUniform4fv(p.u("uLPos"), 32, pos)
        GL.glUniform4fv(p.u("uLCol"), 32, col)
        GL.glUniform4fv(p.u("uLSpot"), 32, spot)
        GL.glUniform1fv(p.u("uLInner"), 32, inner)
        GL.glUniform1iv(p.u("uLShadow"), 32, sh)

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
        if r is None:
            return ""
        if r.type == "npc":
            npc = self.ctx.project.npc(r.npc) if self.ctx.project else None
            if npc is not None:
                return npc.actor.model_or_default()
            return NPC_DEFAULT_MODEL.get("npc_citizen")
        if r.type in ("container", "prop"):
            return r.model
        if r.type == "terminal":
            from ..termdata import DEFAULT_MODEL
            t = self.ctx.project.terminal(r.terminal) if self.ctx.project is not None else None
            return r.model or DEFAULT_MODEL.get(t.style if t else "pc", DEFAULT_MODEL["pc"])
        if r.type == "item":
            it = self.ctx.project.item(r.classname) if self.ctx.project is not None else None
            if it is not None and it.model:
                return it.model
            if it is not None and it.base:
                return ITEM_MODELS["item_rpm_key" if it.base == "key" else "item_rpm"]
            return ITEM_MODELS.get(r.classname, "")
        if r.type == "entity":
            return ENTITY_MODELS.get(r.classname) or NPC_DEFAULT_MODEL.get(r.classname) or r.keyvalues.get("model", "")
        return ""

    def _is_selected(self, t):
        return t in self.selset

    def _draw_single_models(self, translucent):
        p = self.prog
        items = []
        self._frame_no += 1
        self._mannequins_seen = False
        if self.layer is not None and self.opts.get("mannequins"):
            for r in self.layer.refs:
                if r.type != "marker":
                    continue
                posed = self._mannequin(r)
                if posed is not None:
                    name, sm, mat = posed
                    hl = (0.28, 0.16, 0.0) if self._is_selected(("ref", r.id)) else (0.02, 0.05, 0.1)
                    items.append((name, mat, 0, hl, 0.9, sm))
                    self._mannequins_seen = True
        if self.layer is not None:
            for r in self.layer.refs:
                name = self._ref_model_name(r)
                if not name:
                    continue
                hl = (0.28, 0.16, 0.0) if self._is_selected(("ref", r.id)) else (0, 0, 0)
                skin = r.skin
                if r.type == "npc":
                    npc = self.ctx.project.npc(r.npc)
                    skin = npc.actor.skin if npc else 0
                    if self.opts.get("anims") and npc is not None:
                        posed = self._npc_posed(r, npc, name)
                        if posed is not None:
                            sm, mat = posed
                            items.append((name, mat, skin, hl, 1.0, sm))
                            continue
                items.append((name, model_matrix(r.pos, [0, r.angles[1], 0] if r.type == "npc" else r.angles), skin, hl, 1.0))
        for ob in self.scene.objects:
            name = ob.draw_model()
            if not name:
                continue
            sel = self._is_selected(("obj", ob.key))
            if ob.is_npc():
                if not self.opts["hammer"]:
                    continue
                items.append((name, ob.matrix, ob.skin, (0.2, 0.12, 0.0) if sel else (0, 0, 0), 0.8))
            elif sel and self.opts["props"]:
                items.append((name, ob.matrix, ob.skin, (0.28, 0.16, 0.0), 1.0))
        for it in items:
            name, mat, skin, hl, shade = it[:5]
            sm = it[5] if len(it) > 5 else None
            mm = self._model_mesh(name)
            if mm is None:
                continue
            p.mat4("uModel", mat)
            p.v3("uHighlight", hl)
            GL.glBindVertexArray(sm.vao if sm is not None else mm.single_vao)
            for skinref, first, count in mm.parts:
                m = self.matcache.get(mm.model.material_for(skinref, skin))
                if m.translucent:
                    translucent.append(("ref", m, sm if sm is not None else mm, mat, first, count, hl))
                    continue
                self._bind_material(m, model=True)
                if shade != 1.0:
                    p.v4("uTint", (m.tint[0] * shade, m.tint[1] * shade, m.tint[2] * shade, m.tint[3]))
                GL.glDrawElements(GL.GL_TRIANGLES, count, GL.GL_UNSIGNED_INT, rgl.ctypes.c_void_p(first * 4))
        p.v3("uHighlight", (0, 0, 0))

    def _mannequin(self, r):
        """(model, SkinnedMesh, matrix) of the figure on a marker playing its animation (GECK-like), or None.
        Markers with the same model and animation share one pose per frame."""
        fs = getattr(self.ctx, "fs", None)
        if fs is None or not hasattr(self.ctx, "marker_sequence"):
            return None
        if np.linalg.norm(np.asarray(r.pos) - self.cam.pos) > 2500:
            return None
        from ..world import anim as animmod
        from ..worlddata import DEFAULT_MARKER_ANIM, NPC_DEFAULT_MODEL
        owner = self.ctx.project.npc(r.owner) if r.owner and self.ctx.project else None
        name = owner.actor.model_or_default() if owner else NPC_DEFAULT_MODEL.get("npc_citizen", "models/humans/group01/male_07.mdl")
        anim_name = r.anim or DEFAULT_MARKER_ANIM.get(r.kind, "") or "ACT_IDLE"
        seq_name = self.ctx.marker_sequence(anim_name) or "ACT_IDLE"
        a = self.animsets.get(name)
        if a is None:
            a = animmod.AnimSet(fs, name)
            self.animsets[name] = a
        mm = self._model_mesh(name)
        if not a.ok or mm is None:
            return None
        blend = r.seat_h if getattr(r, "seat_h", -1) >= 0 else None
        key = (name, seq_name.lower(), None if blend is None else round(blend, 1))
        cur = self.mannequins.get(key)
        if cur is None:
            cur = [rgl.SkinnedMesh(mm), -1]
            self.mannequins[key] = cur
        sm = cur[0]
        if cur[1] != self._frame_no:
            seq = a.find(seq_name) or a.find("ACT_IDLE")
            pos, quat = a.pose(seq, time.perf_counter() - self.anim_t0, blend)
            pp, nn = animmod.skin(sm.pos, sm.nrm, sm.bw, sm.bb, a.skin_matrices(a.matrices(pos, quat)))
            sm.update(pp, nn)
            cur[1] = self._frame_no
        return name, sm, model_matrix(r.pos, [0, r.angles[1], 0])

    def _npc_posed(self, r, npc, name):
        """(SkinnedMesh, matrix) of an NPC of the project playing its package of the chosen hour:
        on its marker with the marker's animation, else at its place, idle. None if it can't animate."""
        fs = getattr(self.ctx, "fs", None)
        if fs is None or not hasattr(self.ctx, "npc_preview_sequence"):
            return None
        from ..world import anim as animmod
        a = self.animsets.get(name)
        if a is None:
            a = animmod.AnimSet(fs, name)
            self.animsets[name] = a
        if not a.ok:
            return None
        mm = self._model_mesh(name)
        if mm is None:
            return None
        cur = self.skinned.get(r.id)
        if cur is None or cur[0] != name:
            if cur is not None:
                cur[1].delete()
            cur = (name, rgl.SkinnedMesh(mm))
            self.skinned[r.id] = cur
        sm = cur[1]
        pk = npc.actor.package_at(self.hour)
        mk = self.ctx.marker_of_package(pk) if pk is not None else None
        seq = a.find(self.ctx.npc_preview_sequence(npc, self.hour)) or a.find("ACT_IDLE")
        t = time.perf_counter() - self.anim_t0 + (r.id % 7) * 0.37
        pos, quat = a.pose(seq, t)
        p, n = animmod.skin(sm.pos, sm.nrm, sm.bw, sm.bb, a.skin_matrices(a.matrices(pos, quat)))
        sm.update(p, n)
        if mk is not None:
            mat = model_matrix(mk.pos, [0, mk.angles[1], 0])
        else:
            mat = model_matrix(r.pos, [0, r.angles[1], 0])
        return sm, mat

    # ------------------------------------------------------------------ overlay
    def invalidate_overlay(self):
        self.overlay_dirty = True
        self._update_gizmo()
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

    def _star(self, out, c, r, col):
        for d in ((1, 0, 0), (0, 1, 0), (0, 0, 1), (0.7, 0.7, 0), (0.7, -0.7, 0)):
            out.append((c[0] - d[0] * r, c[1] - d[1] * r, c[2] - d[2] * r) + col)
            out.append((c[0] + d[0] * r, c[1] + d[1] * r, c[2] + d[2] * r) + col)

    def _build_overlay(self):
        lines, points, big = [], [], []
        L = self.layer
        proj = self.ctx.project
        if L is not None:
            for r in L.refs:
                sel = ("ref", r.id) in self.selset or (self.sel is not None and self.sel[0] == "pathpt" and self.sel[1] == r.id)
                if r.type == "marker":
                    col = _color(MARKER_COLORS.get(r.kind, "#8a96a6"), 1.0)
                    if sel:
                        col = SEL_COL
                    self._circle(lines, r.pos, 16, col, 20, 2)
                    self._circle(lines, r.pos, 10, col, 14, 2)
                    self._arrow(lines, r.pos, r.angles[1], 30, col)
                    lines.append((r.pos[0], r.pos[1], r.pos[2]) + col)
                    lines.append((r.pos[0], r.pos[1], r.pos[2] + 40) + col)
                elif r.type == "path":
                    col = (0.95, 0.45, 0.4, 1.0) if not sel else (1.0, 0.75, 0.3, 1.0)
                    for i, pt in enumerate(r.points):
                        a = pt.pos
                        big.append((a[0], a[1], a[2] + 6) + ((1.0, 0.85, 0.3, 1.0) if (sel and self.sel and self.sel[0] == "pathpt" and self.sel[2] == i) else col))
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
                    col = (0.4, 0.85, 1.0, 1.0) if not sel else SEL_COL
                    self._circle(lines, r.pos, 22, col, 20, 2)
                    self._arrow(lines, r.pos, r.angles[1], 40, col)
                elif r.type == "entity":
                    col = (0.8, 0.8, 0.85, 1.0) if not sel else SEL_COL
                    self._box(lines, (-8, -8, 0), (8, 8, 16), model_matrix(r.pos, r.angles), col)
                elif r.type == "zone":
                    col = (1.0, 0.85, 0.25, 0.9) if not sel else SEL_COL
                    w2, d2, h = r.size[0] / 2, r.size[1] / 2, r.size[2]
                    self._box(lines, (-w2, -d2, 0), (w2, d2, h), model_matrix(r.pos, [0, r.angles[1], 0]), col)
                elif r.type == "transition":
                    col = (0.35, 1.0, 0.75, 1.0) if not sel else SEL_COL
                    w2, d2, h = r.size[0] / 2, r.size[1] / 2, r.size[2]
                    self._box(lines, (-w2, -d2, 0), (w2, d2, h), model_matrix(r.pos, [0, 0, 0]), col)
                    arr, yaw = r.arrive()
                    self._circle(lines, arr, 16, col, 16, 2)
                    self._arrow(lines, arr, yaw, 40, col)
                    out_c = (col[0], col[1], col[2], 0.5)
                    lines.append((r.pos[0], r.pos[1], r.pos[2] + 2) + out_c)
                    lines.append((arr[0], arr[1], arr[2] + 2) + out_c)
                elif r.type == "sound":
                    col = (0.55, 0.75, 1.0, 1.0) if not sel else SEL_COL
                    m = model_matrix(r.pos, [0, 0, 0])
                    self._box(lines, (-6, -6, -6), (6, 6, 6), m, col)
                    for k in (12, 20, 28):
                        self._circle(lines, r.pos, k, (col[0], col[1], col[2], 0.9 - k / 40.0), 16, 2)
                    if sel and not r.everywhere:
                        self._circle(lines, r.pos, r.radius, (col[0], col[1], col[2], 0.45), 64, 2)
                elif r.type == "spawner":
                    col = (1.0, 0.42, 0.35, 1.0) if not sel else SEL_COL
                    try:
                        rad = float(r.keyvalues.get("radius", 384))
                    except ValueError:
                        rad = 384
                    self._circle(lines, r.pos, rad, (col[0], col[1], col[2], 0.7), 64, 3)
                    self._circle(lines, r.pos, 14, col, 12, 3)
                    self._box(lines, (-10, -10, 0), (10, 10, 28), model_matrix(r.pos, [0, 0, 0]), col)
                if sel and r.type in ("container", "prop", "item", "npc", "terminal", "entity"):
                    name = self._ref_model_name(r)
                    if name:
                        lo, hi = self.scene.model_bounds(name)
                        ang = [0, r.angles[1], 0] if r.type == "npc" else r.angles
                        self._box(lines, lo, hi, model_matrix(r.pos, ang), SEL_COL)
            # what the selected NPC does (packages): lines to their places at the chosen hour
            if self.opts["packages"] and proj is not None:
                for r in L.refs:
                    if r.type != "npc":
                        continue
                    npc = proj.npc(r.npc)
                    if npc is None or not npc.actor.packages:
                        continue
                    focus = ("ref", r.id) in self.selset or (self.focus_npc and self.focus_npc.lower() == r.npc.lower())
                    if not focus:
                        continue
                    for pkg in npc.actor.packages:
                        active = pkg is npc.actor.package_at(self.hour)
                        col = _color(PACKAGE_COLORS.get(pkg.type, "#ffffff"), 1.0 if active else 0.35)
                        for tgt in self._package_places(L, r, pkg):
                            lines.append((r.pos[0], r.pos[1], r.pos[2] + 50) + col)
                            lines.append((tgt[0], tgt[1], tgt[2] + 30) + col)
                        if pkg.type in ("wander", "sandbox"):
                            c = self._package_places(L, r, pkg)
                            c = c[0] if c else r.pos
                            self._circle(lines, c, pkg.radius or (512 if pkg.type == "wander" else 768), col, 48, 4)
            if self.opts["nav"] and L.nav:
                ncol = (1.0, 0.86, 0.25, 0.9) if self.mode == "nav" else (0.95, 0.8, 0.2, 0.55)
                for i, pnt in enumerate(L.nav):
                    if ("nav", i) in self.selset:
                        big.append((pnt[0], pnt[1], pnt[2] + 2) + (1.0, 0.4, 0.2, 1.0))
                    else:
                        points.append((pnt[0], pnt[1], pnt[2] + 2) + ncol)
        if self.scene is not None:
            for ob in self.scene.objects:
                if not self._obj_visible(ob):
                    continue
                sel = ("obj", ob.key) in self.selset
                o = ob.origin
                name = ob.draw_model()
                if name:
                    if sel:
                        lo, hi = self.scene.model_bounds(name)
                        self._box(lines, lo, hi, ob.matrix, SEL_COL)
                    if ob.is_npc():
                        col = SEL_COL if sel else (0.62, 0.7, 0.8, 1.0)
                        self._circle(lines, o, 22, col, 20, 2)
                    continue
                cls = ob.classname
                if cls == "info_player_start":
                    col = SEL_COL if sel else (0.4, 1.0, 0.45, 1.0)
                    self._circle(lines, o, 18, col, 16, 2)
                    self._arrow(lines, o, ob.angles[1], 36, col)
                elif cls in LIGHT_CLASSES:
                    col = SEL_COL if sel else _light_color(ob)
                    self._star(lines, [o[0], o[1], o[2]], 10, col)
                    if cls in ("light_spot", "light_deferred_spot", "env_projectedtexture"):
                        ang = list(ob.angles)
                        if cls == "light_spot":
                            p = ob.get("pitch", "")
                            if p not in ("", None):
                                try:
                                    ang[0] = -float(p)
                                except ValueError:
                                    pass
                        f = angle_matrix(*ang)[:, 0]
                        lines.append(tuple(o) + col)
                        lines.append((o[0] + f[0] * 48, o[1] + f[1] * 48, o[2] + f[2] * 48) + col)
                    if sel:
                        try:
                            rad = float(ob.get("radius", "0") or 0)
                        except ValueError:
                            rad = 0
                        if rad > 0:
                            self._circle(lines, o, rad, (col[0], col[1], col[2], 0.5), 64)
                else:
                    col = SEL_COL if sel else (0.78, 0.6, 0.95, 0.9) if ob.editable else (0.7, 0.7, 0.75, 0.8)
                    self._box(lines, (-6, -6, -6), (6, 6, 6), model_matrix(o, ob.angles), col)
        # the user's task markers for Claude (VMF geometry: rpm_claude_task, func_rpm_claude_area)
        for t in getattr(self.scene.bsp, "tasks", []) if self.scene is not None else []:
            done = t.get("status") == "done"
            col = (1.0, 0.62, 0.1, 0.45 if done else 1.0)
            self._box(lines, t["mins"], t["maxs"], np.eye(4, dtype=np.float32), col)
            if "origin" in t:
                self._star(lines, t["origin"], 20, col)
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

    def _draw_gizmo(self, vp):
        if self.gizmo.center is None or self.gizbuf is None:
            return
        self.gizmo.update_size(self.cam.pos, self.cam.fov)
        tris = self.gizmo.triangles(self.cam.pos)
        if not tris:
            return
        self.gizbuf.upload(np.array(tris, dtype=np.float32))
        lp = self.lprog
        lp.use()
        lp.mat4("uVP", vp)
        GL.glEnable(GL.GL_BLEND)
        GL.glBlendFunc(GL.GL_SRC_ALPHA, GL.GL_ONE_MINUS_SRC_ALPHA)
        GL.glDisable(GL.GL_DEPTH_TEST)
        self.gizbuf.draw(GL.GL_TRIANGLES)
        GL.glEnable(GL.GL_DEPTH_TEST)

    def _update_gizmo(self):
        """Gizmo at the main selection (editable things only)."""
        g = self.gizmo
        t = self.sel
        if t is None or self.mode != "select" or not self.t_editable(t) or t[0] == "nav":
            g.center = None
            return
        p = self.t_pos(t)
        if p is None:
            g.center = None
            return
        g.center = np.array(p, float)
        if t[0] in ("pathpt",) or (t[0] == "ref" and self._ref(t) is not None and self._ref(t).type == "path"):
            g.mode = "move" if g.mode == "rotate" else g.mode
        yaw_only = all(self.t_yaw_only(x) for x in self.selset if x[0] in ("ref", "obj"))
        g.rot_axes = (2,) if yaw_only else (0, 1, 2)

    def set_gizmo_mode(self, mode):
        self.gizmo.mode = mode
        self.gizmo.hot = None
        self.gizmoModeChanged.emit(mode)
        self.update()

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
                elif r.type == "zone":
                    loc = r.display_name.get(self.ctx.display_lang) or r.display_name.get(proj.base_language) or ""
                    sub = "зона %d×%d" % (r.size[0], r.size[1])
                    if loc:
                        sub = "локация «%s»" % loc + (" · музыка" if r.music else "") + (" · " + r.soundscape if r.soundscape else "")
                    out.append(([r.pos[0], r.pos[1], r.pos[2] + r.size[2] + 10], r.name or "зона", sub, "#ffd84a"))
                elif r.type == "sound":
                    out.append(([r.pos[0], r.pos[1], r.pos[2] + 34], r.label(), "звук · %d ед." % r.radius if not r.everywhere else "звук · везде",
                                "#8fbfff"))
                elif r.type == "transition":
                    ok = bool(r.target_map and r.target)
                    out.append(([r.pos[0], r.pos[1], r.pos[2] + r.size[2] + 12], "→ " + (r.target_map or "?"),
                                (r.target if ok else "не связан!"), "#5affbf" if ok else "#ff8a7a"))
                elif r.type == "spawner":
                    n = len([x for x in r.keyvalues.get("npcs", "").split(";") if x.strip()])
                    out.append(([r.pos[0], r.pos[1], r.pos[2] + 40], r.name or "точка появления",
                                "появление · %s–%s NPC%s" % (r.keyvalues.get("count_min", "1"), r.keyvalues.get("count_max", "3"),
                                                            "" if n else " · список пуст!"), "#ff8a7a"))
                elif r.type in ("container", "item", "prop", "entity", "terminal"):
                    sub = {"container": "контейнер", "item": "предмет", "prop": "проп", "entity": "сущность", "terminal": "терминал"}[r.type]
                    out.append(([r.pos[0], r.pos[1], r.pos[2] + 40], r.label(), sub, "#d8dde4"))
        if self.scene is not None:
            for ob in self.scene.objects:
                if not self._obj_visible(ob):
                    continue
                sel = ("obj", ob.key) in self.selset
                if ob.is_npc():
                    tn = ob.get("targetname")
                    out.append(([ob.origin[0], ob.origin[1], ob.origin[2] + 82], tn or ob.classname,
                                "Hammer · " + ob.classname if tn else "Hammer", "#a9b2bd"))
                elif ob.classname in LIGHT_CLASSES and self.opts["hammer"]:
                    tn = ob.get("targetname")
                    out.append(([ob.origin[0], ob.origin[1], ob.origin[2] + 16], tn or "свет", ob.classname, "#ffe9a8"))
                elif sel:
                    out.append(([ob.origin[0], ob.origin[1], ob.origin[2] + 24], ob.label(), ob.classname, "#e6c2ff"))
            status = {"todo": "сделать", "doing": "в работе", "question": "вопрос", "done": "готово"}
            for t in getattr(self.scene.bsp, "tasks", []):
                c = [(t["mins"][i] + t["maxs"][i]) / 2 for i in range(3)]
                c[2] = t["maxs"][2] + 12
                text = t.get("task") or ""
                out.append((c, "AI: " + (t.get("name") or "задача"),
                             "%s · %s" % (status.get(t.get("status"), t.get("status")), text[:60] + ("…" if len(text) > 60 else "")),
                             "#ffaa33"))
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
        if self.rubber is not None:
            r = QRect(self.rubber[0], self.rubber[1]).normalized()
            qp.setPen(QPen(QColor(255, 200, 90, 230), 1, Qt.DashLine))
            qp.setBrush(QColor(255, 200, 90, 40))
            qp.drawRect(r)
            qp.setBrush(Qt.NoBrush)
        # mode / hints
        qp.setFont(QFont("Segoe UI", 9))
        if self.mouselook:
            hint = "Режим полёта: мышь — обзор · WASD, Q/E, Shift · колесо — скорость · Z или Esc — выйти"
        else:
            hint = self.mode_text or ("Z или ПКМ + WASD — полёт · ЛКМ — выделить (Ctrl — добавить), тащить — двигать, рамка — по пустому · "
                                      "ПКМ по выделенному — перемещение/поворот · End — приземлить")
        qp.setPen(QColor(0, 0, 0, 150))
        qp.drawText(11, self.height() - 9, hint)
        qp.setPen(QColor("#ffd27a") if (self.mode != "select" or self.mouselook) else QColor("#eef2f6"))
        qp.drawText(10, self.height() - 10, hint)
        qp.setPen(QColor("#eef2f6"))
        nsel = len(self.selset)
        info = "%s · x %.0f y %.0f z %.0f · %.0f мс%s" % (self.scene.name if self.scene else "", self.cam.pos[0], self.cam.pos[1],
                                                           self.cam.pos[2], self.frame_ms, (" · выделено %d" % nsel) if nsel > 1 else "")
        qp.drawText(10, 18, info)
        if self.mouselook:
            c = self.rect().center()
            qp.setPen(QPen(QColor(255, 255, 255, 180), 1))
            qp.drawLine(c.x() - 6, c.y(), c.x() + 6, c.y())
            qp.drawLine(c.x(), c.y() - 6, c.x(), c.y() + 6)
        qp.end()

    # ------------------------------------------------------------------ modes
    def set_mode(self, mode, text="", cb=None):
        self.mode = mode
        self.mode_text = text
        self.mode_cb = cb
        if not self.mouselook:
            self.setCursor(Qt.CrossCursor if mode != "select" else Qt.ArrowCursor)
        self.overlay_dirty = True
        self._update_gizmo()
        self.update()
        self.status.emit(text)

    def cancel_mode(self):
        if self.mode == "path" and self.mode_cb:
            self.mode_cb(None)
        self.set_mode("select")

    def select(self, target, emit=True, add=False, toggle=False):
        if target is None:
            if not add and not toggle:
                self.selset = []
                self.sel = None
        elif toggle:
            if target in self.selset:
                self.selset.remove(target)
                self.sel = self.selset[-1] if self.selset else None
            else:
                self.selset.append(target)
                self.sel = target
        elif add:
            if target not in self.selset:
                self.selset.append(target)
            self.sel = target
        else:
            self.selset = [target]
            self.sel = target
        self.overlay_dirty = True
        self._update_gizmo()
        self.update()
        if emit:
            self.selected.emit(self.sel)

    def select_many(self, targets, add=False):
        if not add:
            self.selset = []
        for t in targets:
            if t not in self.selset:
                self.selset.append(t)
        self.sel = self.selset[-1] if self.selset else None
        self.overlay_dirty = True
        self._update_gizmo()
        self.update()
        self.selected.emit(self.sel if len(self.selset) <= 1 else ("multi", len(self.selset)))

    def _prune_selection(self):
        keep = [t for t in self.selset if self.t_pos(t) is not None]
        if keep != self.selset:
            self.selset = keep
            self.sel = keep[-1] if keep else None
            self._update_gizmo()

    def focus_on(self, pos, dist=360):
        pos = np.asarray(pos, float)
        f = self.cam.forward()
        self.cam.pos = pos - f * dist + [0, 0, 40]
        self.cam.look_at(pos)
        self.update()

    def focus_selected(self):
        p = self.t_pos(self.sel)
        if p is not None:
            self.focus_on([p[0], p[1], p[2] + 40])

    # ------------------------------------------------------------------ edits of the selection
    def editable_selection(self):
        return [t for t in self.selset if self.t_editable(t)]

    def rotate_selected(self, delta):
        sel = [t for t in self.editable_selection() if t[0] in ("ref", "obj")]
        if not sel:
            return
        for t in sel:
            a = self.t_angles(t)
            if a is None:
                continue
            a[1] = (a[1] + delta) % 360
            self.t_set(t, angles=a)
        self.overlay_dirty = True
        self._update_gizmo()
        self.update()
        self.edited.emit("rotate")

    def land_selected(self):
        """End: put the selection on what is below it (models: the lowest point touches)."""
        sel = [t for t in self.editable_selection() if t[0] in ("ref", "obj", "pathpt", "nav")]
        if not sel or self.scene is None:
            return
        skip = tuple(t if t[0] == "ref" else t[1] for t in sel if t[0] in ("ref", "obj"))
        extra = self.ref_models(skip=tuple(t for t in sel if t[0] == "ref"))
        n = 0
        for t in sel:
            pos = self.t_pos(t)
            if pos is None:
                continue
            name = self.t_model(t)
            ang = self.t_angles(t)
            is_point = t[0] in ("pathpt", "nav") or not name or (t[0] == "ref" and self._ref(t).type in ("npc", "marker")) or \
                (t[0] == "obj" and self._obj(t).is_npc())
            if is_point:
                np_ = self.scene.drop_to_floor(pos, skip=skip, extra=extra)
                if np_ != pos:
                    self.t_set(t, pos=np_)
                    n += 1
                continue
            r = self.scene.land(name, pos, ang or [0, 0, 0], skip=skip, extra=extra,
                                align=self.opts["align"] and not self.t_yaw_only(t))
            if r is not None:
                self.t_set(t, pos=r[0], angles=r[1] if self.opts["align"] and not self.t_yaw_only(t) else None)
                n += 1
        self.overlay_dirty = True
        self._update_gizmo()
        self.update()
        if n:
            self.edited.emit("land")
            self.status.emit("Приземлено объектов: %d" % n)
        else:
            self.status.emit("Под выделенным нет поверхности")

    # ------------------------------------------------------------------ input
    def _tick(self):
        now = time.perf_counter()
        dt = min(0.1, now - self._last_tick)
        self._last_tick = now
        move = np.zeros(3)
        flying = self.press_button == Qt.RightButton or self.mouselook
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

    def set_mouselook(self, on):
        if on == self.mouselook:
            return
        self.mouselook = on
        if on:
            self.setFocus()
            self.setCursor(Qt.BlankCursor)
            self._center_cursor()
            self._last_tick = time.perf_counter()
            self.timer.start()
        else:
            self.setCursor(Qt.CrossCursor if self.mode != "select" else Qt.ArrowCursor)
            self._warp = None
        self.update()

    def _center_cursor(self):
        c = self.rect().center()
        self._warp = c
        QCursor.setPos(self.mapToGlobal(c))

    @staticmethod
    def _phys_key(e):
        vk = e.nativeVirtualKey()
        if 0x41 <= vk <= 0x5A:
            return vk
        return e.key()

    def keyPressEvent(self, e):
        key = self._phys_key(e)
        if e.isAutoRepeat() and key in self.keys:
            return
        mods = e.modifiers()
        self.keys.add(key)
        if key in (Qt.Key_W, Qt.Key_A, Qt.Key_S, Qt.Key_D, Qt.Key_Q, Qt.Key_E, Qt.Key_Up, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right) \
                and not (mods & Qt.ControlModifier):
            if not self.timer.isActive():
                self._last_tick = time.perf_counter()
                self.timer.start()
            return
        if key == Qt.Key_Z and not (mods & (Qt.ControlModifier | Qt.AltModifier)):
            self.set_mouselook(not self.mouselook)
            return
        if key == Qt.Key_Escape:
            if self.mouselook:
                self.set_mouselook(False)
                return
            if self.gizmo.active is not None and self.drag:
                self._cancel_drag()
                return
            self.cancel_mode()
            return
        if key in (Qt.Key_Return, Qt.Key_Enter) and self.mode == "path":
            self.cancel_mode()
            return
        if key in (Qt.Key_BracketLeft, Qt.Key_BracketRight):
            self.rotate_selected((15 if key == Qt.Key_BracketLeft else -15) / (3 if mods & Qt.ShiftModifier else 1))
            return
        if key == Qt.Key_End:
            self.land_selected()
            return
        if key == Qt.Key_Space and self.gizmo.center is not None:
            self.set_gizmo_mode("rotate" if self.gizmo.mode == "move" else "move")
            return
        if key == Qt.Key_Delete:
            self.deleteRequested.emit()
            return
        if key == Qt.Key_F:
            self.focus_selected()
            return
        if key == Qt.Key_D and mods & Qt.ControlModifier:
            self.duplicateRequested.emit()
            return
        if key == Qt.Key_C and mods & Qt.ControlModifier:
            self.copyRequested.emit()
            return
        if key == Qt.Key_V and mods & Qt.ControlModifier:
            pt = self.mapFromGlobal(QCursor.pos())
            hit = self.cast(pt.x(), pt.y()) if self.rect().contains(pt) else None
            self.pasteRequested.emit([float(c) for c in hit[0]] if hit else None)
            return
        super().keyPressEvent(e)

    def keyReleaseEvent(self, e):
        if not e.isAutoRepeat():
            self.keys.discard(self._phys_key(e))
        super().keyReleaseEvent(e)

    def focusOutEvent(self, e):
        self.keys.clear()
        if self.mouselook:
            self.set_mouselook(False)
        super().focusOutEvent(e)

    def _cancel_drag(self):
        d = self.drag
        if d and d.get("orig"):
            for t, (p, a) in d["orig"].items():
                self.t_set(t, pos=p, angles=a)
        self.gizmo.end()
        self.drag = None
        self.overlay_dirty = True
        self._update_gizmo()
        self.update()

    def mousePressEvent(self, e):
        self.setFocus()
        self.press_pos = e.position()
        self.press_button = e.button()
        self.last_mouse = e.position().toPoint()
        self.drag = None
        if self.mouselook:
            if e.button() == Qt.RightButton:
                self.set_mouselook(False)
            return
        if e.button() == Qt.RightButton:
            self._last_tick = time.perf_counter()
            self.timer.start()
            return
        if e.button() != Qt.LeftButton or self.scene is None:
            return
        x, y = e.position().x(), e.position().y()
        mods = e.modifiers()
        if self.mode == "place":
            hit = self.cast(x, y)
            if hit is not None:
                self.placeRequested.emit([float(c) for c in hit[0]], [float(c) for c in hit[1]])
                if not (mods & Qt.ShiftModifier):
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
                self._begin_surface_drag(t, mods)
                return
            hit = self.cast(x, y)
            if hit is not None and hit[1][2] > 0.6 and self.layer is not None:
                self.layer.nav.append([float(hit[0][0]), float(hit[0][1]), float(hit[0][2]) + 1])
                self.select(("nav", len(self.layer.nav) - 1))
                self.edited.emit("nav")
            return
        # gizmo handle first
        h = self.gizmo.hit(self.project, x, y)
        if h is not None:
            sel = self.editable_selection()
            if self.gizmo.begin(h, self.ray(x, y)):
                self.drag = {"gizmo": True, "moved": False, "center": self.gizmo.center.copy(),
                             "orig": {t: (self.t_pos(t), self.t_angles(t)) for t in sel}}
            return
        t = self.pick(x, y)
        if t is None:
            # frame selection on empty space
            self.rubber = (e.position().toPoint(), e.position().toPoint())
            self._rubber_add = bool(mods & (Qt.ShiftModifier | Qt.ControlModifier))
            return
        if mods & Qt.ControlModifier:
            self.select(t, toggle=True)
            return
        if mods & Qt.ShiftModifier:
            self.select(t, add=True)
        elif t not in self.selset:
            self.select(t)
        else:
            self.sel = t
            self._update_gizmo()
            self.selected.emit(t)
        if self.t_editable(t) and t[0] in ("ref", "pathpt", "obj"):
            self._begin_surface_drag(t, mods)

    def _begin_surface_drag(self, t, mods):
        sel = [x for x in self.editable_selection()] if t in self.selset else [t]
        if t not in sel:
            sel.append(t)
        name = self.t_model(t)
        ang = self.t_angles(t) or [0, 0, 0]
        lift = 0.0
        point_like = t[0] in ("pathpt", "nav") or not name or (t[0] == "ref" and self._ref(t).type in ("npc", "marker")) or \
            (t[0] == "obj" and self._obj(t).is_npc())
        if not point_like:
            lift = self.scene.lowest_offset(name, ang)
        self.drag = {"target": t, "moved": False, "rotate": bool(mods & Qt.AltModifier), "lift": lift,
                     "orig": {x: (self.t_pos(x), self.t_angles(x)) for x in sel},
                     "skip": tuple(x if x[0] == "ref" else x[1] for x in sel if x[0] in ("ref", "obj"))}

    def mouseMoveEvent(self, e):
        pos = e.position().toPoint()
        if self.mouselook:
            if self._warp is None:
                self._center_cursor()
                return
            dx = pos.x() - self._warp.x()
            dy = pos.y() - self._warp.y()
            if dx or dy:
                self.cam.yaw -= dx * 0.18
                self.cam.pitch = max(-89.0, min(89.0, self.cam.pitch - dy * 0.18))
                self._center_cursor()
                self.update()
            return
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
        left_held = self.press_button == Qt.LeftButton
        if self.rubber is not None and left_held:
            self.rubber = (self.rubber[0], pos)
            self.update()
            return
        if self.drag and left_held:
            if self.drag.get("gizmo"):
                self._gizmo_drag(e)
                return
            if not self.drag["moved"] and (e.position() - self.press_pos).manhattanLength() < 4:
                return
            self.drag["moved"] = True
            self._surface_drag(e)
            return
        # hover over the gizmo
        if not e.buttons() and self.gizmo.center is not None:
            h = self.gizmo.hit(self.project, e.position().x(), e.position().y())
            if h != self.gizmo.hot:
                self.gizmo.hot = h
                self.update()

    def _surface_drag(self, e):
        d = self.drag
        t = d["target"]
        hit = self.cast(e.position().x(), e.position().y(), skip=d["skip"])
        if hit is None:
            return
        p = [float(c) for c in hit[0]]
        p0, a0 = d["orig"][t]
        if d.get("rotate"):
            if a0 is not None:
                yaw = math.degrees(math.atan2(p[1] - p0[1], p[0] - p0[0])) % 360
                if e.modifiers() & Qt.ControlModifier:
                    yaw = round(yaw / 15) * 15 % 360
                for x, (px, ax) in d["orig"].items():
                    if ax is not None:
                        na = list(ax)
                        na[1] = (ax[1] + yaw - a0[1]) % 360
                        self.t_set(x, angles=na)
        else:
            if e.modifiers() & Qt.ControlModifier:
                p[0] = round(p[0] / 16) * 16
                p[1] = round(p[1] / 16) * 16
            if t[0] == "nav":
                p[2] += 1
            p[2] += d["lift"]
            off = np.array(p) - np.array(p0)
            for x, (px, ax) in d["orig"].items():
                if px is None:
                    continue
                if x == t:
                    self.t_set(x, pos=p)
                else:
                    self.t_set(x, pos=list(np.array(px) + off))
        self.overlay_dirty = True
        self._update_gizmo()
        self.update()

    def _gizmo_drag(self, e):
        d = self.drag
        r = self.gizmo.drag(self.ray(e.position().x(), e.position().y()))
        if r is None:
            return
        d["moved"] = True
        snap = bool(e.modifiers() & Qt.ControlModifier)
        c = d["center"]
        if r[0] == "move":
            off = np.array(r[1], float)
            if snap:
                off = np.round(off / 16.0) * 16.0
            for t, (p, a) in d["orig"].items():
                if p is not None:
                    self.t_set(t, pos=list(np.array(p) + off))
            self.gizmo.center = c + off
            self.status.emit("Сдвиг: %.0f %.0f %.0f" % tuple(off))
        else:
            _, ax, deg = r
            if snap:
                deg = round(deg / 15.0) * 15.0
            R = axis_rotation(ax, deg)
            for t, (p, a) in d["orig"].items():
                if p is None:
                    continue
                np_ = c + R @ (np.array(p) - c) if len(d["orig"]) > 1 else None
                if a is None:
                    if np_ is not None:
                        self.t_set(t, pos=list(np_))
                    continue
                if self.t_yaw_only(t) or ax == 2 and abs(a[0]) < 1e-6 and abs(a[2]) < 1e-6:
                    na = [a[0], (a[1] + deg) % 360, a[2]] if ax == 2 else a
                else:
                    na = matrix_angles(R @ angle_matrix(*a).astype(np.float64))
                self.t_set(t, pos=list(np_) if np_ is not None else None, angles=na)
            self.status.emit("Поворот: %.0f°" % deg)
        self.overlay_dirty = True
        self.update()

    def mouseReleaseEvent(self, e):
        if self.mouselook:
            return
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
                # a short right click on the selection (or its gizmo) switches move <-> rotate
                if self.mode == "select" and self.gizmo.center is not None:
                    t = self.gizmo.hit(self.project, x, y) or self.pick(x, y)
                    if t is not None and (not isinstance(t, tuple) or t in self.selset or t[0] == "r"):
                        self.set_gizmo_mode("rotate" if self.gizmo.mode == "move" else "move")
                        return
                t = self.pick(x, y)
                hit = self.cast(x, y)
                if t is not None and t not in self.selset:
                    self.select(t)
                self.contextRequested.emit(t, e.globalPosition().toPoint(), [float(c) for c in hit[0]] if hit else None)
            return
        if e.button() == Qt.LeftButton:
            if self.rubber is not None:
                a, b = self.rubber[0], e.position().toPoint()
                self.rubber = None
                rect = QRect(a, b).normalized()
                if rect.width() > 4 or rect.height() > 4:
                    self.select_many(self._targets_in_rect(rect), add=self._rubber_add)
                elif not self._rubber_add:
                    self.select(None)
                self.update()
            elif self.drag and self.drag.get("gizmo") and self.press_pos is not None:
                self._gizmo_drag(e)          # the release position counts (no move event before it)
                self.gizmo.end()
                if self.drag["moved"]:
                    self.edited.emit("move")
            elif self.drag and self.drag["moved"]:
                self.gizmo.end()
                self.edited.emit("move")
            else:
                self.gizmo.end()
        self.drag = None
        self.press_button = None
        self._update_gizmo()
        self.update()

    def _targets_in_rect(self, rect):
        out = []

        def inside(p):
            pr = self.project(p)
            return pr is not None and rect.contains(pr[0].toPoint())
        if self.layer is not None:
            for r in self.layer.refs:
                if r.type == "path":
                    if r.points and inside(r.points[0].pos):
                        out.append(("ref", r.id))
                elif inside(self._ref_center(r)):
                    out.append(("ref", r.id))
            if self.mode == "nav":
                for i, p in enumerate(self.layer.nav):
                    if inside(p):
                        out.append(("nav", i))
        if self.scene is not None:
            for ob in self.scene.objects:
                if self._obj_visible(ob) and inside(ob.origin):
                    out.append(("obj", ob.key))
        return out

    # --- drag and drop from the object window
    def dragEnterEvent(self, e):
        if e.mimeData().hasFormat("application/x-hl2rpm-place") and self.scene is not None:
            e.acceptProposedAction()

    def dragMoveEvent(self, e):
        if e.mimeData().hasFormat("application/x-hl2rpm-place"):
            e.acceptProposedAction()

    def dropEvent(self, e):
        import json
        try:
            payload = json.loads(bytes(e.mimeData().data("application/x-hl2rpm-place")).decode("utf-8"))
        except ValueError:
            return
        p = e.position()
        hit = self.cast(p.x(), p.y())
        if hit is None:
            return
        e.acceptProposedAction()
        self.setFocus()
        self.dropped.emit(payload, [float(c) for c in hit[0]], [float(c) for c in hit[1]])

    def mouseDoubleClickEvent(self, e):
        if e.button() == Qt.LeftButton and self.sel is not None:
            self.activated.emit(self.sel)

    def wheelEvent(self, e):
        steps = e.angleDelta().y() / 120.0
        if self.press_button == Qt.RightButton or self.mouselook:
            self.speed = max(60.0, min(6000.0, self.speed * (1.25 ** steps)))
            self.status.emit("Скорость полёта: %.0f" % self.speed)
            return
        self.cam.pos += self.cam.forward() * steps * max(64.0, self.speed * 0.35)
        self.update()

    def camera_state(self):
        return [float(self.cam.pos[0]), float(self.cam.pos[1]), float(self.cam.pos[2]), float(self.cam.pitch), float(self.cam.yaw)]


def _light_color(ob):
    for k in ("_light", "diffuse", "_lightcolor"):
        v = ob.get(k)
        if v:
            try:
                c = [float(x) for x in v.split()[:3]]
                m = max(1.0, max(c))
                return (c[0] / m, c[1] / m, c[2] / m, 1.0)
            except ValueError:
                pass
    return (1.0, 0.92, 0.6, 1.0)
