"""The world part of the main window: the map panel (render window + its toolbar),
the object window (what can be placed), the cell view (what is on the map),
factions, placing / moving / deleting, AI nodes, running the game at the camera."""
import glob
import math
import os
import subprocess
import time

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (QApplication, QCheckBox, QComboBox, QDialog, QDialogButtonBox, QHBoxLayout, QInputDialog,
                               QLabel, QLineEdit, QListWidget, QListWidgetItem, QMenu, QMessageBox, QProgressDialog,
                               QPushButton, QSlider, QToolButton, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget)

from .. import world_import
from .. import worlddata as wd
from ..model import NPC, Node, TARGETNAME_RE
from ..world import mdl as mdlmod
from ..world.gamefs import GameFS
from ..world.materials import MaterialSystem
from ..world.scene import MapScene, is_npc_class
from .worldview import WorldView

HUMAN_MODELS = (["models/humans/group01/male_%02d.mdl" % i for i in range(1, 10)] +
                ["models/humans/group01/female_%02d.mdl" % i for i in (1, 2, 3, 4, 6, 7)] +
                ["models/humans/group02/male_%02d.mdl" % i for i in range(1, 10)] +
                ["models/humans/group02/female_%02d.mdl" % i for i in (1, 2, 3, 4, 6, 7)] +
                ["models/humans/group03/male_%02d.mdl" % i for i in range(1, 10)] +
                ["models/humans/group03/female_%02d.mdl" % i for i in (1, 2, 3, 4, 6, 7)] +
                ["models/humans/group03m/male_%02d.mdl" % i for i in range(1, 10)])


class ModelBrowser(QDialog):
    def __init__(self, parent, names, current):
        super().__init__(parent)
        self.setWindowTitle("Модель из игры")
        self.resize(620, 640)
        lay = QVBoxLayout(self)
        self.flt = QLineEdit()
        self.flt.setPlaceholderText("поиск: часть пути, например furniture или humans/group02")
        lay.addWidget(self.flt)
        self.lst = QListWidget()
        lay.addWidget(self.lst, 1)
        self.names = names
        self.flt.textChanged.connect(self.fill)
        self.lst.itemDoubleClicked.connect(lambda _: self.accept())
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)
        lay.addWidget(bb)
        self.flt.setText(os.path.dirname(current).replace("models/", "") if current else "")
        self.fill()

    def fill(self):
        t = self.flt.text().strip().lower()
        self.lst.clear()
        n = 0
        for m in self.names:
            if t and t not in m:
                continue
            self.lst.addItem(m)
            n += 1
            if n >= 3000:
                break

    def value(self):
        it = self.lst.currentItem()
        return it.text() if it else ""


class MapPanel(QWidget):
    def __init__(self, ctx):
        super().__init__()
        self.ctx = ctx
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)
        bar = QWidget()
        bar.setObjectName("mapbar")
        bl = QHBoxLayout(bar)
        bl.setContentsMargins(6, 4, 6, 4)
        bl.addWidget(QLabel("Карта:"))
        self.maps = QComboBox()
        self.maps.setMinimumContentsLength(24)
        self.maps.setSizeAdjustPolicy(QComboBox.AdjustToMinimumContentsLengthWithIcon)
        self.maps.setMaxVisibleItems(30)
        bl.addWidget(self.maps)
        self.reload = QToolButton(text="⟳")
        self.reload.setToolTip("Перечитать карту (после компиляции в Hammer)")
        bl.addWidget(self.reload)
        bl.addSpacing(12)
        bl.addWidget(QLabel("Время:"))
        self.hour = QSlider(Qt.Horizontal)
        self.hour.setRange(0, 24 * 4 - 1)
        self.hour.setValue(12 * 4)
        self.hour.setFixedWidth(170)
        self.hour.setToolTip("Время суток для просмотра распорядка: подпись над NPC и линии к месту текущего пакета")
        bl.addWidget(self.hour)
        self.hour_lbl = QLabel("12:00")
        self.hour_lbl.setMinimumWidth(40)
        bl.addWidget(self.hour_lbl)
        bl.addSpacing(12)
        self.toggles = {}
        for key, text, tip in (("labels", "Подписи", "Имена над объектами"), ("hammer", "Hammer", "NPC и старт игрока из самой карты"),
                               ("nav", "Навигация", "Узлы, по которым ходят NPC"), ("packages", "Распорядок", "Линии к местам пакетов выбранного NPC"),
                               ("props", "Пропы", "Статические пропы карты")):
            cb = QCheckBox(text)
            cb.setToolTip(tip)
            cb.setChecked(True)
            bl.addWidget(cb)
            self.toggles[key] = cb
        bl.addSpacing(12)
        self.nav_btn = QToolButton(text="Навигация ▾")
        self.nav_btn.setPopupMode(QToolButton.InstantPopup)
        bl.addWidget(self.nav_btn)
        self.play = QPushButton("▶ Игра отсюда")
        self.play.setToolTip("Сохранить и запустить карту: игрок появится там, где камера (F6)")
        bl.addWidget(self.play)
        bl.addStretch(1)
        lay.addWidget(bar)
        self.view = WorldView(ctx)
        lay.addWidget(self.view, 1)


class WorldMixin:
    # ------------------------------------------------------------------ build
    def _build_world_ui(self):
        self.fs = None
        self.matsys = None
        self.scene = None
        self.layer = None
        self.cur_map = ""
        self._seq_cache = {}
        self._model_names = None
        self.map_panel = MapPanel(self)
        self.worldview = self.map_panel.view
        mp = self.map_panel
        mp.maps.activated.connect(lambda _: self.open_map(mp.maps.currentData()))
        mp.reload.clicked.connect(lambda: self.open_map(self.cur_map, force=True))
        mp.hour.valueChanged.connect(self._hour_changed)
        for key, cb in mp.toggles.items():
            cb.toggled.connect(lambda v, key=key: (self.worldview.opts.__setitem__(key, bool(v)), self.worldview.invalidate_overlay()))
        m = QMenu(mp)
        m.addAction("Сгенерировать в игре…", self.nav_generate)
        m.addAction("Править узлы (клик — добавить, Delete — удалить)", self.nav_edit_mode)
        m.addAction("Удалить все узлы этой карты", self.nav_clear)
        mp.nav_btn.setMenu(m)
        mp.play.clicked.connect(self.run_game_here)
        wv = self.worldview
        wv.selected.connect(self.on_world_selected)
        wv.edited.connect(self.on_world_edited)
        wv.placeRequested.connect(self.on_place)
        wv.contextRequested.connect(self.on_world_context)
        wv.activated.connect(self.on_world_activated)
        wv.deleteRequested.connect(self.delete_world_selected)
        wv.duplicateRequested.connect(self.duplicate_world_selected)
        wv.status.connect(lambda s: self.statusBar().showMessage(s, 5000))
        self.place_payload = None
        self.cell_timer = QTimer(self)
        self.cell_timer.setSingleShot(True)
        self.cell_timer.setInterval(250)
        self.cell_timer.timeout.connect(self.refresh_cell_view)

    def _build_palette(self):
        w = QWidget()
        l = QVBoxLayout(w)
        l.setContentsMargins(4, 4, 4, 4)
        self.palette = QTreeWidget()
        self.palette.setHeaderHidden(True)
        self.palette.itemDoubleClicked.connect(lambda it, _: self._palette_place(it))
        l.addWidget(self.palette, 1)
        b = QPushButton("Поставить на карту")
        b.setToolTip("Или двойной клик по объекту, затем клик в окне карты (Shift — поставить несколько)")
        b.clicked.connect(lambda: self._palette_place(self.palette.currentItem()))
        l.addWidget(b)
        return w

    def _build_cell_view(self):
        w = QWidget()
        l = QVBoxLayout(w)
        l.setContentsMargins(4, 4, 4, 4)
        self.cell_filter = QLineEdit()
        self.cell_filter.setPlaceholderText("фильтр…")
        self.cell_filter.textChanged.connect(lambda _: self.refresh_cell_view())
        l.addWidget(self.cell_filter)
        self.cell = QTreeWidget()
        self.cell.setHeaderLabels(["Объект", "Где"])
        self.cell.setColumnWidth(0, 190)
        self.cell.itemClicked.connect(self._cell_clicked)
        self.cell.itemDoubleClicked.connect(self._cell_double)
        l.addWidget(self.cell, 1)
        return w

    def _build_factions(self):
        w = QWidget()
        l = QVBoxLayout(w)
        l.setContentsMargins(4, 4, 4, 4)
        self.faction_list = QListWidget()
        self.faction_list.itemClicked.connect(lambda it: self.inspector.show_target(("faction", it.data(Qt.UserRole))))
        l.addWidget(self.faction_list, 1)
        b = QPushButton("+ фракция")
        b.clicked.connect(lambda: self.new_faction())
        l.addWidget(b)
        return w

    # ------------------------------------------------------------------ project / maps
    def init_world(self):
        p = self.project
        s = p.settings
        sdk = os.path.dirname(s.hl2_exe) if s.hl2_exe else ""
        extra = [s.game_dir] if s.game_dir and os.path.normcase(os.path.abspath(s.game_dir)) != os.path.normcase(os.path.abspath(s.mod_dir)) else []
        try:
            self.fs = GameFS(s.mod_dir, sdk, extra)
            self.matsys = MaterialSystem(self.fs)
        except Exception as e:
            self.fs = None
            self.logln("Файлы игры не прочитаны: %s" % e)
        self._seq_cache = {}
        self._model_names = None
        self.refresh_map_list()
        self.refresh_palette()
        self.refresh_faction_list()
        last = p.view.get("_last_map", "")
        files = self.map_files()
        if last not in files:
            last = next((m for m in sorted(p.maps) if m in files and p.maps[m].refs), "")
        self.scene = None
        self.layer = None
        self.cur_map = ""
        self.worldview.set_map(None, None)
        if last:
            QTimer.singleShot(50, lambda: self.open_map(last))

    def map_files(self):
        s = self.project.settings if self.project else None
        if s is None:
            return {}
        # the game's order: the mod's maps shadow testmaps (a fresh compile into hl2rpm\maps wins)
        dirs = [os.path.join(s.mod_dir, "maps"), os.path.join(s.mod_dir, "testmaps", "maps"), os.path.join(s.game_dir, "maps")]
        out = {}
        for d in dirs:
            for f in sorted(glob.glob(os.path.join(d, "*.bsp"))):
                out.setdefault(os.path.splitext(os.path.basename(f))[0], f)
        return out

    def refresh_map_list(self):
        mp = self.map_panel
        files = self.map_files()
        mp.maps.blockSignals(True)
        mp.maps.clear()
        mp.maps.addItem("— выберите карту —", "")
        for name in sorted(files, key=lambda n: (not (self.project and self.project.layer(n) and self.project.layer(n).refs), n.lower())):
            layer = self.project.layer(name) if self.project else None
            n = len(layer.refs) if layer else 0
            mp.maps.addItem("%s%s" % (name, ("  · объектов %d" % n) if n else ""), name)
        mp.maps.setCurrentIndex(max(0, mp.maps.findData(self.cur_map)))
        mp.maps.blockSignals(False)

    def current_map_name(self):
        return self.cur_map

    def open_map(self, name, force=False):
        if not name or not self.project or getattr(self, "_loading_map", False):
            return
        if name == self.cur_map and self.scene is not None and not force:
            self.central_tabs.setCurrentWidget(self.map_panel)
            return
        files = self.map_files()
        path = files.get(name)
        if not path:
            QMessageBox.warning(self, "Карта", "Не найден %s.bsp в папках maps игры и мода." % name)
            return
        if self.fs is None:
            QMessageBox.warning(self, "Карта", "Не удалось прочитать файлы игры (Проект → Настройки: папка мода и hl2.exe).")
            return
        if self.layer is not None and self.cur_map:
            self.layer.camera = self.worldview.camera_state()
        prog = QProgressDialog("Загрузка карты %s…" % name, None, 0, 100, self)
        prog.setWindowTitle("Карта")
        prog.setWindowModality(Qt.WindowModal)
        prog.setMinimumDuration(200)

        def progress(f, msg):
            prog.setValue(int(f * 100))
            prog.setLabelText(msg)
            QApplication.processEvents()
        t0 = time.time()
        # the progress dialog runs the event loop: no second load from a click or a timer meanwhile
        self._loading_map = True
        try:
            scene = MapScene(path, self.fs, self.matsys, progress)
        except Exception as e:
            prog.close()
            QMessageBox.critical(self, "Карта", "Не удалось открыть %s:\n%s" % (path, e))
            return
        finally:
            self._loading_map = False
        prog.close()
        self.scene = scene
        self.cur_map = name
        self.layer = self.project.layer(name, create=True)
        self.project.view["_last_map"] = name
        self.worldview.set_map(scene, self.layer, self.matsys)
        self.map_panel.maps.setCurrentIndex(max(0, self.map_panel.maps.findData(name)))
        self.refresh_cell_view()
        self.logln("Карта %s: %d треугольников, %d пропов, объектов проекта %d, узлов навигации %d (%.1f с)" % (
            name, scene.ray.n, len(scene.props), len(self.layer.refs), len(self.layer.nav), time.time() - t0))
        if self.central_tabs.currentWidget() is not self.map_panel:
            self.central_tabs.setCurrentWidget(self.map_panel)

    # ------------------------------------------------------------------ lists
    def refresh_palette(self):
        pt = self.palette
        pt.clear()
        if not self.project:
            return

        def cat(title):
            it = QTreeWidgetItem([title])
            f = it.font(0)
            f.setBold(True)
            it.setFont(0, f)
            pt.addTopLevelItem(it)
            return it

        def add(parent, text, data, tip=""):
            it = QTreeWidgetItem([text])
            it.setData(0, Qt.UserRole, data)
            if tip:
                it.setToolTip(0, tip)
            parent.addChild(it)
            return it
        c = cat("Персонажи (NPC проекта)")
        for n in sorted(self.project.npcs, key=lambda x: x.targetname.lower()):
            disp = n.display_name.get(self.display_lang) or n.display_name.get(self.project.base_language, "")
            add(c, "%s%s" % (n.targetname, ("  — " + disp) if disp else ""), ("npc", n.targetname),
                wd.NPC_CLASS_NAMES.get(n.actor.classname, n.actor.classname))
        add(c, "＋ новый персонаж…", ("new_npc",))
        c = cat("Маркеры (кровать, стул, стена…)")
        for k, n, col in wd.MARKER_KINDS:
            it = add(c, n, ("marker", k))
            it.setForeground(0, QColor(col))
        c = cat("Предметы")
        for name in self.item_names():
            add(c, name, ("item", name))
        c = cat("Контейнеры (с лутом)")
        for m, n in wd.CONTAINER_MODELS:
            add(c, n, ("container", m), m)
        add(c, "другая модель…", ("container", None))
        c = cat("Пропы (мебель, декор)")
        for m, n in wd.PROP_MODELS:
            add(c, n, ("prop", m), m)
        add(c, "другая модель…", ("prop", None))
        c = cat("Прочее")
        add(c, "Маршрут патруля", ("path",), "Кликайте точки на карте, Enter или правая кнопка — готово")
        add(c, "Своя сущность (classname)…", ("entity",), "ambient_generic, light_deferred, info_target…")
        pt.expandItem(pt.topLevelItem(0))
        pt.expandItem(pt.topLevelItem(1))

    def refresh_faction_list(self):
        self.faction_list.clear()
        if not self.project:
            return
        for f in self.project.factions:
            name = f.name.get(self.display_lang) or f.name.get(self.project.base_language, "")
            it = QListWidgetItem("%s%s\n   к игроку: %s" % (f.id, ("  — " + name) if name else "", wd.RELATION_NAMES.get(f.player, "по классу")))
            it.setData(Qt.UserRole, f.id)
            self.faction_list.addItem(it)

    def refresh_cell_view(self):
        c = self.cell
        c.clear()
        if self.layer is None:
            return
        flt = self.cell_filter.text().strip().lower()
        groups = {}
        for t, n in wd.REF_TYPES:
            it = QTreeWidgetItem([n, ""])
            f = it.font(0)
            f.setBold(True)
            it.setFont(0, f)
            groups[t] = it
        for r in self.layer.refs:
            lbl = r.label()
            if r.type == "marker":
                lbl = "%s  (%s%s)" % (lbl, wd.MARKER_NAMES.get(r.kind, r.kind), (", " + r.owner) if r.owner else "")
            if flt and flt not in lbl.lower():
                continue
            it = QTreeWidgetItem([lbl, "%.0f %.0f %.0f" % tuple(r.pos) if r.type != "path" else "%d точек" % len(r.points)])
            it.setData(0, Qt.UserRole, ("ref", r.id))
            groups[r.type].addChild(it)
        for t, it in groups.items():
            if it.childCount():
                it.setText(0, "%s (%d)" % (it.text(0), it.childCount()))
                c.addTopLevelItem(it)
                it.setExpanded(True)
        if self.scene is not None:
            hi = QTreeWidgetItem(["NPC из Hammer", ""])
            f = hi.font(0)
            f.setBold(True)
            hi.setFont(0, f)
            for (i, cls, o, a, e) in self.scene.entities:
                if not is_npc_class(cls):
                    continue
                tn = e.get("targetname", "")
                lbl = "%s  (%s)" % (tn or "без имени", cls)
                if flt and flt not in lbl.lower():
                    continue
                it = QTreeWidgetItem([lbl, "%.0f %.0f %.0f" % tuple(o)])
                it.setData(0, Qt.UserRole, ("hammer", i))
                if tn and self.project.npc(tn):
                    it.setForeground(0, QColor("#7fd6ff"))
                hi.addChild(it)
            if hi.childCount():
                hi.setText(0, "NPC из Hammer (%d)" % hi.childCount())
                c.addTopLevelItem(hi)
        self.refresh_map_list()

    def _cell_clicked(self, it, col):
        t = it.data(0, Qt.UserRole)
        if t:
            self.worldview.select(t)

    def _cell_double(self, it, col):
        t = it.data(0, Qt.UserRole)
        if t:
            self.worldview.select(t)
            self.worldview.focus_selected()
            self.central_tabs.setCurrentWidget(self.map_panel)

    # ------------------------------------------------------------------ selection / edits
    def world_changed(self, what=""):
        self.worldview.invalidate_overlay()
        self.cell_timer.start()
        if what in ("faction",):
            self.refresh_faction_list()

    def on_world_selected(self, target):
        if target is None:
            return
        if target[0] == "ref" and self.layer is not None:
            r = self.layer.ref(target[1])
            self.worldview.focus_npc = r.npc if r is not None and r.type == "npc" else ""
        self.inspector.show_target(target)

    def on_world_edited(self, what):
        self.model_changed(False, moved=True)
        self.cell_timer.start()
        t = self.inspector.target
        if t and t[0] in ("ref", "pathpt", "nav"):
            self.inspector.show_target(t)

    def on_world_activated(self, target):
        if target and target[0] == "ref" and self.layer is not None:
            r = self.layer.ref(target[1])
            if r is not None and r.type == "npc" and self.project.npc(r.npc):
                self.open_npc(r.npc)

    def _sel_ref(self):
        t = self.worldview.sel
        if t is not None and t[0] in ("ref", "pathpt") and self.layer is not None:
            return self.layer.ref(t[1])
        return None

    def delete_world_selected(self):
        t = self.worldview.sel
        if t is None or self.layer is None:
            return
        if t[0] == "pathpt":
            r = self.layer.ref(t[1])
            if r is not None and t[2] < len(r.points):
                del r.points[t[2]]
                self.worldview.select(("ref", r.id))
                self.on_world_edited("delete")
            return
        if t[0] == "nav":
            self.delete_nav(t[1])
            return
        r = self._sel_ref()
        if r is not None:
            self.delete_ref(r)

    def duplicate_world_selected(self):
        r = self._sel_ref()
        if r is not None:
            self.duplicate_ref(r)

    def delete_ref(self, ref):
        if self.layer is None or ref not in self.layer.refs:
            return
        self.layer.refs.remove(ref)
        self.worldview.select(None, emit=False)
        self.inspector.show_target(("npc",) if self.npc else None)
        self.model_changed(False, moved=True)
        self.world_changed("delete")

    def duplicate_ref(self, ref):
        if self.layer is None:
            return
        r = ref.clone(self.layer.new_id())
        r.pos[0] += 48
        if r.name:
            r.name = self._unique_name(r.name)
        self.layer.refs.append(r)
        self.model_changed(False, moved=True)
        self.world_changed("dup")
        self.worldview.select(("ref", r.id))

    def delete_nav(self, i):
        if self.layer is not None and 0 <= i < len(self.layer.nav):
            del self.layer.nav[i]
            self.worldview.select(None, emit=False)
            self.model_changed(False, moved=True)
            self.world_changed("nav")

    def focus_ref(self, mapname, rid):
        if mapname != self.cur_map:
            self.open_map(mapname)
        if self.layer is None or self.cur_map != mapname:
            return
        self.central_tabs.setCurrentWidget(self.map_panel)
        self.worldview.select(("ref", rid))
        self.worldview.focus_selected()

    def _unique_name(self, base):
        used = set()
        for layer in self.project.maps.values():
            for r in layer.refs:
                if r.name:
                    used.add(r.name.lower())
        stem = base.rstrip("0123456789").rstrip("_") or base
        if base.lower() not in used:
            return base
        k = 2
        while ("%s_%d" % (stem, k)).lower() in used:
            k += 1
        return "%s_%d" % (stem, k)

    def rename_world_name(self, old, new):
        for n in self.project.npcs:
            for pk in n.actor.packages:
                if pk.ref == old:
                    pk.ref = new
                if pk.path == old:
                    pk.path = new
                if pk.target == old:
                    pk.target = new

    def ref_names_for(self, tn):
        maps = [m for m, _ in self.project.placements(tn)] or ([self.cur_map] if self.cur_map else [])
        names = set()
        for m in maps:
            layer = self.project.layer(m)
            if layer:
                names.update(r.name for r in layer.refs if r.name and r.type != "path")
        return sorted(names)

    def path_names_for(self, tn):
        maps = [m for m, _ in self.project.placements(tn)] or ([self.cur_map] if self.cur_map else [])
        names = set()
        for m in maps:
            layer = self.project.layer(m)
            if layer:
                names.update(layer.path_names())
        return sorted(names)

    # ------------------------------------------------------------------ placing
    def _palette_place(self, it):
        if it is None:
            return
        d = it.data(0, Qt.UserRole)
        if not d:
            return
        if d[0] == "new_npc":
            self.new_npc()
            return
        if d[0] in ("container", "prop") and d[1] is None:
            m = self.browse_model("", "models/")
            if not m:
                return
            d = (d[0], m)
        if d[0] == "entity":
            cls, ok = QInputDialog.getText(self, "Сущность", "classname:", text="ambient_generic")
            if not ok or not cls.strip():
                return
            d = ("entity", cls.strip())
        self.start_place(d)

    def start_place(self, payload):
        if self.scene is None:
            QMessageBox.information(self, "Карта", "Сначала откройте карту (список «Карта» над окном карты).")
            return
        self.central_tabs.setCurrentWidget(self.map_panel)
        self.place_payload = payload
        names = {"npc": "NPC «%s»" % payload[1] if len(payload) > 1 else "NPC", "marker": "маркер «%s»" % wd.MARKER_NAMES.get(payload[1], ""),
                 "item": "предмет %s" % (payload[1] if len(payload) > 1 else ""), "container": "контейнер", "prop": "проп",
                 "path": "маршрут", "entity": "сущность %s" % (payload[1] if len(payload) > 1 else "")}
        self.worldview.set_mode("place", "Клик — поставить %s · Shift+клик — поставить ещё · Esc — отмена" % names.get(payload[0], ""))
        self.worldview.setFocus()

    def start_place_npc(self, tn):
        self.start_place(("npc", tn))

    def on_place(self, pos, normal):
        d = self.place_payload
        if d is None or self.layer is None:
            return
        L = self.layer
        cam = self.worldview.cam.pos
        yaw = math.degrees(math.atan2(cam[1] - pos[1], cam[0] - pos[0])) % 360   # face the camera
        r = wd.Ref(L.new_id(), d[0], pos)
        r.angles = [0.0, round(yaw / 15) * 15 % 360, 0.0]
        if d[0] == "npc":
            r.npc = d[1]
        elif d[0] == "marker":
            r.kind = d[1]
            r.anim = wd.DEFAULT_MARKER_ANIM.get(d[1], "")
            r.name = self._unique_name("%s_%d" % (d[1], r.id))
            if d[1] in ("lean",):
                r.angles[1] = (math.degrees(math.atan2(normal[1], normal[0])) % 360) if abs(normal[2]) < 0.5 else r.angles[1]
        elif d[0] == "item":
            r.classname = d[1]
            r.pos[2] += 4
        elif d[0] in ("container", "prop"):
            r.model = d[1]
            m = self.scene.model(d[1])
            if m is not None and m.ok:
                r.pos[2] -= float(m.mins[2])      # stand on the surface
            if d[0] == "container":
                label = dict(wd.CONTAINER_MODELS).get(d[1], "")
                if label and "russian" in self.project.languages:
                    r.display_name["russian"] = label
        elif d[0] == "path":
            r.name = self._unique_name("patrol_%d" % r.id)
            r.points = [wd.PathPoint(pos)]
            L.refs.append(r)
            self.model_changed(False, moved=True)
            self.world_changed("place")
            self.worldview.select(("ref", r.id))
            self.start_path_draw(r)
            return
        elif d[0] == "entity":
            r.classname = d[1]
        L.refs.append(r)
        self.model_changed(False, moved=True)
        self.world_changed("place")
        self.worldview.select(("ref", r.id))

    def start_pick_point(self, cb, text):
        if self.scene is None:
            QMessageBox.information(self, "Карта", "Откройте карту.")
            return
        self.central_tabs.setCurrentWidget(self.map_panel)
        self.worldview.set_mode("pick", text, cb)
        self.worldview.setFocus()

    def start_path_draw(self, ref):
        if self.scene is None or self.layer is None or ref not in self.layer.refs:
            return
        self.central_tabs.setCurrentWidget(self.map_panel)

        def add(pos):
            if pos is None:
                self.model_changed(False, moved=True)
                self.world_changed("path")
                self.inspector.show_target(("ref", ref.id))
                return
            ref.points.append(wd.PathPoint(pos))
            self.world_changed("path")
        self.worldview.set_mode("path", "Маршрут «%s»: клик — точка · Enter, Esc или правая кнопка — готово" % ref.name, add)
        self.worldview.setFocus()

    def start_new_path(self, tn, pkg):
        if self.layer is None:
            QMessageBox.information(self, "Карта", "Откройте карту, на которой будет маршрут.")
            return
        r = wd.Ref(self.layer.new_id(), "path", [0, 0, 0])
        r.name = self._unique_name("%s_patrol" % tn)
        self.layer.refs.append(r)
        pkg.path = r.name
        self.model_changed(True)
        self.worldview.select(("ref", r.id), emit=False)
        self.start_path_draw(r)

    # ------------------------------------------------------------------ context menu
    def on_world_context(self, target, gpos, wpos):
        m = QMenu(self)
        if target is not None and target[0] in ("ref", "pathpt"):
            r = self.layer.ref(target[1])
            if r is not None:
                if r.type == "npc" and self.project.npc(r.npc):
                    m.addAction("Открыть NPC «%s»" % r.npc, lambda: self.open_npc(r.npc))
                    m.addAction("Распорядок…", lambda: self.open_npc(r.npc, tab=2))
                if r.type == "path":
                    m.addAction("Добавить точки…", lambda: self.start_path_draw(r))
                m.addAction("Опустить на пол", lambda: (setattr(r, "pos", self.scene.drop_to_floor(r.pos)), self.on_world_edited("drop"), self.world_changed("drop")))
                m.addAction("Повернуть на 90°", lambda: self.worldview.rotate_selected(90))
                m.addAction("Дублировать", lambda: self.duplicate_ref(r))
                m.addSeparator()
                if target[0] == "pathpt":
                    m.addAction("Удалить точку", self.delete_world_selected)
                m.addAction("Удалить", lambda: self.delete_ref(r))
                m.addSeparator()
        elif target is not None and target[0] == "hammer":
            for (i, cls, o, a, e) in self.scene.entities:
                if i == target[1]:
                    tn = e.get("targetname", "")
                    if tn and self.project.npc(tn):
                        m.addAction("Открыть NPC «%s»" % tn, lambda tn=tn: self.open_npc(tn))
                    elif tn and is_npc_class(cls):
                        m.addAction("Добавить «%s» в проект" % tn, lambda tn=tn, cls=cls, e=e: self.npc_from_hammer(tn, cls, e))
            m.addSeparator()
        if wpos is not None:
            sub = m.addMenu("Поставить сюда")
            for n in sorted(self.project.npcs, key=lambda x: x.targetname.lower())[:40]:
                sub.addAction("NPC " + n.targetname, lambda tn=n.targetname: self._place_at(("npc", tn), wpos))
            sub.addSeparator()
            for k, n, _ in wd.MARKER_KINDS:
                sub.addAction("Маркер: " + n, lambda k=k: self._place_at(("marker", k), wpos))
            sub.addSeparator()
            sub.addAction("Ящик с лутом", lambda: self._place_at(("container", wd.CONTAINER_MODELS[0][0]), wpos))
            sub.addAction("Аптечка", lambda: self._place_at(("item", "item_healthkit"), wpos))
            sub.addAction("Маршрут патруля отсюда", lambda: self._place_at(("path",), wpos))
            m.addAction("Игра отсюда (F6)", lambda: self.run_game_here(wpos))
        m.addAction("Вид сверху сюда", lambda: self._top_view(wpos))
        m.exec(gpos)

    def _place_at(self, payload, pos):
        self.place_payload = payload
        self.on_place(list(pos), [0, 0, 1])

    def _top_view(self, pos):
        if pos is None:
            return
        cam = self.worldview.cam
        cam.pos[:] = [pos[0], pos[1], pos[2] + 1400]
        cam.pitch = -89.0
        self.worldview.update()

    # ------------------------------------------------------------------ NPC helpers
    def open_npc(self, tn, tab=None):
        npc = self.project.npc(tn)
        if npc is None:
            return
        if npc is not self.npc:
            self.select_npc(npc)
        if tab is not None:
            self.inspector.npc_tab = tab
        elif self.inspector.npc_tab == 0 and self.central_tabs.currentWidget() is self.map_panel:
            self.inspector.npc_tab = 1
        self.worldview.focus_npc = tn
        self.worldview.invalidate_overlay()
        self.inspector.show_target(("npc",))

    def npc_from_hammer(self, tn, cls, e):
        if self.project.npc(tn):
            self.open_npc(tn)
            return
        npc = self._make_npc(tn, e.get("loot_displayname", ""))
        npc.actor.classname = cls
        mdl = e.get("model", "")
        if mdl.endswith(".mdl") and mdl.lower() != wd.NPC_DEFAULT_MODEL.get(cls, ""):
            npc.actor.model = mdl.lower()
        if e.get("ftag"):
            npc.actor.faction = e["ftag"]
        self.project.npcs.append(npc)
        self.model_changed(True)
        self.refresh_palette()
        self.select_npc(npc)
        self.open_npc(tn, tab=2)

    def human_models(self):
        return HUMAN_MODELS

    def browse_model(self, current, prefix="models/"):
        if self.fs is None:
            return ""
        if self._model_names is None:
            QApplication.setOverrideCursor(Qt.WaitCursor)
            try:
                self._model_names = self.fs.listdir("models", ".mdl")
            finally:
                QApplication.restoreOverrideCursor()
        dlg = ModelBrowser(self, self._model_names, current)
        if dlg.exec() == QDialog.Accepted:
            return dlg.value()
        return ""

    def model_sequences(self, model):
        if self.fs is None or not model:
            return None
        key = model.lower()
        s = self._seq_cache.get(key)
        if s is None:
            seqs = mdlmod.sequence_names(self.fs, key)
            s = set()
            for name, act in seqs:
                s.add(name.lower())
                if act:
                    s.add(act.lower())
            self._seq_cache[key] = s
        return s

    def preview_hour(self):
        return self.map_panel.hour.value() / 4.0

    def set_preview_hour(self, h):
        self.map_panel.hour.setValue(int(round(h * 4)) % 96)

    def _hour_changed(self, v):
        h = v / 4.0
        self.map_panel.hour_lbl.setText("%02d:%02d" % (int(h), int((h % 1) * 60)))
        self.worldview.hour = h
        self.worldview.invalidate_overlay()

    # ------------------------------------------------------------------ factions
    def new_faction(self, assign=None):
        fid, ok = QInputDialog.getText(self, "Новая фракция", "id фракции (ftag — латиница, без пробелов):", text="rebels")
        fid = (fid or "").strip()
        if not ok or not fid:
            return
        if not TARGETNAME_RE.match(fid) or self.project.faction(fid):
            QMessageBox.warning(self, "Фракция", "Недопустимые символы или уже есть")
            return
        f = wd.Faction(fid)
        self.project.factions.append(f)
        if assign is not None:
            assign.faction = fid
        self.model_changed(True)
        self.refresh_faction_list()
        self.inspector.show_target(("faction", fid) if assign is None else self.inspector.target)

    def rename_faction(self, fac, new):
        old = fac.id
        fac.id = new
        for n in self.project.npcs:
            if n.actor.faction == old:
                n.actor.faction = new
        for f in self.project.factions:
            if old in f.relations:
                f.relations[new] = f.relations.pop(old)
        self.model_changed(True)
        self.refresh_faction_list()
        self.inspector.show_target(("faction", new))

    def delete_faction(self, fac):
        users = [n.targetname for n in self.project.npcs if n.actor.faction == fac.id]
        if QMessageBox.question(self, "Фракция", "Удалить фракцию «%s»?%s" % (fac.id, ("\nОна указана у NPC: " + ", ".join(users)) if users else "")) != QMessageBox.Yes:
            return
        self.project.factions.remove(fac)
        for f in self.project.factions:
            f.relations.pop(fac.id, None)
        self.model_changed(True)
        self.refresh_faction_list()
        self.inspector.show_target(None)

    # ------------------------------------------------------------------ navigation
    def nav_edit_mode(self):
        if self.layer is None:
            return
        self.map_panel.toggles["nav"].setChecked(True)
        self.worldview.set_mode("nav", "Узлы навигации: клик по полу — новый узел · тащить — двигать · Delete — удалить · Esc — выйти")
        self.worldview.setFocus()

    def nav_clear(self):
        if self.layer is None or not self.layer.nav:
            return
        if QMessageBox.question(self, "Навигация", "Удалить все %d узлов карты %s?" % (len(self.layer.nav), self.cur_map)) != QMessageBox.Yes:
            return
        self.layer.nav = []
        self.model_changed(False, moved=True)
        self.world_changed("nav")

    def nav_generate(self):
        if self.layer is None or not self.project:
            QMessageBox.information(self, "Навигация", "Откройте карту.")
            return
        s = self.project.settings
        if not os.path.isfile(s.hl2_exe):
            QMessageBox.warning(self, "Навигация", "Не найден hl2.exe (Проект → Настройки).")
            return
        sp, ok = QInputDialog.getInt(self, "Навигация", "Шаг сетки узлов (единиц; 128 — улицы и комнаты, 96 — тесные помещения):",
                                     self.layer.nav_spacing or 128, 48, 512, 16)
        if not ok:
            return
        if QMessageBox.question(self, "Навигация", "Игра загрузит карту %s, разложит узлы там, куда может пройти NPC "
                                "(от игрока, NPC и маркеров), и закроется. Это займёт около минуты.\n\nПродолжить?" % self.cur_map) != QMessageBox.Yes:
            return
        rel = "cache/ck_nav_%s.txt" % self.cur_map
        out = os.path.join(s.mod_dir, rel)
        try:
            if os.path.exists(out):
                os.remove(out)
        except OSError:
            pass
        # rpm_nav_autogen: the game generates 3 s after the map is loaded and quits
        args = [s.hl2_exe, "-game", s.mod_dir, "-novid", "-windowed", "-w", "1024", "-h", "640",
                "+rpm_nav_autogen", "%d 6000 %s" % (sp, rel), "+map", self.cur_map]
        self.logln("Навигация: " + " ".join(args))
        proc = subprocess.Popen(args, cwd=os.path.dirname(s.hl2_exe))
        prog = QProgressDialog("Игра раскладывает узлы навигации на %s…" % self.cur_map, "Отмена", 0, 0, self)
        prog.setWindowTitle("Навигация")
        prog.setWindowModality(Qt.WindowModal)
        prog.show()
        t0 = time.time()
        while True:
            QApplication.processEvents()
            time.sleep(0.1)
            if prog.wasCanceled():
                proc.kill()
                break
            if os.path.isfile(out) and proc.poll() is not None:
                break
            if proc.poll() is not None and time.time() - t0 > 5:
                break
            if time.time() - t0 > 600:
                proc.kill()
                break
        prog.close()
        if not os.path.isfile(out):
            QMessageBox.warning(self, "Навигация", "Игра не записала узлы (%s). Проверьте, что server.dll мода свежий." % rel)
            return
        nodes = world_import.read_nav(out)
        try:
            os.remove(out)
        except OSError:
            pass
        self.layer.nav = nodes
        self.layer.nav_spacing = sp
        self.model_changed(False, moved=True)
        self.world_changed("nav")
        self.map_panel.toggles["nav"].setChecked(True)
        self.logln("Навигация %s: %d узлов (шаг %d). Сохраните проект — игра построит граф при следующей загрузке карты." % (self.cur_map, len(nodes), sp))
        QMessageBox.information(self, "Навигация", "Узлов: %d.\nСохраните проект (Ctrl+S): при следующей загрузке карты игра построит по ним граф пути "
                                "(в консоли «Node Graph out of Date. Rebuilding…»)." % len(nodes))

    # ------------------------------------------------------------------ game
    def run_game_here(self, pos=None):
        if not self.project or not self.cur_map:
            QMessageBox.information(self, "Игра", "Откройте карту.")
            return
        s = self.project.settings
        if not os.path.isfile(s.hl2_exe):
            QMessageBox.warning(self, "Игра", "Не найден hl2.exe: %s (Проект → Настройки)" % s.hl2_exe)
            return
        if self.dirty and not self.save():
            return
        cam = self.worldview.cam
        if pos is None:
            p = cam.pos - [0, 0, 64]
            if self.scene is not None:
                p = self.scene.drop_to_floor(list(cam.pos), up=0, down=8192)
        else:
            p = pos
        # rpm_start: the game puts the player there once the map is loaded (Source pitch is down-positive)
        start = "%.0f %.0f %.0f %.0f %.0f %.2f" % (p[0], p[1], p[2] + 4, -cam.pitch if pos is None else 0, cam.yaw, self.preview_hour())
        args = [s.hl2_exe, "-game", s.mod_dir, "-dev", "-console", "-novid", "+dialog_language", self.display_lang,
                "+rpm_start", start, "+map", self.cur_map]
        self.logln("Запуск: " + " ".join('"%s"' % a if " " in a else a for a in args))
        subprocess.Popen(args, cwd=os.path.dirname(s.hl2_exe))

    def import_world(self):
        if not self.project:
            return
        log = []
        n_npc, n_maps = world_import.import_world(self.project, self.project.settings.game_dir, log)
        self.logln("Импорт мира: " + "; ".join(log))
        self.model_changed(True)
        self.refresh_npc_list()
        self.refresh_palette()
        self.refresh_faction_list()
        self.refresh_map_list()
        if self.cur_map:
            self.layer = self.project.layer(self.cur_map, create=True)
            self.worldview.set_layer(self.layer)
            self.refresh_cell_view()
        QMessageBox.information(self, "Импорт мира", "NPC: %d, карт: %d, фракций: %d" % (n_npc, n_maps, len(self.project.factions)))
