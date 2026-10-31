"""Inspector pages of the world: actor (what the NPC is), routine (AI packages with a
24-hour timeline), placed objects, Hammer entities, factions, AI nodes."""
import math

from PySide6.QtCore import QRectF, QSize, Qt, QTime, Signal
from PySide6.QtGui import QColor, QFont, QPainter, QPen
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDoubleSpinBox, QFormLayout, QGroupBox, QHBoxLayout, QHeaderView,
                               QLabel, QLineEdit, QListWidget, QListWidgetItem, QMessageBox, QPushButton, QSizePolicy,
                               QSpinBox, QTableWidget, QTableWidgetItem, QTimeEdit, QToolButton, QVBoxLayout, QWidget)

from .. import worlddata as wd
from ..model import QUEST_STATES, QUEST_STATE_NAMES, TARGETNAME_RE
from .widgets import AutoText, LangTexts, QuestCombo, hint


def _group(title):
    g = QGroupBox(title)
    lay = QVBoxLayout(g)
    lay.setSpacing(6)
    return g, lay


def _form():
    f = QFormLayout()
    f.setLabelAlignment(Qt.AlignRight)
    f.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow)
    return f


def combo(items, current, editable=False, min_len=10):
    """items: [(data, label)]"""
    c = QComboBox()
    c.setEditable(editable)
    c.setSizeAdjustPolicy(QComboBox.AdjustToMinimumContentsLengthWithIcon)
    c.setMinimumContentsLength(min_len)
    c.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
    c.view().setMinimumWidth(320)
    for data, label in items:
        c.addItem(label, data)
    i = c.findData(current)
    if i >= 0:
        c.setCurrentIndex(i)
    elif editable:
        c.setEditText(current or "")
    return c


def hours_text(h):
    h = h % 24.0
    return "%02d:%02d" % (int(h), int(round((h - int(h)) * 60)) % 60)


class ItemsTable(QWidget):
    """[[classname, amount], ...] edited in place."""
    changed = Signal()

    def __init__(self, items, names, title="Предмет (classname)"):
        super().__init__()
        self.items = items
        self.names = names
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        self.tbl = QTableWidget(0, 2)
        self.tbl.setHorizontalHeaderLabels([title, "Кол-во"])
        self.tbl.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        self.tbl.verticalHeader().setVisible(False)
        lay.addWidget(self.tbl)
        bl = QHBoxLayout()
        a = QPushButton("+ предмет")
        a.clicked.connect(self.add)
        d = QPushButton("− выбранный")
        d.clicked.connect(self.remove)
        bl.addWidget(a)
        bl.addWidget(d)
        bl.addStretch(1)
        lay.addLayout(bl)
        self.fill()

    def fill(self):
        self.tbl.setRowCount(len(self.items))
        self.tbl.setMaximumHeight(36 + 30 * max(2, len(self.items)))
        for r, (item, amount) in enumerate(self.items):
            cb = QComboBox()
            cb.setEditable(True)
            cb.addItems(self.names)
            cb.setCurrentText(item)
            cb.currentTextChanged.connect(self.sync)
            sp = QSpinBox()
            sp.setRange(1, 999)
            sp.setValue(int(amount))
            sp.valueChanged.connect(self.sync)
            self.tbl.setCellWidget(r, 0, cb)
            self.tbl.setCellWidget(r, 1, sp)

    def sync(self, *_):
        out = []
        for r in range(self.tbl.rowCount()):
            name = self.tbl.cellWidget(r, 0).currentText().strip()
            if name:
                out.append([name, self.tbl.cellWidget(r, 1).value()])
        self.items[:] = out
        self.changed.emit()

    def add(self):
        self.items.append(["item_healthkit", 1])
        self.fill()
        self.changed.emit()

    def remove(self):
        r = self.tbl.currentRow()
        if 0 <= r < len(self.items):
            del self.items[r]
            self.fill()
            self.changed.emit()


class KVTable(QWidget):
    """{key: value} of extra keyvalues."""
    changed = Signal()

    def __init__(self, kv):
        super().__init__()
        self.kv = kv
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        self.tbl = QTableWidget(0, 2)
        self.tbl.setHorizontalHeaderLabels(["Ключ", "Значение"])
        self.tbl.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self.tbl.verticalHeader().setVisible(False)
        lay.addWidget(self.tbl)
        bl = QHBoxLayout()
        a = QPushButton("+ ключ")
        a.clicked.connect(self.add)
        d = QPushButton("− выбранный")
        d.clicked.connect(self.remove)
        bl.addWidget(a)
        bl.addWidget(d)
        bl.addStretch(1)
        lay.addLayout(bl)
        self.fill()
        self.tbl.itemChanged.connect(self.sync)

    def fill(self):
        self.tbl.blockSignals(True)
        self.tbl.setRowCount(len(self.kv))
        self.tbl.setMaximumHeight(36 + 28 * max(2, len(self.kv)))
        for r, (k, v) in enumerate(sorted(self.kv.items())):
            self.tbl.setItem(r, 0, QTableWidgetItem(k))
            self.tbl.setItem(r, 1, QTableWidgetItem(str(v)))
        self.tbl.blockSignals(False)

    def sync(self, *_):
        new = {}
        for r in range(self.tbl.rowCount()):
            k = self.tbl.item(r, 0)
            v = self.tbl.item(r, 1)
            if k and k.text().strip():
                new[k.text().strip()] = v.text() if v else ""
        self.kv.clear()
        self.kv.update(new)
        self.changed.emit()

    def add(self):
        n = 1
        while ("key%d" % n) in self.kv:
            n += 1
        self.kv["key%d" % n] = ""
        self.fill()
        self.changed.emit()

    def remove(self):
        r = self.tbl.currentRow()
        if r >= 0 and self.tbl.item(r, 0):
            self.kv.pop(self.tbl.item(r, 0).text(), None)
            self.fill()
            self.changed.emit()


class Timeline(QWidget):
    """24 hours: the top bar is what the NPC does (first matching package wins), the rows
    below are each package's own window (faded where a higher package overrides it)."""
    picked = Signal(int)
    hourPicked = Signal(float)

    def __init__(self, actor, selected=-1, hour=12.0):
        super().__init__()
        self.actor = actor
        self.selected = selected
        self.hour = hour
        self.setMinimumHeight(self._height())
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.setToolTip("Верхняя полоса — что NPC делает в течение суток (побеждает первый подходящий пакет, как в GECK).\n"
                        "Строки ниже — окна времени каждого пакета. Клик по строке — выбрать пакет, по шкале — время на карте.")

    def _height(self):
        return 40 + 18 * max(1, len(self.actor.packages)) + 6

    def sizeHint(self):
        return QSize(360, self._height())

    def paintEvent(self, e):
        qp = QPainter(self)
        qp.setRenderHint(QPainter.Antialiasing)
        w = self.width()
        x0, x1 = 6, w - 6
        span = x1 - x0

        def X(h):
            return x0 + span * (h / 24.0)
        qp.fillRect(self.rect(), QColor("#1f242b"))
        # effective bar
        steps = 96
        for k in range(steps):
            h = (k + 0.5) * 24.0 / steps
            p = self.actor.package_at(h)
            col = QColor(wd.PACKAGE_COLORS.get(p.type, "#666")) if p else QColor("#2c323a")
            qp.fillRect(QRectF(X(k * 24.0 / steps), 6, span / steps + 0.6, 16), col)
        qp.setPen(QColor("#aab4c0"))
        qp.setFont(QFont("Segoe UI", 7))
        for h in range(0, 25, 3):
            qp.drawLine(int(X(h)), 22, int(X(h)), 26)
            if h < 24:
                qp.drawText(int(X(h)) + 2, 35, "%02d" % h)
        # rows
        y = 40
        for i, p in enumerate(self.actor.packages):
            col = QColor(wd.PACKAGE_COLORS.get(p.type, "#888"))
            if i == self.selected:
                qp.fillRect(QRectF(0, y - 1, w, 17), QColor("#34506e"))
            for k in range(steps):
                h = (k + 0.5) * 24.0 / steps
                if not p.active_at(h):
                    continue
                win = self.actor.package_at(h) is p
                c = QColor(col)
                c.setAlpha(255 if win else 70)
                qp.fillRect(QRectF(X(k * 24.0 / steps), y + 2, span / steps + 0.6, 12), c)
            qp.setPen(QColor("#eef2f6"))
            qp.setFont(QFont("Segoe UI", 8))
            qp.drawText(QRectF(x0 + 4, y, span, 16), Qt.AlignVCenter | Qt.AlignLeft, "%d. %s" % (i + 1, p.name))
            y += 18
        # preview hour
        qp.setPen(QPen(QColor("#ffd27a"), 2))
        qp.drawLine(int(X(self.hour)), 2, int(X(self.hour)), self.height() - 2)
        qp.end()

    def mousePressEvent(self, e):
        y = e.position().y()
        x = e.position().x()
        if y < 38:
            h = max(0.0, min(23.99, (x - 6) / max(1, self.width() - 12) * 24.0))
            self.hourPicked.emit(h)
            return
        i = int((y - 40) // 18)
        if 0 <= i < len(self.actor.packages):
            self.picked.emit(i)


# ======================================================================== actor
def actor_tab(insp, npc):
    ctx = insp.ctx
    p = ctx.project
    a = npc.actor
    w = QWidget()
    lay = QVBoxLayout(w)
    lay.setContentsMargins(0, 6, 0, 0)
    lay.addWidget(hint("Кто этот NPC в игре (как запись актёра в GECK). Если NPC поставлен на карту из программы, "
                       "игра создаёт его по этим данным; если он стоит в Hammer с тем же targetname — "
                       "берёт распорядок, фракцию, бессмертие и отношение к игроку."))

    def ch(structural=False):
        insp.changed(structural)
        ctx.world_changed("actor")

    f = _form()
    cls = combo([(c, "%s  (%s)" % (n, c)) for c, n, _, _ in wd.NPC_CLASSES], a.classname, editable=True)

    def set_cls(*_):
        v = cls.currentData() if cls.currentIndex() >= 0 and cls.currentText() == cls.itemText(cls.currentIndex()) else cls.currentText().split()[0]
        a.classname = (v or "npc_citizen").strip()
        ch(True)
        insp.show_target(insp.target)
    cls.activated.connect(set_cls)
    cls.lineEdit().editingFinished.connect(set_cls)
    f.addRow("Класс:", cls)
    if not wd.NPC_HAS_ROUTINE.get(a.classname, True):
        f.addRow("", hint("У этого класса нет распорядка в игре (нет поведения actbusy) — пакеты не будут работать."))

    mrow = QHBoxLayout()
    models = sorted(set([m for _, _, m, _ in wd.NPC_CLASSES] + ctx.human_models()))
    mdl = combo([("", "по умолчанию: " + wd.NPC_DEFAULT_MODEL.get(a.classname, ""))] + [(m, m) for m in models], a.model, editable=True, min_len=18)

    def set_model(*_):
        t = mdl.currentText().strip()
        a.model = "" if t.startswith("по умолчанию") else t
        ch(True)
    mdl.activated.connect(set_model)
    mdl.lineEdit().editingFinished.connect(set_model)
    mrow.addWidget(mdl, 1)
    br = QToolButton(text="…")
    br.setToolTip("Выбрать модель из игры")

    def browse():
        m = ctx.browse_model(a.model or wd.NPC_DEFAULT_MODEL.get(a.classname, ""), "models/")
        if m:
            a.model = m
            ch(True)
            insp.show_target(insp.target)
    br.clicked.connect(browse)
    mrow.addWidget(br)
    f.addRow("Модель:", mrow)
    sk = QSpinBox()
    sk.setRange(0, 32)
    sk.setValue(a.skin)
    sk.valueChanged.connect(lambda v: (setattr(a, "skin", v), ch()))
    f.addRow("Скин:", sk)
    if a.classname == "npc_citizen":
        ct = combo(wd.CITIZEN_TYPES, a.citizen_type)
        ct.currentIndexChanged.connect(lambda _: (setattr(a, "citizen_type", ct.currentData()), ch()))
        f.addRow("Тип горожанина:", ct)
    wp = combo([(x, x or "— без оружия —") for x in wd.WEAPONS], a.weapon, editable=True)

    def set_wp(*_):
        t = wp.currentText().strip()
        a.weapon = "" if t.startswith("—") else t
        ch()
    wp.activated.connect(set_wp)
    wp.lineEdit().editingFinished.connect(set_wp)
    f.addRow("Оружие:", wp)
    hp = QSpinBox()
    hp.setRange(0, 100000)
    hp.setSpecialValueText("по умолчанию")
    hp.setValue(a.health)
    hp.valueChanged.connect(lambda v: (setattr(a, "health", v), ch()))
    f.addRow("Здоровье:", hp)
    lay.addLayout(f)

    g, gl = _group("Отношения")
    f = _form()
    fac = combo([("", "— без фракции —")] + [(x.id, x.id) for x in p.factions], a.faction, editable=True)

    def set_fac(*_):
        t = fac.currentText().strip()
        a.faction = "" if t.startswith("—") else t
        ch(True)
    fac.activated.connect(set_fac)
    fac.lineEdit().editingFinished.connect(set_fac)
    frow = QHBoxLayout()
    frow.addWidget(fac, 1)
    nf = QToolButton(text="+")
    nf.setToolTip("Новая фракция")
    nf.clicked.connect(lambda: ctx.new_faction(assign=a))
    frow.addWidget(nf)
    f.addRow("Фракция:", frow)
    rel = combo(wd.RELATIONS, a.relation)
    rel.currentIndexChanged.connect(lambda _: (setattr(a, "relation", rel.currentData()), ch()))
    f.addRow("К игроку:", rel)
    sq = QLineEdit(a.squad)
    sq.setPlaceholderText("отряд: NPC одного отряда помогают друг другу")
    sq.editingFinished.connect(lambda: (setattr(a, "squad", sq.text().strip()), ch()))
    f.addRow("Отряд:", sq)
    gl.addLayout(f)
    for attr, text, tip in (("hate_on_dmg", "Становится врагом, если его ранить", ""),
                            ("spoil_on_dmg", "Перестаёт разговаривать, если его ранить", ""),
                            ("essential", "Бессмертный (как Essential в GECK)", "Получает урон, но не умирает — для сюжетных NPC"),
                            ("commandable", "Можно позвать в отряд игрока", "Иначе горожанин не вступает в отряд игрока (HL2 набирает дружественных горожан сам)")):
        cb = QCheckBox(text)
        cb.setChecked(getattr(a, attr))
        if tip:
            cb.setToolTip(tip)
        cb.toggled.connect(lambda v, attr=attr: (setattr(a, attr, bool(v)), ch()))
        gl.addWidget(cb)
    lay.addWidget(g)

    g, gl = _group("Инвентарь трупа (лут)")
    it = ItemsTable(a.loot_items, ctx.item_names())
    it.changed.connect(ch)
    gl.addWidget(it)
    lay.addWidget(g)

    g, gl = _group("Где стоит")
    pl = p.placements(npc.targetname)
    for mname, r in pl:
        b = QPushButton("%s: %.0f %.0f %.0f — показать" % (mname, r.pos[0], r.pos[1], r.pos[2]))
        b.setFlat(True)
        b.setStyleSheet("text-align:left; color:#7fb6ff;")
        b.clicked.connect(lambda _=False, m=mname, rid=r.id: ctx.focus_ref(m, rid))
        gl.addWidget(b)
    hammer = ctx.mapindex.find(npc.targetname) if hasattr(ctx, "mapindex") else []
    for e in hammer:
        l = QLabel("Hammer: %s (%s)" % (e.map, e.classname))
        l.setObjectName("hint")
        gl.addWidget(l)
    if not pl and not hammer:
        gl.addWidget(hint("Пока нигде. Поставьте его на открытую карту кнопкой ниже или перетащите из списка «Объекты»."))
    pb = QPushButton("Поставить на карту…")
    pb.clicked.connect(lambda: ctx.start_place_npc(npc.targetname))
    gl.addWidget(pb)
    lay.addWidget(g)

    g, gl = _group("Дополнительные ключи (для знатоков Hammer)")
    kvt = KVTable(a.keyvalues)
    kvt.changed.connect(ch)
    gl.addWidget(kvt)
    gl.addWidget(hint("Любые keyvalues сущности: например, expressiontype, spawnflags, additionalequipment."))
    lay.addWidget(g)
    lay.addStretch(1)
    return w


# ======================================================================== routine
def routine_tab(insp, npc):
    ctx = insp.ctx
    p = ctx.project
    a = npc.actor
    w = QWidget()
    lay = QVBoxLayout(w)
    lay.setContentsMargins(0, 6, 0, 0)
    lay.addWidget(hint("Распорядок — пакеты ИИ, как в GECK: в каждый момент работает первый пакет списка, у которого "
                       "подходит время и условия. Бой, скрипты карты и диалог с игроком прерывают распорядок."))
    sel = insp.pkg_sel if 0 <= insp.pkg_sel < len(a.packages) else (0 if a.packages else -1)
    insp.pkg_sel = sel
    tl = Timeline(a, sel, ctx.preview_hour())
    tl.picked.connect(lambda i: (setattr(insp, "pkg_sel", i), insp.show_target(insp.target)))
    tl.hourPicked.connect(lambda h: (ctx.set_preview_hour(h), insp.show_target(insp.target)))
    lay.addWidget(tl)
    cur = a.package_at(ctx.preview_hour())
    if cur:
        tname = wd.PACKAGE_NAMES.get(cur.type, cur.type)
        now = "«%s»" % cur.name if cur.name == tname else "«%s» (%s)" % (cur.name, tname)
    else:
        now = "ничего — стоит как обычный NPC"
    lay.addWidget(hint("В %s: %s. Клик по шкале — другое время." % (hours_text(ctx.preview_hour()), now)))

    lst = QListWidget()
    lst.setMaximumHeight(28 + 22 * max(3, len(a.packages)))
    for i, pk in enumerate(a.packages):
        it = QListWidgetItem("%d. %s — %s, %s" % (i + 1, pk.name, wd.PACKAGE_NAMES.get(pk.type, pk.type),
                                                 "весь день" if pk.always() else "%s–%s" % (hours_text(pk.start), hours_text(pk.start + pk.duration))))
        it.setForeground(QColor(wd.PACKAGE_COLORS.get(pk.type, "#ddd")))
        lst.addItem(it)
    if sel >= 0:
        lst.setCurrentRow(sel)
    lst.currentRowChanged.connect(lambda i: (setattr(insp, "pkg_sel", i), insp.show_target(insp.target)) if i != insp.pkg_sel else None)
    lay.addWidget(lst)
    bl = QHBoxLayout()
    add = QPushButton("+ пакет")

    def add_pkg():
        from PySide6.QtWidgets import QMenu
        m = QMenu(w)
        for k, n, h in wd.PACKAGE_TYPES:
            act = m.addAction("%s — %s" % (n, h))
            act.setData(k)
        r = m.exec(add.mapToGlobal(add.rect().bottomLeft()))
        if r is None:
            return
        pk = wd.Package(r.data())
        if pk.type == "sleep":
            pk.start, pk.duration = 22.0, 8.0
        elif pk.type == "eat":
            pk.start, pk.duration = 7.0, 1.0
        elif pk.type == "patrol":
            pk.start, pk.duration = 8.0, 10.0
        a.packages.append(pk)
        insp.pkg_sel = len(a.packages) - 1
        insp.changed(True)
        ctx.world_changed("packages")
        insp.show_target(insp.target)
    add.clicked.connect(add_pkg)
    bl.addWidget(add)
    for txt, d in (("▲", -1), ("▼", 1)):
        b = QToolButton(text=txt)
        b.setToolTip("Выше в списке — важнее" if d < 0 else "Ниже")

        def mv(_=False, d=d):
            i = insp.pkg_sel
            j = i + d
            if 0 <= i < len(a.packages) and 0 <= j < len(a.packages):
                a.packages[i], a.packages[j] = a.packages[j], a.packages[i]
                insp.pkg_sel = j
                insp.changed(True)
                ctx.world_changed("packages")
                insp.show_target(insp.target)
        b.clicked.connect(mv)
        bl.addWidget(b)
    dup = QToolButton(text="⧉")
    dup.setToolTip("Копия пакета")

    def dup_pkg():
        i = insp.pkg_sel
        if 0 <= i < len(a.packages):
            c = wd.Package.from_dict(a.packages[i].to_dict())
            c.uid = wd.Package().uid
            c.name += " (копия)"
            a.packages.insert(i + 1, c)
            insp.pkg_sel = i + 1
            insp.changed(True)
            insp.show_target(insp.target)
    dup.clicked.connect(dup_pkg)
    bl.addWidget(dup)
    de = QToolButton(text="✕")
    de.setToolTip("Удалить пакет")

    def del_pkg():
        i = insp.pkg_sel
        if 0 <= i < len(a.packages):
            del a.packages[i]
            insp.pkg_sel = min(i, len(a.packages) - 1)
            insp.changed(True)
            ctx.world_changed("packages")
            insp.show_target(insp.target)
    de.clicked.connect(del_pkg)
    bl.addWidget(de)
    bl.addStretch(1)
    lay.addLayout(bl)
    if 0 <= sel < len(a.packages):
        lay.addWidget(package_editor(insp, npc, a.packages[sel]))
    elif not a.packages:
        lay.addWidget(hint("Пакетов нет — NPC стоит на месте и ведёт себя как обычно в HL2. Типичный день: «Сон» 22:00–6:00, "
                           "«Еда» 7:00–8:00, «Патруль» или «Охрана» днём, «Свободное время» вечером."))
    lay.addStretch(1)
    return w


def package_editor(insp, npc, pk):
    ctx = insp.ctx
    p = ctx.project
    g, gl = _group("Пакет «%s»" % pk.name)

    def ch(structural=False, rebuild=False):
        insp.changed(structural)
        ctx.world_changed("packages")
        if rebuild:
            insp.show_target(insp.target)
    f = _form()
    nm = QLineEdit(pk.name)
    nm.editingFinished.connect(lambda: (setattr(pk, "name", nm.text().strip() or wd.PACKAGE_NAMES.get(pk.type, pk.type)), ch(True, True)))
    f.addRow("Название:", nm)
    tp = combo([(k, n) for k, n, _ in wd.PACKAGE_TYPES], pk.type)

    def set_type(_):
        old_default = pk.name == wd.PACKAGE_NAMES.get(pk.type, pk.type)
        pk.type = tp.currentData()
        if old_default:
            pk.name = wd.PACKAGE_NAMES.get(pk.type, pk.type)
        pk.once = pk.type == "talk"
        ch(True, True)
    tp.currentIndexChanged.connect(set_type)
    f.addRow("Что делает:", tp)
    f.addRow("", hint(wd.PACKAGE_HELP.get(pk.type, "")))
    # time
    trow = QHBoxLayout()
    allday = QCheckBox("весь день")
    allday.setChecked(pk.always())
    t0 = QTimeEdit(QTime(int(pk.start) % 24, int(round((pk.start % 1) * 60)) % 60))
    t0.setDisplayFormat("HH:mm")
    end = (pk.start + (pk.duration if not pk.always() else 8)) % 24
    t1 = QTimeEdit(QTime(int(end) % 24, int(round((end % 1) * 60)) % 60))
    t1.setDisplayFormat("HH:mm")

    def set_time(*_):
        if allday.isChecked():
            pk.duration = 24.0
        else:
            s = t0.time().hour() + t0.time().minute() / 60.0
            e = t1.time().hour() + t1.time().minute() / 60.0
            pk.start = s
            pk.duration = (e - s) % 24.0 or 24.0
        t0.setEnabled(not allday.isChecked())
        t1.setEnabled(not allday.isChecked())
        ch(True)
    t0.setEnabled(not pk.always())
    t1.setEnabled(not pk.always())
    allday.toggled.connect(set_time)
    t0.editingFinished.connect(set_time)
    t1.editingFinished.connect(set_time)
    trow.addWidget(QLabel("с"))
    trow.addWidget(t0)
    trow.addWidget(QLabel("до"))
    trow.addWidget(t1)
    trow.addWidget(allday)
    trow.addStretch(1)
    f.addRow("Время:", trow)
    gl.addLayout(f)

    t = pk.type
    uses_loc = t in ("travel", "guard", "wait", "wander", "sandbox", "sleep", "eat", "use_marker")
    if uses_loc:
        f = _form()
        loc = combo(wd.LOCATIONS, pk.location)

        def set_loc(_):
            pk.location = loc.currentData()
            ch(False, True)
        loc.currentIndexChanged.connect(set_loc)
        lbl = {"sleep": "Искать кровать возле:", "eat": "Искать стол возле:", "use_marker": "Искать возле:",
               "wander": "Центр:", "sandbox": "Центр:"}.get(t, "Где:")
        f.addRow(lbl, loc)
        if pk.location == "ref" or t == "use_marker":
            names = ctx.ref_names_for(npc.targetname)
            rc = combo([("", "—")] + [(n, n) for n in names], pk.ref, editable=True)

            def set_ref(*_):
                v = rc.currentText().strip()
                pk.ref = "" if v == "—" else v
                ch()
            rc.activated.connect(set_ref)
            rc.lineEdit().editingFinished.connect(set_ref)
            f.addRow("Маркер / объект:" if t != "use_marker" else "Маркер (по имени):", rc)
            if t == "use_marker":
                f.addRow("", hint("Пусто — любой свободный маркер выбранных ниже видов в радиусе."))
        if pk.location == "point":
            prow = QHBoxLayout()
            spins = []
            for k in range(3):
                s = QDoubleSpinBox()
                s.setRange(-32768, 32768)
                s.setDecimals(0)
                s.setValue(pk.point[k])
                s.valueChanged.connect(lambda v, k=k: (pk.point.__setitem__(k, v), ch()))
                spins.append(s)
                prow.addWidget(s)
            pick = QPushButton("На карте…")
            pick.setToolTip("Кликните в нужное место в окне карты")

            def do_pick():
                def got(pos):
                    pk.point = [round(pos[0], 1), round(pos[1], 1), round(pos[2], 1)]
                    pk.point_map = ctx.current_map_name()
                    ch(False, True)
                ctx.start_pick_point(got, "Клик — точка для пакета «%s» · Esc — отмена" % pk.name)
            pick.clicked.connect(do_pick)
            prow.addWidget(pick)
            f.addRow("Точка:", prow)
        if t in ("travel", "guard", "wait"):
            yrow = QHBoxLayout()
            hy = QCheckBox("смотреть в сторону")
            hy.setChecked(pk.yaw is not None)
            ys = QSpinBox()
            ys.setRange(0, 359)
            ys.setWrapping(True)
            ys.setSuffix("°")
            ys.setValue(int(pk.yaw or 0))
            ys.setEnabled(pk.yaw is not None)

            def set_yaw(*_):
                pk.yaw = float(ys.value()) if hy.isChecked() else None
                ys.setEnabled(hy.isChecked())
                ch()
            hy.toggled.connect(set_yaw)
            ys.valueChanged.connect(set_yaw)
            yrow.addWidget(hy)
            yrow.addWidget(ys)
            yrow.addStretch(1)
            f.addRow("", yrow)
        gl.addLayout(f)
    f = _form()
    if t in ("wander", "sandbox", "sleep", "eat", "use_marker", "follow", "talk", "travel"):
        rad = QSpinBox()
        rad.setRange(0, 16384)
        rad.setSingleStep(64)
        rad.setSpecialValueText({"wander": "512", "sandbox": "768", "follow": "128", "talk": "1500", "travel": "48"}.get(t, "2048"))
        rad.setValue(int(pk.radius))
        rad.valueChanged.connect(lambda v: (setattr(pk, "radius", v), ch()))
        f.addRow({"follow": "Держится на:", "talk": "Замечает игрока с:", "travel": "Точность:"}.get(t, "Радиус:"), rad)
    if t in ("sandbox", "use_marker"):
        kw = QWidget()
        kl = QHBoxLayout(kw)
        kl.setContentsMargins(0, 0, 0, 0)
        grid = QVBoxLayout()
        row = None
        for i, (k, n, c) in enumerate(wd.MARKER_KINDS):
            if i % 2 == 0:
                row = QHBoxLayout()
                grid.addLayout(row)
            cb = QCheckBox(n)
            cb.setChecked(k in pk.markers)

            def tog(v, k=k):
                if v and k not in pk.markers:
                    pk.markers.append(k)
                elif not v and k in pk.markers:
                    pk.markers.remove(k)
                ch()
            cb.toggled.connect(tog)
            row.addWidget(cb)
        kl.addLayout(grid)
        f.addRow("Маркеры:", kw)
        if t == "sandbox":
            f.addRow("", hint("Не отмечено ничего — стулья, стены, столы, земля, работа и точки ожидания."))
            urow = QHBoxLayout()
            mn = QSpinBox()
            mn.setRange(5, 3600)
            mn.setValue(pk.min_use)
            mx = QSpinBox()
            mx.setRange(5, 3600)
            mx.setValue(pk.max_use)
            mn.valueChanged.connect(lambda v: (setattr(pk, "min_use", v), ch()))
            mx.valueChanged.connect(lambda v: (setattr(pk, "max_use", v), ch()))
            urow.addWidget(mn)
            urow.addWidget(QLabel("–"))
            urow.addWidget(mx)
            urow.addWidget(QLabel("с у маркера"))
            urow.addStretch(1)
            f.addRow("Сидит:", urow)
    if t == "patrol":
        paths = ctx.path_names_for(npc.targetname)
        pc = combo([("", "—")] + [(n, n) for n in paths], pk.path, editable=True)

        def set_path(*_):
            v = pc.currentText().strip()
            pk.path = "" if v == "—" else v
            ch()
        pc.activated.connect(set_path)
        pc.lineEdit().editingFinished.connect(set_path)
        prow = QHBoxLayout()
        prow.addWidget(pc, 1)
        nb = QPushButton("Нарисовать…")
        nb.setToolTip("Новый маршрут: кликайте точки на карте, Enter или правая кнопка — готово")
        nb.clicked.connect(lambda: ctx.start_new_path(npc.targetname, pk))
        prow.addWidget(nb)
        f.addRow("Маршрут:", prow)
        pm = combo([("loop", "по кругу"), ("pingpong", "туда и обратно"), ("once", "один раз")], pk.path_mode)
        pm.currentIndexChanged.connect(lambda _: (setattr(pk, "path_mode", pm.currentData()), ch()))
        f.addRow("Как идти:", pm)
    if t == "follow":
        names = ["!player"] + sorted(n.targetname for n in p.npcs if n is not npc)
        tc = combo([(n, "игрок" if n == "!player" else n) for n in names], pk.target or "!player", editable=True)

        def set_tg(*_):
            v = tc.currentData() if tc.currentIndex() >= 0 and tc.currentText() == tc.itemText(tc.currentIndex()) else tc.currentText().strip()
            pk.target = "" if v in ("!player", "игрок") else v
            ch()
        tc.activated.connect(set_tg)
        tc.lineEdit().editingFinished.connect(set_tg)
        f.addRow("За кем:", tc)
    if t in ("wait", "travel", "guard", "sleep"):
        anims = [("", "— без анимации —" if t != "sleep" else "— на земле: сидя —")] + [(x, "%s  (%s)" % (n, x)) for x, n, _ in wd.ANIMS]
        an = combo(anims, pk.anim, editable=True)

        def set_anim(*_):
            v = an.currentData() if an.currentIndex() >= 0 and an.currentText() == an.itemText(an.currentIndex()) else an.currentText().strip()
            pk.anim = v or ""
            ch(False, True)
        an.activated.connect(set_anim)
        an.lineEdit().editingFinished.connect(set_anim)
        f.addRow("Анимация:" if t != "sleep" else "Без кровати:", an)
        if pk.anim:
            f.addRow("", anim_check(ctx, npc, pk.anim))
    if t in ("travel", "guard", "patrol", "wander", "sandbox", "follow", "talk", "sleep", "eat", "use_marker"):
        rn = QCheckBox("бегом")
        rn.setChecked(pk.run)
        rn.toggled.connect(lambda v: (setattr(pk, "run", bool(v)), ch()))
        f.addRow("", rn)
    if t == "talk":
        on = QCheckBox("только один раз за игру")
        on.setChecked(pk.once)
        on.toggled.connect(lambda v: (setattr(pk, "once", bool(v)), ch()))
        f.addRow("", on)
        if not npc.has_dialog():
            f.addRow("", hint("У этого NPC нет диалога — подойдёт молча. Напишите реплики во вкладке «Диалог»."))
    gl.addLayout(f)

    # conditions
    cg, cl = _group("Условие (пусто — всегда)")
    cf = _form()
    qs = QuestCombo(ctx, pk.req_state[0] if pk.req_state else "", "— квест —")
    st = QComboBox()
    for s in QUEST_STATES:
        st.addItem(QUEST_STATE_NAMES[s], s)
    st.setCurrentIndex(max(0, st.findData(pk.req_state[1] if pk.req_state else "active")))

    def set_state(*_):
        pk.req_state = [qs.currentData(), st.currentData()] if qs.currentData() else None
        ch()
    qs.picked.connect(set_state)
    st.currentIndexChanged.connect(set_state)
    r1 = QHBoxLayout()
    r1.addWidget(qs, 1)
    r1.addWidget(st)
    cf.addRow("Квест в состоянии:", r1)
    qg = QuestCombo(ctx, pk.req_stage[0] if pk.req_stage else "", "— квест —")
    sg = QSpinBox()
    sg.setRange(0, 99)
    sg.setValue(pk.req_stage[1] if pk.req_stage else 0)

    def set_stage(*_):
        pk.req_stage = [qg.currentData(), sg.value()] if qg.currentData() else None
        ch()
    qg.picked.connect(set_stage)
    sg.valueChanged.connect(set_stage)
    r2 = QHBoxLayout()
    r2.addWidget(qg, 1)
    r2.addWidget(QLabel("этап ≥"))
    r2.addWidget(sg)
    cf.addRow("Квест на этапе:", r2)
    cl.addLayout(cf)
    gl.addWidget(cg)
    note = AutoText(pk.note, 1, 4, "заметка")
    note.edited.connect(lambda tx: (setattr(pk, "note", tx), insp.changed(False)))
    gl.addWidget(note)
    return g


def anim_check(ctx, npc, anim):
    model = npc.actor.model_or_default()
    names = ctx.model_sequences(model)
    if names is None:
        return hint("")
    ok = anim.lower() in names
    short = model.rsplit("/", 1)[-1]
    lbl = QLabel(("✓ у модели %s есть эта анимация" % short) if ok else ("⚠ у модели %s нет «%s» — NPC будет просто стоять" % (short, anim)))
    lbl.setStyleSheet("color:%s" % ("#79c27f" if ok else "#e0a040"))
    lbl.setWordWrap(True)
    return lbl


# ======================================================================== placed objects
def ref_page(insp, layer, ref):
    ctx = insp.ctx
    p = ctx.project
    w, lay = insp._page("%s  #%d" % (wd.REF_TYPE_NAMES.get(ref.type, ref.type), ref.id),
                        "На карте %s. Тащите мышью в окне карты, Alt+тащить — повернуть, [ ] — поворот на 15°, Delete — удалить." % layer.name)

    def ch(structural=False, rebuild=False):
        insp.changed(structural)
        ctx.world_changed("ref")
        if rebuild:
            insp.show_target(insp.target)
    f = _form()
    if ref.type in ("marker", "path", "item", "container", "prop", "entity"):
        nm = QLineEdit(ref.name)
        nm.setPlaceholderText({"marker": "имя (на него ссылаются пакеты)", "path": "имя маршрута"}.get(ref.type, "targetname (необязательно)"))

        def set_name():
            v = nm.text().strip()
            if v and not TARGETNAME_RE.match(v):
                QMessageBox.warning(w, "Имя", "Только латиница, цифры и _ - .")
                nm.setText(ref.name)
                return
            old = ref.name
            ref.name = v
            if old and v != old:
                ctx.rename_world_name(old, v)
            ch(True)
        nm.editingFinished.connect(set_name)
        f.addRow("Имя:", nm)
    if ref.type != "path":
        prow = QHBoxLayout()
        for k in range(3):
            s = QDoubleSpinBox()
            s.setRange(-32768, 32768)
            s.setDecimals(1)
            s.setValue(ref.pos[k])
            s.valueChanged.connect(lambda v, k=k: (ref.pos.__setitem__(k, v), ch()))
            prow.addWidget(s)
        drop = QToolButton(text="↓")
        drop.setToolTip("Опустить на пол")

        def do_drop():
            if ctx.scene is not None:
                ref.pos = ctx.scene.drop_to_floor(ref.pos)
                ch(False, True)
        drop.clicked.connect(do_drop)
        prow.addWidget(drop)
        f.addRow("Позиция:", prow)
        yw = QSpinBox()
        yw.setRange(0, 359)
        yw.setWrapping(True)
        yw.setSuffix("°")
        yw.setValue(int(round(ref.angles[1])) % 360)
        yw.valueChanged.connect(lambda v: (ref.angles.__setitem__(1, float(v)), ch()))
        f.addRow("Поворот:", yw)
    lay.addLayout(f)

    if ref.type == "npc":
        f = _form()
        names = sorted(n.targetname for n in p.npcs)
        nc = combo([(n, n) for n in names], ref.npc)

        def set_npc(_):
            ref.npc = nc.currentData()
            ch(True, True)
        nc.currentIndexChanged.connect(set_npc)
        f.addRow("NPC:", nc)
        lay.addLayout(f)
        npc = p.npc(ref.npc)
        if npc is not None:
            ob = QPushButton("Открыть NPC «%s» (персонаж, распорядок, диалог)" % ref.npc)
            ob.clicked.connect(lambda: ctx.open_npc(ref.npc))
            lay.addWidget(ob)
            pk = npc.actor.package_at(ctx.preview_hour())
            lay.addWidget(hint("В %s: %s" % (hours_text(ctx.preview_hour()), ("«%s»" % pk.name) if pk else "нет пакета")))
        else:
            lay.addWidget(hint("NPC «%s» нет в проекте." % ref.npc))
    elif ref.type == "marker":
        f = _form()
        kd = combo([(k, n) for k, n, _ in wd.MARKER_KINDS], ref.kind)

        def set_kind(_):
            old = ref.kind
            ref.kind = kd.currentData()
            if not ref.anim or ref.anim == wd.DEFAULT_MARKER_ANIM.get(old, ""):
                ref.anim = wd.DEFAULT_MARKER_ANIM.get(ref.kind, "")
            ch(True, True)
        kd.currentIndexChanged.connect(set_kind)
        f.addRow("Вид:", kd)
        anims = [("", "— без анимации (стоять) —")] + [(x, "%s  (%s)" % (n, x)) for x, n, k in wd.ANIMS if k == ref.kind] + \
                [(x, "%s  (%s)" % (n, x)) for x, n, k in wd.ANIMS if k != ref.kind]
        an = combo(anims, ref.anim, editable=True)

        def set_anim(*_):
            v = an.currentData() if an.currentIndex() >= 0 and an.currentText() == an.itemText(an.currentIndex()) else an.currentText().strip()
            ref.anim = v or ""
            ch(False, True)
        an.activated.connect(set_anim)
        an.lineEdit().editingFinished.connect(set_anim)
        f.addRow("Анимация:", an)
        own = combo([("", "— любой NPC —")] + [(n.targetname, n.targetname) for n in sorted(p.npcs, key=lambda x: x.targetname)], ref.owner)
        own.currentIndexChanged.connect(lambda _: (setattr(ref, "owner", own.currentData()), ch(True)))
        f.addRow("Владелец:", own)
        lay.addLayout(f)
        lay.addWidget(hint("NPC встаёт в точку маркера лицом по стрелке и играет анимацию. Для кровати и стула ставьте маркер "
                           "туда, где должен оказаться NPC, — точное место подбирается в игре (F8 — обновить)."))
        if ref.anim and ref.owner and p.npc(ref.owner):
            lay.addWidget(anim_check(ctx, p.npc(ref.owner), ref.anim))
        users = [n.targetname for n in p.npcs for pk in n.actor.packages if pk.ref == ref.name and ref.name]
        if users:
            lay.addWidget(hint("Используют пакеты: " + ", ".join(sorted(set(users)))))
    elif ref.type == "item":
        f = _form()
        ic = combo([(x, x) for x in ctx.item_names()], ref.classname, editable=True)

        def set_cls(*_):
            ref.classname = ic.currentText().strip()
            ch(True)
        ic.activated.connect(set_cls)
        ic.lineEdit().editingFinished.connect(set_cls)
        f.addRow("Предмет:", ic)
        lay.addLayout(f)
    elif ref.type in ("container", "prop"):
        f = _form()
        lst = wd.CONTAINER_MODELS if ref.type == "container" else wd.PROP_MODELS
        mc = combo([(m, "%s  (%s)" % (n, m.rsplit("/", 1)[-1])) for m, n in lst], ref.model, editable=True, min_len=16)

        def set_model(*_):
            v = mc.currentData() if mc.currentIndex() >= 0 and mc.currentText() == mc.itemText(mc.currentIndex()) else mc.currentText().strip()
            ref.model = v
            ch(True)
        mc.activated.connect(set_model)
        mc.lineEdit().editingFinished.connect(set_model)
        mrow = QHBoxLayout()
        mrow.addWidget(mc, 1)
        br = QToolButton(text="…")

        def browse():
            m = ctx.browse_model(ref.model, "models/")
            if m:
                ref.model = m
                ch(True, True)
        br.clicked.connect(browse)
        mrow.addWidget(br)
        f.addRow("Модель:", mrow)
        sk = QSpinBox()
        sk.setRange(0, 32)
        sk.setValue(ref.skin)
        sk.valueChanged.connect(lambda v: (setattr(ref, "skin", v), ch()))
        f.addRow("Скин:", sk)
        if ref.type == "prop":
            ph = QCheckBox("физический (падает, толкается)")
            ph.setChecked(ref.physics)
            ph.toggled.connect(lambda v: (setattr(ref, "physics", bool(v)), ch()))
            f.addRow("", ph)
        lay.addLayout(f)
        if ref.type == "container":
            g, gl = _group("Название в окне лута")
            lt = LangTexts(ctx, ref.display_name, multiline=False, placeholder="например, Шкафчик охранника")
            lt.changed.connect(lambda: ch())
            gl.addWidget(lt)
            lay.addWidget(g)
            g, gl = _group("Содержимое")
            it = ItemsTable(ref.loot_items, ctx.item_names())
            it.changed.connect(ch)
            gl.addWidget(it)
            lay.addWidget(g)
    elif ref.type == "path":
        g, gl = _group("Точки (%d)" % len(ref.points))
        tbl = QTableWidget(len(ref.points), 4)
        tbl.setHorizontalHeaderLabels(["Позиция", "Ждать, с", "Смотреть °", "Анимация"])
        tbl.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        tbl.verticalHeader().setVisible(True)
        tbl.setMaximumHeight(36 + 30 * max(3, len(ref.points)))
        for i, pt in enumerate(ref.points):
            tbl.setItem(i, 0, QTableWidgetItem("%.0f %.0f %.0f" % tuple(pt.pos)))
            ws = QDoubleSpinBox()
            ws.setRange(0, 3600)
            ws.setDecimals(1)
            ws.setValue(pt.wait)
            ws.valueChanged.connect(lambda v, pt=pt: (setattr(pt, "wait", v), ch()))
            tbl.setCellWidget(i, 1, ws)
            ys = QSpinBox()
            ys.setRange(-1, 359)
            ys.setSpecialValueText("—")
            ys.setValue(int(pt.yaw) if pt.yaw is not None else -1)
            ys.valueChanged.connect(lambda v, pt=pt: (setattr(pt, "yaw", None if v < 0 else float(v)), ch()))
            tbl.setCellWidget(i, 2, ys)
            an = combo([("", "—")] + [(x, n) for x, n, _ in wd.ANIMS], pt.anim, editable=True, min_len=6)
            an.activated.connect(lambda _, an=an, pt=pt: (setattr(pt, "anim", an.currentData() or ""), ch()))
            tbl.setCellWidget(i, 3, an)
        gl.addWidget(tbl)
        bl = QHBoxLayout()
        ab = QPushButton("Добавить точки на карте…")
        ab.clicked.connect(lambda: ctx.start_path_draw(ref))
        bl.addWidget(ab)
        rb = QPushButton("− выбранная")

        def rm_pt():
            r = tbl.currentRow()
            if 0 <= r < len(ref.points):
                del ref.points[r]
                ch(True, True)
        rb.clicked.connect(rm_pt)
        bl.addWidget(rb)
        rv = QPushButton("Развернуть")

        def rev():
            ref.points.reverse()
            ch(True, True)
        rv.clicked.connect(rev)
        bl.addWidget(rv)
        bl.addStretch(1)
        gl.addLayout(bl)
        lay.addWidget(g)
        users = [n.targetname for n in p.npcs for pk in n.actor.packages if pk.type == "patrol" and pk.path == ref.name and ref.name]
        lay.addWidget(hint(("Патрулируют: " + ", ".join(sorted(set(users)))) if users else "Пока никто не патрулирует. Пакет «Патруль» в распорядке NPC → выберите этот маршрут."))
    elif ref.type == "entity":
        f = _form()
        cl = QLineEdit(ref.classname)
        cl.setPlaceholderText("classname, например ambient_generic или light_deferred")
        cl.editingFinished.connect(lambda: (setattr(ref, "classname", cl.text().strip()), ch(True)))
        f.addRow("classname:", cl)
        lay.addLayout(f)
        g, gl = _group("Ключи")
        kvt = KVTable(ref.keyvalues)
        kvt.changed.connect(ch)
        gl.addWidget(kvt)
        lay.addWidget(g)
    if ref.type in ("item", "container", "prop", "npc", "marker"):
        g, gl = _group("Дополнительные ключи")
        kvt = KVTable(ref.keyvalues)
        kvt.changed.connect(ch)
        gl.addWidget(kvt)
        lay.addWidget(g)
    bl = QHBoxLayout()
    fb = QPushButton("Показать")
    fb.clicked.connect(lambda: ctx.focus_ref(layer.name, ref.id))
    db = QPushButton("Дублировать")
    db.clicked.connect(lambda: ctx.duplicate_ref(ref))
    xb = QPushButton("Удалить")
    xb.clicked.connect(lambda: ctx.delete_ref(ref))
    for b in (fb, db, xb):
        bl.addWidget(b)
    bl.addStretch(1)
    lay.addLayout(bl)
    note = AutoText(ref.note, 1, 4, "заметка")
    note.edited.connect(lambda tx: (setattr(ref, "note", tx), insp.changed(False)))
    lay.addWidget(note)
    return w, lay


def hammer_page(insp, scene, idx):
    ctx = insp.ctx
    ent = None
    for (i, cls, o, a, e) in scene.entities:
        if i == idx:
            ent = (cls, o, a, e)
    if ent is None:
        return insp._page("Сущность не найдена")
    cls, o, a, e = ent
    tn = e.get("targetname", "")
    w, lay = insp._page("Hammer: %s" % cls, "Стоит в карте (из Hammer) — двигать её можно только в Hammer. "
                                            "Распорядок, фракцию и диалог она получает из NPC проекта с тем же targetname.")
    rows = ["targetname: %s" % (tn or "— нет —"), "позиция: %.0f %.0f %.0f" % tuple(o)]
    for k in ("model", "loot_displayname", "loot_items", "ftag", "squadname", "additionalequipment"):
        if e.get(k):
            rows.append("%s: %s" % (k, e[k]))
    l = QLabel("\n".join(rows))
    l.setTextInteractionFlags(Qt.TextSelectableByMouse)
    l.setWordWrap(True)
    lay.addWidget(l)
    if tn:
        npc = ctx.project.npc(tn)
        if npc is not None:
            b = QPushButton("Открыть NPC проекта «%s»" % tn)
            b.clicked.connect(lambda: ctx.open_npc(tn))
            lay.addWidget(b)
        elif cls.startswith("npc_"):
            b = QPushButton("Добавить в проект (распорядок, диалог, фракция)")
            b.clicked.connect(lambda: ctx.npc_from_hammer(tn, cls, e))
            lay.addWidget(b)
    else:
        lay.addWidget(hint("Без targetname игра не сможет дать ему распорядок или диалог — задайте имя в Hammer."))
    return w, lay


def nav_page(insp, layer, i):
    w, lay = insp._page("Узел навигации #%d" % i, "Узлы — точки, по которым NPC прокладывают путь (info_node). "
                                                   "Граф игра строит сама при загрузке карты после изменения узлов.")
    if 0 <= i < len(layer.nav):
        lay.addWidget(QLabel("%.0f %.0f %.0f" % tuple(layer.nav[i])))
        b = QPushButton("Удалить узел")
        b.clicked.connect(lambda: insp.ctx.delete_nav(i))
        lay.addWidget(b)
    lay.addWidget(hint("Всего узлов на карте: %d. Режим правки узлов: «Навигация → Править узлы» над картой — "
                       "клик добавляет узел, перетаскивание двигает, Delete удаляет." % len(layer.nav)))
    return w, lay


def faction_page(insp, fac):
    ctx = insp.ctx
    p = ctx.project
    w, lay = insp._page("Фракция  %s" % fac.id, "Фракция = ftag NPC. Отношения к игроку и между фракциями применяются в игре "
                                                "при появлении NPC; ранить одного — разозлить всю фракцию (если включено у NPC).")

    def ch(rebuild=False):
        insp.changed(True)
        ctx.world_changed("faction")
        if rebuild:
            insp.show_target(insp.target)
    f = _form()
    idl = QLineEdit(fac.id)

    def rename():
        new = idl.text().strip()
        if new == fac.id:
            return
        if not new or not TARGETNAME_RE.match(new) or p.faction(new):
            QMessageBox.warning(w, "Фракция", "Пусто, недопустимые символы или уже есть")
            idl.setText(fac.id)
            return
        ctx.rename_faction(fac, new)
    idl.editingFinished.connect(rename)
    f.addRow("id (ftag):", idl)
    pl = combo(wd.RELATIONS, fac.player)
    pl.currentIndexChanged.connect(lambda _: (setattr(fac, "player", pl.currentData()), ch()))
    f.addRow("К игроку:", pl)
    lay.addLayout(f)
    g, gl = _group("Название")
    nm = LangTexts(ctx, fac.name, multiline=False, placeholder="например, Повстанцы")
    nm.changed.connect(lambda: insp.changed(False))
    gl.addWidget(nm)
    lay.addWidget(g)
    g, gl = _group("К другим фракциям")
    f = _form()
    for other in p.factions:
        if other is fac:
            continue
        c = combo(wd.RELATIONS, fac.relations.get(other.id, ""))
        c.currentIndexChanged.connect(lambda _, c=c, o=other.id: (fac.relations.__setitem__(o, c.currentData()) if c.currentData() else fac.relations.pop(o, None), ch()))
        f.addRow(other.id + ":", c)
    gl.addLayout(f)
    if len(p.factions) < 2:
        gl.addWidget(hint("Других фракций пока нет."))
    lay.addWidget(g)
    members = sorted(n.targetname for n in p.npcs if n.actor.faction == fac.id)
    lay.addWidget(hint(("Члены: " + ", ".join(members)) if members else "Пока ни у одного NPC нет этой фракции (вкладка «Персонаж»)."))
    de = QPushButton("Удалить фракцию")
    de.clicked.connect(lambda: ctx.delete_faction(fac))
    lay.addWidget(de)
    return w, lay
