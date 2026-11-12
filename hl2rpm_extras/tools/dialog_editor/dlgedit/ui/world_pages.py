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
from .logic_widgets import LogicList


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


def bind_combo(cb, apply):
    """Editable combo: call apply() only when its text really changed. Qt emits
    lineEdit().editingFinished as soon as the arrow is clicked (the list is already open):
    reacting to it rebuilt the page and closed the list, marked the project changed and
    took an undo snapshot."""
    last = [cb.currentText()]

    def fire(*_):
        if cb.currentText() == last[0]:
            return
        last[0] = cb.currentText()
        apply()
    cb.activated.connect(fire)
    if cb.lineEdit() is not None:
        cb.lineEdit().editingFinished.connect(fire)


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
        insp.rebuild_soon()
    bind_combo(cls, set_cls)
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
    bind_combo(mdl, set_model)
    mrow.addWidget(mdl, 1)
    br = QToolButton(text="…")
    br.setToolTip("Выбрать модель из игры")

    def browse():
        m = ctx.browse_model(a.model or wd.NPC_DEFAULT_MODEL.get(a.classname, ""), "models/")
        if m:
            a.model = m
            ch(True)
            insp.rebuild_soon()
    br.clicked.connect(browse)
    mrow.addWidget(br)
    va = QToolButton(text="Анимации…")
    va.setToolTip("Модель с анимациями (свои и из подключённых моделей): что сейчас делает NPC по распорядку, "
                  "анимации маркеров, походки")
    va.clicked.connect(lambda: hasattr(ctx, "view_model") and ctx.view_model(a.model_or_default(), a.skin,
                                                                             ctx.npc_preview_sequence(npc) if npc else "ACT_IDLE",
                                                                             "Просмотр: %s" % npc.targetname if npc else ""))
    f.addRow("Модель:", mrow)
    f.addRow("", va)
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
    bind_combo(wp, set_wp)
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
    bind_combo(fac, set_fac)
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
    it = ItemsTable(a.loot_items, ctx.all_item_ids() + ["list:" + x.id for x in p.lootlists], title="Предмет или list:<список лута>")
    it.changed.connect(ch)
    gl.addWidget(it)
    lay.addWidget(g)

    # combat
    g, gl = _group("Бой")
    cf = _form()
    pr = combo([(-1, "по классу NPC"), (0, "плохая"), (1, "средняя"), (2, "хорошая"), (3, "очень хорошая"), (4, "идеальная")], a.proficiency)
    pr.currentIndexChanged.connect(lambda _: (setattr(a, "proficiency", int(pr.currentData())), ch()))
    cf.addRow("Меткость:", pr)
    ag = combo([(-1, "по классу и фракции"), (0, "не нападает первым"), (1, "нападает на врагов"),
                (2, "нападает на всех, кроме друзей"), (3, "бешеный — на всех, и на своих")], a.aggression)
    ag.setToolTip("Как в New Vegas. «Не нападает первым» — пока его (или игрок) не тронут; «на всех, кроме друзей» — "
                  "игрок враг, если фракция ему не друг")
    ag.currentIndexChanged.connect(lambda _: (setattr(a, "aggression", int(ag.currentData())), ch()))
    cf.addRow("Агрессия:", ag)
    cn = combo([(-1, "по классу"), (0, "трус — убегает сразу"), (1, "осторожный — убегает при 50 % здоровья"),
                (2, "обычный — убегает при 25 %"), (3, "храбрый — не убегает")], a.confidence)
    cn.setToolTip("Как в New Vegas: при каком здоровье NPC бросает бой и убегает от врага")
    cn.currentIndexChanged.connect(lambda _: (setattr(a, "confidence", int(cn.currentData())), ch()))
    cf.addRow("Смелость:", cn)
    xp = QSpinBox()
    xp.setRange(0, 100000)
    xp.setSpecialValueText("нет")
    xp.setValue(a.xp)
    xp.valueChanged.connect(lambda v: (setattr(a, "xp", int(v)), ch()))
    cf.addRow("Опыт за убийство:", xp)
    gl.addLayout(cf)
    lay.addWidget(g)

    # behaviour (rules of the game by default: РПГ → правила «Поведение NPC»)
    g, gl = _group("Поведение")
    bf = _form()
    yl = combo([(-1, "по правилам игры (по умолчанию — нет)"), (0, "нет — стоит на месте"), (1, "да — отходит, как в HL2")],
               a.yield_player)
    yl.setToolTip("HL2: горожане отходят, когда игрок идёт на них. Торговцу и собеседнику лучше «нет».\n"
                  "Общее значение — правило npc_yield (вкладка «РПГ»)")
    yl.currentIndexChanged.connect(lambda _: (setattr(a, "yield_player", int(yl.currentData())), ch()))
    bf.addRow("Уступает дорогу игроку:", yl)
    pw = combo([(-1, "по правилам игры (по умолчанию — нет)"), (0, "нет"), (1, "да — любое оружие на полу"),
                (2, "только в отряде игрока")], a.pickup_weapons)
    pw.setToolTip("HL2: горожане подбирают любое оружие с пола. Общее значение — правило npc_pickup_weapons")
    pw.currentIndexChanged.connect(lambda _: (setattr(a, "pickup_weapons", int(pw.currentData())), ch()))
    bf.addRow("Подбирает оружие:", pw)
    wl = combo([(-1, "по классу (ГО и солдаты следят, горожане — гражданские)"), (0, "не касается"),
                (1, "гражданский — оружие носить нельзя"), (2, "следит: предупреждает, потом стреляет"),
                (3, "следит: стреляет сразу")], a.weapon_law)
    wl.setToolTip("Запрет оружия в Городе. Тот, кто следит, видит у гражданского (игрока или NPC) оружие в руках —\n"
                  "«Убрать оружие!», последнее предупреждение, огонь. Убрал (H) — «Проходи». Повтор в течение минуты — огонь сразу.\n"
                  "Игрок в форме ГО / солдата — свой. Фразы — событие «Запрет оружия» во «Фразах»; без них — фразы HL2.\n"
                  "Общие настройки — правила weapon_law* (вкладка «РПГ»)")
    wl.currentIndexChanged.connect(lambda _: (setattr(a, "weapon_law", int(wl.currentData())), ch()))
    bf.addRow("Запрет оружия:", wl)
    gl.addLayout(bf)
    lay.addWidget(g)

    # trade
    g, gl = _group("Торговец")
    vend = QCheckBox("Торгует")
    vend.setToolTip("Окно торговли открывает вариант ответа с флажком «Открыть торговлю» в диалоге этого NPC")
    vend.setChecked(a.vendor is not None)
    gl.addWidget(vend)
    vbox = QWidget()
    vf = QFormLayout(vbox)
    vf.setContentsMargins(0, 0, 0, 0)
    vd = a.vendor or {}

    def vset(key, value):
        if a.vendor is None:
            return
        if a.vendor.get(key) != value:
            a.vendor[key] = value
            ch()
    lists = ["list:" + x.id for x in p.lootlists]
    goods = QComboBox()
    goods.setEditable(True)
    goods.addItems(lists)
    goods.setCurrentText(vd.get("list", lists[0] if lists else ""))
    goods.setToolTip("Список лута (list:<id>) и/или предметы через «;» — например list:trader_food;item_healthkit:2")
    goods.lineEdit().editingFinished.connect(lambda: vset("list", goods.currentText().strip()))
    goods.activated.connect(lambda _: vset("list", goods.currentText().strip()))
    vf.addRow("Товары:", goods)
    money = QSpinBox()
    money.setRange(0, 1000000)
    money.setValue(int(vd.get("money", 300)))
    money.valueChanged.connect(lambda v: vset("money", int(v)))
    vf.addRow("Деньги торговца:", money)
    mk = QDoubleSpinBox()
    mk.setRange(0.5, 10.0)
    mk.setSingleStep(0.05)
    mk.setValue(float(vd.get("markup", 1.25)))
    mk.setSuffix(" × цены")
    mk.valueChanged.connect(lambda v: vset("markup", round(float(v), 2)))
    vf.addRow("Продаёт по:", mk)
    pays = QSpinBox()
    pays.setRange(1, 100)
    pays.setSuffix(" % цены")
    pays.setValue(int(round(float(vd.get("pays", 0.5)) * 100)))
    pays.valueChanged.connect(lambda v: vset("pays", round(v / 100.0, 2)))
    vf.addRow("Покупает по:", pays)
    rs = QSpinBox()
    rs.setRange(0, 24 * 60)
    rs.setSpecialValueText("никогда")
    rs.setSuffix(" ч")
    rs.setValue(int(float(vd.get("restock_hours", 72))))
    rs.valueChanged.connect(lambda v: vset("restock_hours", int(v)))
    vf.addRow("Новый товар через:", rs)
    curs = [x.id for x in p.items if x.category == "currency"] or ["rpm_credits"]
    cur = combo([(c, c) for c in curs], vd.get("currency", curs[0]), editable=True)
    cur.currentTextChanged.connect(lambda t: vset("currency", t.strip()))
    vf.addRow("Валюта:", cur)
    gl.addWidget(vbox)
    vbox.setVisible(a.vendor is not None)

    def set_vendor(on):
        if on and a.vendor is None:
            a.vendor = {"list": goods.currentText().strip(), "money": money.value(), "markup": round(mk.value(), 2),
                        "pays": round(pays.value() / 100.0, 2), "restock_hours": rs.value(), "currency": cur.currentText().strip()}
        elif not on:
            a.vendor = None
        vbox.setVisible(bool(on))
        ch()
    vend.toggled.connect(set_vendor)
    gl.addWidget(hint("Окно торговли открывает вариант ответа с флажком «Открыть торговлю» в его диалоге. Цены — поле «Цена» "
                      "предметов (вкладка «Предметы»), деньги игрока — предмет-валюта в инвентаре; запас и деньги торговца "
                      "сохраняются в игре и обновляются раз в указанное время."))
    lay.addWidget(g)

    # template and variation
    g, gl = _group("Шаблон и вариации")
    gl.addWidget(hint("Шаблон — другой NPC проекта: пустые поля этого NPC берутся из него (класс, модель, оружие, фракция, "
                      "лут, распорядок…). Вариации — случайный выбор для каждого появившегося NPC: так один шаблон "
                      "«Горожанин» даёт толпу разных людей (точки появления, массовка)."))
    tf = _form()
    others = [("", "— нет —")] + [(n.targetname, n.targetname + ("  (шаблон)" if n.actor.is_template else ""))
                                   for n in sorted(p.npcs, key=lambda x: x.targetname.lower()) if n is not npc]
    tc = combo(others, a.template)
    tc.currentIndexChanged.connect(lambda _: (setattr(a, "template", tc.currentData() or ""), ch(True)))
    tf.addRow("Шаблон:", tc)
    itc = QCheckBox("Это шаблон")
    itc.setToolTip("Основа для других NPC и точек появления")
    itc.setChecked(a.is_template)
    itc.toggled.connect(lambda v: (setattr(a, "is_template", bool(v)), ch(True)))
    tf.addRow("", itc)
    gl.addLayout(tf)

    def list_edit(title, values, attr, browse=None, placeholder=""):
        box = QWidget()
        bl = QVBoxLayout(box)
        bl.setContentsMargins(0, 0, 0, 0)
        bl.addWidget(QLabel(title))
        ed = QLineEdit("; ".join(values))
        ed.setPlaceholderText(placeholder)

        def apply():
            vals = [x.strip() for x in ed.text().split(";")]
            if attr != "weapons":
                vals = [x for x in vals if x]
            elif not any(vals):
                vals = []
            if vals != getattr(a, attr):
                setattr(a, attr, vals)
                ch()
        ed.editingFinished.connect(apply)
        row = QHBoxLayout()
        row.addWidget(ed, 1)
        if browse:
            bt = QToolButton(text="+")
            bt.setToolTip("Добавить модель из игры")

            def add():
                m = ctx.browse_model("", "models/")
                if m:
                    cur = [x.strip() for x in ed.text().split(";") if x.strip()]
                    cur.append(m)
                    ed.setText("; ".join(cur))
                    apply()
            bt.clicked.connect(add)
            row.addWidget(bt)
        bl.addLayout(row)
        return box
    inh = a.resolved(p) if a.template else None

    def from_tpl(vals, dflt):
        return ("из шаблона: " + "; ".join(v or "—" for v in vals)) if inh is not None and vals else dflt
    gl.addWidget(list_edit("Модели (случайная для каждого):", a.models, "models", browse=True,
                           placeholder=from_tpl(inh.models if inh else [], "models/humans/group01/male_01.mdl; models/humans/group01/female_02.mdl")))
    gl.addWidget(list_edit("Оружие (случайное; пусто между ; — без оружия):", a.weapons, "weapons",
                           placeholder=from_tpl(inh.weapons if inh else [], "weapon_pistol; weapon_smg1; ")))
    skr = QHBoxLayout()
    skr.addWidget(QLabel("Скин случайно до:"))
    sm = QSpinBox()
    sm.setRange(0, 32)
    sm.setSpecialValueText("—")
    sm.setValue(a.skin_max)
    sm.valueChanged.connect(lambda v: (setattr(a, "skin_max", int(v)), ch()))
    skr.addWidget(sm)
    skr.addStretch(1)
    gl.addLayout(skr)
    gl.addWidget(hint("Случайные имена — по строке на имя, перевод через « | » (например: Иван | Ivan). "
                      "Женские берутся для женских моделей (female в имени файла), мужские — для остальных."))
    langs = p.languages

    def names_edit(title, attr):
        gl.addWidget(QLabel(title))
        vals = getattr(a, attr)
        ed = AutoText("\n".join(" | ".join(n.get(lg, "") for lg in langs).rstrip(" |") for n in vals), 2, 8,
                      from_tpl([n.get(langs[0], "") for n in getattr(inh, attr)] if inh else [],
                               "имя на %s | на %s" % tuple((langs + langs)[:2])))

        def set_names(text):
            out = []
            for line in text.splitlines():
                parts = [x.strip() for x in line.split("|")]
                if not any(parts):
                    continue
                out.append({lg: parts[i] for i, lg in enumerate(langs) if i < len(parts) and parts[i]})
            if out != getattr(a, attr):
                setattr(a, attr, out)
                ch()
        ed.edited.connect(set_names)
        gl.addWidget(ed)
    names_edit("Мужские имена (и для любых моделей):", "names")
    names_edit("Женские имена:", "names_female")
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
    tl.picked.connect(lambda i: (setattr(insp, "pkg_sel", i), insp.rebuild_soon()))
    tl.hourPicked.connect(lambda h: (ctx.set_preview_hour(h), insp.rebuild_soon()))
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
    lst.currentRowChanged.connect(lambda i: (setattr(insp, "pkg_sel", i), insp.rebuild_soon()) if i != insp.pkg_sel else None)
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
        insp.rebuild_soon()
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
                insp.rebuild_soon()
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
            insp.rebuild_soon()
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
            insp.rebuild_soon()
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
            insp.rebuild_soon()
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
            bind_combo(rc, set_ref)
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
        bind_combo(pc, set_path)
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
        bind_combo(tc, set_tg)
        f.addRow("За кем:", tc)
    if t in ("wait", "travel", "guard", "sleep"):
        anims = [("", "— без анимации —" if t != "sleep" else "— на земле: сидя —")] + [(x, "%s  (%s)" % (n, x)) for x, n, _ in wd.ANIMS]
        an = combo(anims, pk.anim, editable=True)

        def set_anim(*_):
            v = an.currentData() if an.currentIndex() >= 0 and an.currentText() == an.itemText(an.currentIndex()) else an.currentText().strip()
            pk.anim = v or ""
            ch(False, True)
        bind_combo(an, set_anim)
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
    cl.addWidget(QLabel("Ещё условия (флаги, погода, предметы…):"))
    pkl = LogicList(ctx, pk.conditions, max_height=100)
    pkl.changed.connect(lambda: ch())
    cl.addWidget(pkl)
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
    if not ok and hasattr(ctx, "marker_sequence"):
        # an actbusy entry (the library's rpm_sit_chair, rpm_lie_bed...): its busy sequence
        seq = ctx.marker_sequence(anim)
        ok = bool(seq) and seq.lower() in names
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
            insp.rebuild_soon()
    f = _form()
    if ref.type in ("marker", "path", "item", "container", "prop", "entity", "spawner", "transition", "zone", "sound", "terminal"):
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
        bind_combo(an, set_anim)
        f.addRow("Анимация:", an)
        own = combo([("", "— любой NPC —")] + [(n.targetname, n.targetname) for n in sorted(p.npcs, key=lambda x: x.targetname)], ref.owner)
        own.currentIndexChanged.connect(lambda _: (setattr(ref, "owner", own.currentData()), ch(True)))
        f.addRow("Владелец:", own)
        # sitting animations of the library: the seat height and the fit to the furniture
        sp = ctx.seat_params().get((ref.anim or "").lower()) or {}
        if sp.get("wall"):
            wall = QPushButton("Прислонить к стене")
            wall.setToolTip("Найти ближайшую стену, поставить маркер в %.0f ед. перед ней спиной к ней и опустить на пол"
                            % sp.get("wall_dist", 14))

            def do_wall():
                ok, msg = ctx.fit_marker_to_wall(ref)
                wall.setToolTip(msg)
                if ok:
                    ch(True, True)
                else:
                    from PySide6.QtWidgets import QMessageBox
                    QMessageBox.information(wall, "Прислонить к стене", msg)
            wall.clicked.connect(do_wall)
            f.addRow("Стена:", wall)
        elif sp.get("bed"):
            brow = QHBoxLayout()
            bh = QDoubleSpinBox()
            bh.setRange(-1.0, 40.0)
            bh.setDecimals(1)
            bh.setSpecialValueText("не задана (17)")
            bh.setValue(ref.seat_h)
            bh.setToolTip("Высота верха матраса над полом: анимация сама подстраивается (параметр позы rpm_seat_h)")
            bh.valueChanged.connect(lambda v: (setattr(ref, "seat_h", float(v)), ch(False, True)))
            brow.addWidget(bh, 1)
            bfit = QPushButton("Подогнать к кровати")
            bfit.setToolTip("Найти кровать (и матрас на ней), поставить маркер у длинной стороны: NPC сядет на край "
                            "и ляжет головой к изголовью; записать высоту матраса")

            def do_bed():
                ok, msg = ctx.fit_marker_to_bed(ref)
                bfit.setToolTip(msg)
                if ok:
                    ch(True, True)
                from PySide6.QtWidgets import QMessageBox
                QMessageBox.information(bfit, "Подогнать к кровати", msg)
            bfit.clicked.connect(do_bed)
            brow.addWidget(bfit)
            f.addRow("Высота матраса:", brow)
        elif sp.get("ground"):
            gr = QPushButton("Поставить на пол")
            gr.setToolTip("Маркер — где NPC стоит перед тем, как сесть; таз окажется в %.0f ед. позади, ноги — до 30 ед. "
                          "впереди: место должно быть свободно" % sp.get("pelvis_back", 9))
            gr.clicked.connect(lambda: (setattr(ref, "pos", ctx.scene.drop_to_floor(ref.pos)) if ctx.scene is not None else None,
                                        ch(True, True)))
            f.addRow("Пол:", gr)
        elif "over_seat" in sp:
            srow = QHBoxLayout()
            sh = QDoubleSpinBox()
            sh.setRange(-1.0, 40.0)
            sh.setDecimals(1)
            sh.setSpecialValueText("не задана (17)")
            sh.setValue(ref.seat_h)
            sh.setToolTip("Высота сиденья над полом: анимация сама подстраивается под неё (параметр позы rpm_seat_h)")
            sh.valueChanged.connect(lambda v: (setattr(ref, "seat_h", float(v)), ch(False, True)))
            srow.addWidget(sh, 1)
            fit = QPushButton("Подогнать к мебели")
            fit.setToolTip("Найти ближайший стул / диван / скамью, поставить маркер так, чтобы NPC сел ровно на сиденье, "
                           "и записать высоту сиденья")

            def do_fit():
                ok, msg = ctx.fit_marker_to_furniture(ref)
                fit.setToolTip(msg)
                if ok:
                    ch(True, True)
                else:
                    from PySide6.QtWidgets import QMessageBox
                    QMessageBox.information(fit, "Подогнать к мебели", msg)
            fit.clicked.connect(do_fit)
            srow.addWidget(fit)
            f.addRow("Высота сиденья:", srow)
        lay.addLayout(f)
        lay.addWidget(hint("NPC встаёт в точку маркера лицом по стрелке и играет анимацию. У анимаций библиотеки (rpm_…) кнопка "
                           "выше ставит маркер сама: к стулу и дивану, к кровати (головой к изголовью), к стене. Проверить в игре — "
                           "F8 и rpm_marker_use <NPC> <маркер>."))
        if ref.anim and ref.owner and p.npc(ref.owner):
            lay.addWidget(anim_check(ctx, p.npc(ref.owner), ref.anim))
        users = [n.targetname for n in p.npcs for pk in n.actor.packages if pk.ref == ref.name and ref.name]
        if users:
            lay.addWidget(hint("Используют пакеты: " + ", ".join(sorted(set(users)))))
    elif ref.type == "item":
        f = _form()
        ic = combo([(x, x) for x in ctx.all_item_ids()], ref.classname, editable=True)

        def set_cls(*_):
            ref.classname = ic.currentText().strip()
            ch(True)
        bind_combo(ic, set_cls)
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
        bind_combo(mc, set_model)
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
            names = ctx.all_item_ids() + ["list:" + x.id for x in p.lootlists]
            it = ItemsTable(ref.loot_items, names, title="Предмет или list:<список лута>")
            it.changed.connect(ch)
            gl.addWidget(it)
            gl.addWidget(hint("list:<id> — список лута (вкладка «Предметы»): разыгрывается, когда игрок впервые откроет контейнер."))
            lay.addWidget(g)
            # lock and restock (prop_loot_container keys)
            g, gl = _group("Замок и пополнение")
            lf = _form()
            keys = [("", "— без ключа —")] + [(x.id, "%s  (%s)" % (x.label(ctx.display_lang, p.base_language), x.id))
                                              for x in p.items if x.category == "key" or x.base == "key"]
            kc = combo(keys, ref.keyvalues.get("lock_key", ""), editable=True)

            def set_key():
                v = kc.currentData() if kc.currentIndex() >= 0 and kc.currentText() == kc.itemText(kc.currentIndex()) else kc.currentText().strip()
                if v:
                    ref.keyvalues["lock_key"] = v
                else:
                    ref.keyvalues.pop("lock_key", None)
                ch()
            bind_combo(kc, set_key)
            lf.addRow("Ключ:", kc)
            lv = QSpinBox()
            lv.setRange(0, 100)
            lv.setSpecialValueText("не взломать")
            lv.setValue(int(ref.keyvalues.get("lock_level", 0) or 0))
            lv.valueChanged.connect(lambda v: (ref.keyvalues.__setitem__("lock_level", str(v)) if v else ref.keyvalues.pop("lock_level", None), ch()))
            lf.addRow("Взлом (навык):", lv)
            rs = QSpinBox()
            rs.setRange(0, 24 * 60)
            rs.setSpecialValueText("не пополняется")
            rs.setSuffix(" ч")
            rs.setValue(int(float(ref.keyvalues.get("restock_hours", 0) or 0)))
            rs.valueChanged.connect(lambda v: (ref.keyvalues.__setitem__("restock_hours", str(v)) if v else ref.keyvalues.pop("restock_hours", None), ch()))
            lf.addRow("Пополнение через:", rs)
            owners = [("", "— ничьё —")] + [(n.targetname, "NPC: %s" % n.targetname) for n in sorted(p.npcs, key=lambda x: x.targetname.lower())
                                            if not n.actor.is_template] + [(f.id, "Фракция: %s" % f.id) for f in p.factions]
            oc = combo(owners, ref.keyvalues.get("owner", ""), editable=True)
            oc.setToolTip("Чужое: под прицелом — красное «Украсть»; если взятие видит владелец или член его фракции, "
                          "он говорит фразу «кража» и нападает, а репутация у фракции падает (правило rep_theft)")

            def set_owner():
                v = oc.currentData() if oc.currentIndex() >= 0 and oc.currentText() == oc.itemText(oc.currentIndex()) else oc.currentText().strip()
                if v:
                    ref.keyvalues["owner"] = v
                else:
                    ref.keyvalues.pop("owner", None)
                ch()
            bind_combo(oc, set_owner)
            lf.addRow("Владелец:", oc)
            gl.addLayout(lf)
            gl.addWidget(hint("Заперт, если задан ключ или уровень взлома. Игрок с ключом в инвентаре открывает его, без ключа — "
                              "только навыком «Взлом» не ниже уровня. Пополнение: через столько игровых часов содержимое "
                              "возвращается к исходному (списки разыгрываются снова)."))
            lay.addWidget(g)
    elif ref.type == "terminal":
        from .. import termdata as td
        f = _form()
        lang = ctx.display_lang
        terms = [("", "— не выбран —")] + [(t.id, "%s — %s" % (t.id, t.label(lang, p.base_language))) for t in p.terminals]
        tc = combo(terms, ref.terminal, editable=False)

        def set_term(*_):
            ref.terminal = tc.currentData() or ""
            ch(True)
        bind_combo(tc, set_term)
        trow = QHBoxLayout()
        trow.addWidget(tc, 1)
        bn = QToolButton(text="+")
        bn.setToolTip("Новый терминал для этого объекта")

        def new_t():
            from .term_pages import new_terminal
            style = next((s for m, s, _n in td.MODELS if m == ref.model), "pc")
            t = new_terminal(ctx, style)
            if t:
                ref.terminal = t.id
                ch(True)
        bn.clicked.connect(new_t)
        trow.addWidget(bn)
        bo = QToolButton(text="Открыть")
        bo.setToolTip("Записи, взлом, команды этого терминала (вкладка «Логика»)")

        def open_t():
            if ref.terminal and p.terminal(ref.terminal):
                ctx.left_tabs.setCurrentWidget(ctx.logic_tab)
                ctx.inspector.show_target(("terminal", ref.terminal))
        bo.clicked.connect(open_t)
        trow.addWidget(bo)
        f.addRow("Терминал:", trow)
        mc = combo([(m, "%s  (%s)" % (n, m.rsplit("/", 1)[-1])) for m, s, n in td.MODELS], ref.model, editable=True, min_len=16)

        def set_model(*_):
            v = mc.currentData() if mc.currentIndex() >= 0 and mc.currentText() == mc.itemText(mc.currentIndex()) else mc.currentText().strip()
            ref.model = v
            ch(True)
        bind_combo(mc, set_model)
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
        lay.addLayout(f)
        lay.addWidget(hint("Экраны стандартных моделей игра знает сама. Для своей модели задайте экран в «Дополнительных ключах»: "
                           "screen_pos «x y z» (центр экрана в координатах модели), screen_ang «pitch yaw 0» (куда смотрит экран), "
                           "screen_size «ширина высота». Без них экран — передняя грань модели (+X). Вид (компьютер / Альянс) "
                           "берётся из терминала; ключ style переопределяет."))
    elif ref.type == "transition":
        g, gl = _group("Куда ведёт")
        gl.addWidget(hint("Игрок входит в коробку — загружается другая карта, и он появляется перед парным переходом там "
                          "(по стрелке, лицом по ней). Поверните переход, чтобы задать, куда смотрит пришедший сюда. "
                          "Инвентарь, квесты, время и погода переносятся. Пересобирать карты не нужно."))
        tf = _form()
        maps = sorted(n for n in ctx.map_files() if n != layer.name)
        mc = combo([("", "— карта —")] + [(n, n) for n in maps], ref.target_map, editable=False)
        tf.addRow("Карта:", mc)

        def pairs_of(mapname):
            tl = p.layer(mapname) if mapname else None
            return [r for r in (tl.refs if tl else []) if r.type == "transition"]
        pc = combo([("", "— нет —")] + [(r.name, "%s  (→ %s)" % (r.name, r.target_map or "?")) for r in pairs_of(ref.target_map)],
                   ref.target)
        tf.addRow("Парный переход:", pc)

        def set_map(_):
            v = mc.currentData() or ""
            if v != ref.target_map:
                ref.target_map = v
                ref.target = ""
                ch(rebuild=True)
        mc.currentIndexChanged.connect(set_map)

        def set_pair(_):
            v = pc.currentData() or ""
            if v != ref.target:
                ref.target = v
                pr = p.layer(ref.target_map).ref_by_name(v) if v and p.layer(ref.target_map) else None
                if pr is not None and not pr.target:
                    pr.target_map, pr.target = layer.name, ref.name
                ch()
        pc.currentIndexChanged.connect(set_pair)
        gl.addLayout(tf)
        brow = QHBoxLayout()
        mk = QPushButton("Создать парный там")
        mk.setToolTip("Новый переход на выбранной карте, ведущий обратно сюда; потом передвиньте его на той карте")
        mk.setEnabled(bool(ref.target_map) and not ref.target)

        def make_pair():
            tl = p.layer(ref.target_map, create=True)
            others = [r for r in tl.refs if r.type != "transition"]
            pos = list(others[0].pos) if others else [0.0, 0.0, 0.0]
            nr = wd.Ref(tl.new_id(), "transition", pos)
            nr.name = ctx._unique_name("tr_%s_%d" % (ref.target_map, nr.id))
            nr.target_map, nr.target = layer.name, ref.name
            nr.keyvalues["_unplaced"] = "1"
            tl.refs.append(nr)
            ref.target = nr.name
            ch(True, rebuild=True)
        mk.clicked.connect(make_pair)
        brow.addWidget(mk)
        go = QPushButton("Открыть ту карту")
        go.setEnabled(bool(ref.target_map and ref.target))

        def goto_pair():
            tl = p.layer(ref.target_map)
            pr = tl.ref_by_name(ref.target) if tl else None
            if pr is None:
                return
            ctx.open_map(ref.target_map)
            if ctx.cur_map == ref.target_map and pr.keyvalues.pop("_unplaced", None) and ctx.scene is not None:
                o, _a = ctx.scene.player_start()
                pr.pos = [float(o[0]) + 96.0, float(o[1]), float(o[2])]
                ctx.model_changed(False, moved=True)
            ctx.focus_ref(ref.target_map, pr.id)
        go.clicked.connect(goto_pair)
        brow.addWidget(go)
        brow.addStretch(1)
        gl.addLayout(brow)
        lay.addWidget(g)
        g, gl = _group("Размер и прибытие")
        sf = _form()
        srow = QHBoxLayout()
        for i, tip in enumerate(("ширина (X)", "глубина (Y)", "высота")):
            sp = QSpinBox()
            sp.setRange(16, 2048)
            sp.setSingleStep(8)
            sp.setToolTip(tip)
            sp.setValue(int(ref.size[i]))
            sp.valueChanged.connect(lambda v, i=i: (ref.size.__setitem__(i, float(v)), ch(), ctx.worldview.invalidate_overlay()))
            srow.addWidget(sp)
        sf.addRow("Коробка, ед.:", srow)
        ad = QSpinBox()
        ad.setRange(0, 1024)
        ad.setSingleStep(8)
        ad.setSuffix(" ед. перед ним")
        ad.setValue(int(ref.arrive_dist))
        ad.valueChanged.connect(lambda v: (setattr(ref, "arrive_dist", float(v)), ch(), ctx.worldview.invalidate_overlay()))
        sf.addRow("Пришедший появляется:", ad)
        gl.addLayout(sf)
        gl.addWidget(hint("Точка прибытия должна быть свободной (не в стене и не внутри коробки этого перехода)."))
        lay.addWidget(g)
    elif ref.type == "spawner":
        kvs = ref.keyvalues
        g, gl = _group("Кого создаёт")
        gl.addWidget(hint("NPC проекта (лучше шаблоны с вариациями) или классы игры. Вес — как часто выбирается."))
        entries = []
        for part in [x for x in kvs.get("npcs", "").split(";") if x.strip()]:
            nm_, _, wt = part.partition(":")
            entries.append([nm_.strip(), int(wt) if wt.strip().isdigit() else 1])
        npc_names = [n.targetname for n in sorted(p.npcs, key=lambda x: (not x.actor.is_template, x.targetname.lower()))]
        npc_names += ["npc_zombie", "npc_fastzombie", "npc_poisonzombie", "npc_zombine", "npc_headcrab", "npc_headcrab_fast",
                      "npc_antlion", "npc_metropolice", "npc_combine_s", "npc_citizen", "npc_manhack", "npc_cscanner"]
        tbl = QTableWidget(len(entries), 2)
        tbl.setHorizontalHeaderLabels(["NPC / класс", "Вес"])
        tbl.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        tbl.verticalHeader().setVisible(False)

        def sync_npcs(*_):
            out = []
            for rr in range(tbl.rowCount()):
                n_ = tbl.cellWidget(rr, 0).currentText().strip()
                w_ = tbl.cellWidget(rr, 1).value()
                if n_:
                    out.append("%s:%d" % (n_, w_) if w_ != 1 else n_)
            kvs["npcs"] = ";".join(out)
            ch()
        for rr, (n_, w_) in enumerate(entries):
            cb = QComboBox()
            cb.setEditable(True)
            cb.addItems(npc_names)
            cb.setCurrentText(n_)
            cb.currentTextChanged.connect(sync_npcs)
            tbl.setCellWidget(rr, 0, cb)
            sp = QSpinBox()
            sp.setRange(1, 100)
            sp.setValue(w_)
            sp.valueChanged.connect(sync_npcs)
            tbl.setCellWidget(rr, 1, sp)
        tbl.setMaximumHeight(40 + 30 * max(2, len(entries)))
        gl.addWidget(tbl)
        brow = QHBoxLayout()
        ab = QPushButton("+ NPC")

        def add_npc():
            cur = [x for x in kvs.get("npcs", "").split(";") if x.strip()]
            cur.append(npc_names[0] if npc_names else "npc_zombie")
            kvs["npcs"] = ";".join(cur)
            ch()
            insp.rebuild_soon()
        ab.clicked.connect(add_npc)
        brow.addWidget(ab)
        db = QPushButton("− выбранный")

        def del_npc():
            r_ = tbl.currentRow()
            cur = [x for x in kvs.get("npcs", "").split(";") if x.strip()]
            if 0 <= r_ < len(cur):
                del cur[r_]
                kvs["npcs"] = ";".join(cur)
                ch()
                insp.rebuild_soon()
        db.clicked.connect(del_npc)
        brow.addWidget(db)
        brow.addStretch(1)
        gl.addLayout(brow)
        lay.addWidget(g)
        g, gl = _group("Когда и сколько")
        sf = _form()

        def ispin(key, lo, hi, dflt, special=None, suffix=""):
            sp = QSpinBox()
            sp.setRange(lo, hi)
            if special:
                sp.setSpecialValueText(special)
            sp.setSuffix(suffix)
            try:
                sp.setValue(int(float(kvs.get(key, dflt))))
            except ValueError:
                sp.setValue(dflt)
            sp.valueChanged.connect(lambda v: (kvs.__setitem__(key, str(v)), ch(), ctx.worldview.invalidate_overlay()))
            return sp
        crow = QHBoxLayout()
        crow.addWidget(ispin("count_min", 0, 64, 1))
        crow.addWidget(QLabel("–"))
        crow.addWidget(ispin("count_max", 1, 64, 3))
        crow.addStretch(1)
        sf.addRow("Сколько живых:", crow)
        sf.addRow("Радиус:", ispin("radius", 32, 8192, 384, suffix=" ед."))
        sf.addRow("Включается с расстояния:", ispin("activate_dist", 256, 16384, 2500, suffix=" ед."))
        hrow = QHBoxLayout()
        hrow.addWidget(ispin("from_hour", -1, 23, -1, "всегда"))
        hrow.addWidget(QLabel("–"))
        hrow.addWidget(ispin("to_hour", -1, 24, -1, "всегда"))
        hrow.addStretch(1)
        sf.addRow("Часы (с–до):", hrow)
        sf.addRow("Пополнение через:", ispin("respawn_hours", 0, 24 * 30, 24, "никогда", " ч"))
        fl = QLineEdit(kvs.get("require_flag", ""))
        fl.setPlaceholderText("флаг мира (пусто — всегда)")
        fl.editingFinished.connect(lambda: (kvs.__setitem__("require_flag", fl.text().strip()) if fl.text().strip() else kvs.pop("require_flag", None), ch()))
        sf.addRow("Только при флаге:", fl)
        gl.addLayout(sf)
        gl.addWidget(hint("NPC появляются вне поля зрения игрока и не ближе 400 ед. от него. Когда все убиты — новые "
                          "появятся через «пополнение» игровых часов."))
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
    elif ref.type == "zone":
        g, gl = _group("Зона")
        gl.addWidget(hint("Коробка с именем: события «вошёл / вышел из зоны», цели заданий «дойти до», условие «игрок в зоне». "
                          "В игре невидима. Поворачивается вместе с объектом (Alt+тащить)."))
        zf = _form()
        zrow = QHBoxLayout()
        for i, tip in enumerate(("ширина (X)", "глубина (Y)", "высота")):
            sp = QSpinBox()
            sp.setRange(16, 16384)
            sp.setSingleStep(32)
            sp.setToolTip(tip)
            sp.setValue(int(ref.size[i]))
            sp.valueChanged.connect(lambda v, i=i: (ref.size.__setitem__(i, float(v)), ch(), ctx.worldview.invalidate_overlay()))
            zrow.addWidget(sp)
        zf.addRow("Размер, ед.:", zrow)
        gl.addLayout(zf)
        users = []
        for e in p.events:
            if e.on in ("zone_enter", "zone_leave") and ref.name and e.target == ref.name:
                users.append("событие " + e.id)
        for q in p.quests:
            for o in q.all_objectives():
                if o.type == "go" and ref.name and o.target == ref.name:
                    users.append("задание %s, цель %s" % (q.id, o.id))
        gl.addWidget(hint(("Используют: " + ", ".join(users)) if users else
                          "Пока не используется. «Логика» → событие «Игрок вошёл в зону», или цель задания «Дойти до»."))
        bz = QPushButton("+ событие «вошёл в зону»")

        def new_zone_event():
            if not ref.name:
                QMessageBox.information(w, "Зона", "Сначала дайте зоне имя.")
                return
            ctx.new_event("zone_enter")
            e = p.events[-1] if p.events else None
            if e is not None and e.on == "zone_enter":
                e.target = ref.name
                insp.changed(True)
                insp.show_target(("event", e.id))
        bz.clicked.connect(new_zone_event)
        gl.addWidget(bz)
        lay.addWidget(g)
        from .sound_pages import zone_location_group
        lay.addWidget(zone_location_group(insp, ref, ch))
    elif ref.type == "sound":
        from .sound_pages import sound_ref_group
        lay.addWidget(sound_ref_group(insp, ref, ch))
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
    if ref.type in ("item", "container", "prop", "npc", "marker", "terminal"):
        g, gl = _group("Дополнительные ключи")
        kvt = KVTable(ref.keyvalues)
        kvt.changed.connect(ch)
        gl.addWidget(kvt)
        lay.addWidget(g)
    if ref.type not in ("path", "transition", "zone"):
        g, gl = _group("Условие присутствия")
        gl.addWidget(hint("Объект есть в игре, только пока условия верны: появляется и исчезает сам (баррикада после задания, "
                          "NPC переехал, торговец ночью дома). NPC не исчезает на глазах у игрока. Убитый NPC или взятый "
                          "предмет не возвращаются."))
        pl = LogicList(ctx, ref.conditions, self_label="этот объект",
                       empty_text="Нет условий — объект есть всегда.")
        pl.changed.connect(lambda: (ch(), ctx.worldview.invalidate_overlay()))
        gl.addWidget(pl)
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


PROP_CLASSES = ["prop_static", "prop_dynamic", "prop_dynamic_override", "prop_physics", "prop_physics_override",
                "prop_physics_multiplayer", "prop_ragdoll", "prop_door_rotating"]
SOLIDS = [("6", "по модели (VPhysics)"), ("2", "ограничивающий бокс"), ("0", "без столкновений")]
# keys shown with their own fields (the table lists the rest)
_OWN_KEYS = {"id", "classname", "origin", "angles", "targetname", "model", "skin", "solid", "diffuse", "radius", "power",
             "spot_cone_inner", "spot_cone_outer", "autoshadow", "enable_from_hour", "enable_to_hour", "light_type",
             "rpm_key", "rpm_lock_level"}


def mapobj_page(insp, scene, key):
    """An object placed in Hammer (from the map's VMF): position, model, keys; lights get
    their own fields. Without a VMF - what the BSP says, read only."""
    ctx = insp.ctx
    ob = scene.find(key)
    if ob is None:
        return insp._page("Объект не найден")
    cls = ob.classname
    tn = ob.get("targetname")
    title = "Hammer: %s%s" % (cls, ("  #%d" % key[1]) if key[0] == "v" else "")
    if ob.editable:
        sub = ("Стоит в самой карте (VMF). Правки записываются в VMF при сохранении (Ctrl+S); в игре они появятся после "
               "сборки карты — «⚙ Собрать карту» (F9). Гизмо: стрелки, ПКМ — кольца поворота, End — приземлить.")
    else:
        sub = ("VMF этой карты не найден (папки исходников карт — Проект → Настройки), поэтому объект только для просмотра. "
               "Двигать его можно в Hammer.")
    w, lay = insp._page(title, sub)

    def changed(regroup=False):
        if regroup:
            ctx.worldview.refresh_objects()
        else:
            ctx.worldview.object_moved(ob)
            ctx.worldview.invalidate_overlay()
        insp.changed(False)
        ctx.world_changed("obj")

    f = _form()
    if ob.editable:
        known = PROP_CLASSES if cls.startswith("prop_") else (["light_deferred", "light_deferred_spot"] if cls.startswith("light_deferred") else [cls])
        cc = combo([(c, c) for c in known], cls, editable=True)

        def set_cls():
            v = cc.currentText().strip()
            if v and v != ob.classname:
                ob.set_key("classname", v)
                changed(True)
                insp.rebuild_soon()
        bind_combo(cc, set_cls)
        f.addRow("Класс:", cc)
        tl = QLineEdit(tn)
        tl.setPlaceholderText("targetname (необязательно)")
        tl.editingFinished.connect(lambda: (ob.set_key("targetname", tl.text().strip()), changed()) if tl.text().strip() != ob.get("targetname") else None)
        f.addRow("Имя:", tl)
        if cls.startswith("prop_") or ob.model:
            mrow = QHBoxLayout()
            ml = QLineEdit(ob.model)
            ml.editingFinished.connect(lambda: (ob.set_key("model", ml.text().strip().replace("\\", "/")), scene.model(ob.model), changed(True))
                                       if ml.text().strip() != ob.model else None)
            mrow.addWidget(ml, 1)
            br = QToolButton(text="…")
            br.setToolTip("Выбрать модель из игры")

            def browse():
                m = ctx.browse_model(ob.model, "models/")
                if m:
                    ob.set_key("model", m)
                    scene.model(m)
                    changed(True)
                    insp.rebuild_soon()
            br.clicked.connect(browse)
            mrow.addWidget(br)
            f.addRow("Модель:", mrow)
            sk = QSpinBox()
            sk.setRange(0, 64)
            sk.setValue(ob.skin)
            sk.valueChanged.connect(lambda v: (ob.set_key("skin", str(v)), changed(True)))
            f.addRow("Скин:", sk)
            so = combo(SOLIDS, ob.get("solid", "6"))
            so.currentIndexChanged.connect(lambda _: (ob.set_key("solid", so.currentData()), changed()))
            f.addRow("Столкновения:", so)
    else:
        f.addRow("Имя:", QLabel(tn or "— нет —"))
        if ob.model:
            f.addRow("Модель:", QLabel(ob.model))
    # position / angles
    prow = QHBoxLayout()
    spins = []
    for i in range(3):
        sp = QDoubleSpinBox()
        sp.setRange(-65536, 65536)
        sp.setDecimals(1)
        sp.setValue(ob.origin[i])
        sp.setEnabled(ob.editable)
        spins.append(sp)
        prow.addWidget(sp)

    def set_pos():
        v = [s.value() for s in spins]
        if v != ob.origin:
            ob.set_transform(v, None)
            changed()
    for sp in spins:
        sp.editingFinished.connect(set_pos)
    if ob.editable:
        lb = QToolButton(text="⤓")
        lb.setToolTip("Приземлить (End)")
        lb.clicked.connect(lambda: (ctx.worldview.select(("obj", key), emit=False), ctx.worldview.land_selected()))
        prow.addWidget(lb)
    f.addRow("Позиция:", prow)
    arow = QHBoxLayout()
    aspins = []
    for i, name in enumerate(("тангаж", "рыскание", "крен")):
        sp = QDoubleSpinBox()
        sp.setRange(-360, 360)
        sp.setDecimals(1)
        sp.setSuffix("°")
        sp.setToolTip(name)
        sp.setValue(ob.angles[i])
        sp.setEnabled(ob.editable)
        aspins.append(sp)
        arow.addWidget(sp)

    def set_ang():
        v = [s.value() for s in aspins]
        if v != ob.angles:
            ob.set_transform(None, v)
            changed()
    for sp in aspins:
        sp.editingFinished.connect(set_ang)
    f.addRow("Поворот:", arow)
    lay.addLayout(f)

    # lights
    if ob.editable and cls in ("light_deferred", "light_deferred_spot"):
        g, gl = _group("Свет")
        lf = _form()
        col = [int(float(x)) for x in (ob.get("diffuse", "255 255 255 255").split() + ["255"] * 4)[:4]]
        cb = QPushButton("  ")
        cb.setStyleSheet("background: rgb(%d,%d,%d); min-width: 60px;" % tuple(col[:3]))

        def pick_color():
            from PySide6.QtWidgets import QColorDialog
            c = QColorDialog.getColor(QColor(*col[:3]), w, "Цвет света")
            if c.isValid():
                ob.set_key("diffuse", "%d %d %d %d" % (c.red(), c.green(), c.blue(), col[3]))
                changed()
                insp.rebuild_soon()
        cb.clicked.connect(pick_color)
        crow = QHBoxLayout()
        crow.addWidget(cb)
        br = QSpinBox()
        br.setRange(0, 4000)
        br.setValue(col[3])
        br.setToolTip("Яркость (четвёртое число цвета; 255 — обычная)")
        br.valueChanged.connect(lambda v: (ob.set_key("diffuse", "%d %d %d %d" % (col[0], col[1], col[2], v)), changed()))
        crow.addWidget(QLabel("яркость"))
        crow.addWidget(br)
        crow.addStretch(1)
        lf.addRow("Цвет:", crow)

        def fspin(key, lo, hi, dflt, step=1.0, dec=0, suffix=""):
            sp = QDoubleSpinBox()
            sp.setRange(lo, hi)
            sp.setDecimals(dec)
            sp.setSingleStep(step)
            sp.setSuffix(suffix)
            try:
                sp.setValue(float(ob.get(key, dflt) or dflt))
            except ValueError:
                sp.setValue(float(dflt))
            sp.valueChanged.connect(lambda v: (ob.set_key(key, ("%d" % v) if dec == 0 else ("%g" % v)), changed()))
            return sp
        lf.addRow("Радиус:", fspin("radius", 8, 8192, 256, 16))
        lf.addRow("Затухание:", fspin("power", 0.1, 8, 2, 0.1, 1))
        if cls.endswith("spot"):
            lf.addRow("Конус внутр.:", fspin("spot_cone_inner", 1, 179, 30, 1, 0, "°"))
            lf.addRow("Конус внешн.:", fspin("spot_cone_outer", 1, 179, 45, 1, 0, "°"))
        sh = QCheckBox("Тень (свет не проходит сквозь стены)")
        sh.setChecked(ob.get("autoshadow", "1") != "0")
        sh.toggled.connect(lambda v: (ob.set_key("autoshadow", "1" if v else "0"), changed()))
        lf.addRow("", sh)
        hrow = QHBoxLayout()
        h0 = fspin("enable_from_hour", -1, 24, -1, 0.5, 1)
        h1 = fspin("enable_to_hour", -1, 24, -1, 0.5, 1)
        hrow.addWidget(h0)
        hrow.addWidget(QLabel("–"))
        hrow.addWidget(h1)
        lf.addRow("Горит с–до (ч):", hrow)
        lf.addRow("", hint("−1 — всегда. Например 20 – 6: включается вечером, гаснет утром."))
        gl.addLayout(lf)
        lay.addWidget(g)

    # doors: lock with a key item / lockpick skill (rpm_key, rpm_lock_level; "Starts locked" = spawnflag 2048)
    if ob.editable and (cls.startswith("prop_door") or cls in ("func_door", "func_door_rotating")):
        g, gl = _group("Замок")
        df = _form()
        try:
            sf = int(ob.get("spawnflags", "0") or 0)
        except ValueError:
            sf = 0
        lk = QCheckBox("Заперта с начала")
        lk.setChecked(bool(sf & 2048))

        def set_locked(v):
            try:
                cur = int(ob.get("spawnflags", "0") or 0)
            except ValueError:
                cur = 0
            ob.set_key("spawnflags", str((cur | 2048) if v else (cur & ~2048)))
            changed()
        lk.toggled.connect(set_locked)
        df.addRow("", lk)
        p = ctx.project
        keys = [("", "— без ключа —")] + [(x.id, "%s  (%s)" % (x.label(ctx.display_lang, p.base_language), x.id))
                                          for x in p.items if x.category == "key" or x.base == "key"]
        kc = combo(keys, ob.get("rpm_key"), editable=True)

        def set_key():
            v = kc.currentData() if kc.currentIndex() >= 0 and kc.currentText() == kc.itemText(kc.currentIndex()) else kc.currentText().strip()
            if v:
                ob.set_key("rpm_key", v)
            elif ob.ent is not None:
                ob.ent.delete_key("rpm_key")
            changed()
        bind_combo(kc, set_key)
        df.addRow("Ключ:", kc)
        lv = QSpinBox()
        lv.setRange(0, 100)
        lv.setSpecialValueText("не взломать")
        try:
            lv.setValue(int(ob.get("rpm_lock_level", "0") or 0))
        except ValueError:
            pass
        lv.valueChanged.connect(lambda v: (ob.set_key("rpm_lock_level", str(v)) if v else ob.ent.delete_key("rpm_lock_level"), changed()))
        df.addRow("Взлом (навык):", lv)
        gl.addLayout(df)
        gl.addWidget(hint("Запертая дверь открывается ключом из инвентаря игрока или навыком «Взлом» не ниже уровня."))
        lay.addWidget(g)

    # NPC placed in Hammer: the project's NPC with the same targetname
    if ob.is_npc():
        if tn and ctx.project.npc(tn):
            b = QPushButton("Открыть NPC проекта «%s»" % tn)
            b.clicked.connect(lambda: ctx.open_npc(tn))
            lay.addWidget(b)
        elif tn:
            b = QPushButton("Добавить в проект (распорядок, диалог, фракция)")
            b.clicked.connect(lambda: ctx.npc_from_hammer(tn, cls, ob))
            lay.addWidget(b)
        else:
            lay.addWidget(hint("Без targetname игра не сможет дать ему распорядок или диалог — задайте имя."))

    # the other keys
    other = {}
    if ob.ent is not None:
        for k, v in ob.ent.kv:
            if k.lower() not in _OWN_KEYS:
                other[k] = v
    else:
        other = {k: v for k, v in ob.kv.items() if k.lower() not in _OWN_KEYS}
    if other or ob.editable:
        g, gl = _group("Остальные ключи")
        if ob.editable:
            tbl = KVTable(other)

            def kv_changed():
                cur = {k for k, _ in ob.ent.kv if k.lower() not in _OWN_KEYS}
                for k in cur - set(other):
                    ob.ent.delete_key(k)
                for k, v in other.items():
                    ob.set_key(k, v)
                changed()
            tbl.changed.connect(kv_changed)
            gl.addWidget(tbl)
        else:
            l = QLabel("\n".join("%s: %s" % kv for kv in sorted(other.items())))
            l.setTextInteractionFlags(Qt.TextSelectableByMouse)
            l.setWordWrap(True)
            gl.addWidget(l)
        lay.addWidget(g)
    outs = ob.ent.outputs if ob.ent is not None else []
    if outs:
        g, gl = _group("Выходы (правятся в Hammer)")
        l = QLabel("\n".join("%s → %s" % (o, t.replace("\x1b", ",")) for o, t in outs))
        l.setWordWrap(True)
        l.setTextInteractionFlags(Qt.TextSelectableByMouse)
        gl.addWidget(l)
        lay.addWidget(g)
    row = QHBoxLayout()
    b = QPushButton("Показать")
    b.clicked.connect(lambda: (ctx.worldview.select(("obj", key), emit=False), ctx.worldview.focus_selected()))
    row.addWidget(b)
    if ob.editable:
        b = QPushButton("Дублировать")
        b.clicked.connect(lambda: (ctx.worldview.select(("obj", key), emit=False), ctx.duplicate_world_selected()))
        row.addWidget(b)
        b = QPushButton("Удалить")
        b.clicked.connect(lambda: ctx.delete_targets([("obj", key)]))
        row.addWidget(b)
    row.addStretch(1)
    lay.addLayout(row)
    return w, lay


def multi_page(insp, n):
    ctx = insp.ctx
    w, lay = insp._page("Выделено объектов: %d" % n, "Тащите любой из них — двигаются все; гизмо двигает и вращает группу "
                                                        "(вокруг выделенного последним). End — приземлить каждый.")
    row = QHBoxLayout()
    for text, fn in (("Приземлить", ctx.worldview.land_selected), ("Повернуть на 90°", lambda: ctx.worldview.rotate_selected(90)),
                     ("Дублировать", ctx.duplicate_world_selected), ("Копировать", ctx.copy_world_selected),
                     ("Удалить", ctx.delete_world_selected)):
        b = QPushButton(text)
        b.clicked.connect(fn)
        row.addWidget(b)
    row.addStretch(1)
    lay.addLayout(row)
    names = [ctx.worldview.t_label(t) for t in ctx.worldview.selset][:40]
    lay.addWidget(hint("\n".join(names)))
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
            insp.rebuild_soon()
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
