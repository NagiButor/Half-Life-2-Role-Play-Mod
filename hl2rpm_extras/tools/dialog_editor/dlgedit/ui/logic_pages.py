"""The "Logic" tab: events (when <something happens> and <conditions> -> <actions>) and the
world flags / variables, plus their inspector pages. Game files: scripts/rpg/events.txt,
scripts/rpg/globals.txt (game/server/hl2rpm_logic.cpp)."""
import re

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDoubleSpinBox, QHBoxLayout, QHeaderView, QInputDialog, QLabel,
                               QLineEdit, QListWidget, QListWidgetItem, QMessageBox, QPushButton, QSpinBox, QTableWidget,
                               QTableWidgetItem, QVBoxLayout, QWidget)

from .. import logic
from ..model import Event, Global, Topic
from .logic_widgets import LogicList, _Combo, choices
from .widgets import AutoText, LangTexts, hint

ID_RE = re.compile(r"^[A-Za-z0-9_\-]+$")


def _group(title):
    from .inspector import _group as g
    return g(title)


def _form():
    from .inspector import _form as f
    return f()


class LogicMixin:
    def _build_logic_tab(self):
        w = QWidget()
        l = QVBoxLayout(w)
        l.setContentsMargins(4, 4, 4, 4)
        l.addWidget(hint("События: «когда что-то случилось и условия верны — сделать». "
                         "Флаги и переменные — память мира (сохраняются, переходят между картами)."))
        self.logic_list = QListWidget()
        self.logic_list.itemClicked.connect(lambda it: self.inspector.show_target(it.data(Qt.UserRole)))
        l.addWidget(self.logic_list, 1)
        bl = QHBoxLayout()
        b = QPushButton("+ событие")
        b.clicked.connect(lambda: self.new_event())
        bl.addWidget(b)
        b = QPushButton("+ терминал")
        b.setToolTip("Компьютер или консоль Альянса: записи, команды, взлом (ставится на карту объектом «Терминал»)")

        def new_term():
            from .term_pages import new_terminal
            new_terminal(self)
        b.clicked.connect(new_term)
        bl.addWidget(b)
        b = QPushButton("+ тема")
        b.setToolTip("Общая тема диалога: вопрос у всех NPC выбранных классов или фракций («Что тут происходит?»)")
        b.clicked.connect(lambda: self.new_topic())
        bl.addWidget(b)
        b = QPushButton("Удалить")
        b.clicked.connect(self.delete_selected_event)
        bl.addWidget(b)
        bl.addStretch(1)
        l.addLayout(bl)
        self.logic_tab = w
        return w

    def refresh_logic_list(self):
        if not hasattr(self, "logic_list"):
            return
        self.logic_list.clear()
        if not self.project:
            return
        p = self.project
        it = QListWidgetItem("Флаги и переменные (%d)" % len(p.globals))
        it.setData(Qt.UserRole, ("globals",))
        self.logic_list.addItem(it)
        lang = self.display_lang
        for e in p.events:
            on = logic.EVENT_BY_ID.get(e.on, (e.on, e.on, "", ""))[1]
            what = (" " + e.target) if e.target else ""
            if str(e.value) != "":
                what += " (%s)" % e.value
            acts = "; ".join(logic.describe_action(a, p, lang) for a in e.actions[:2]) or "ничего"
            it = QListWidgetItem("%s\n   %s%s → %s" % (e.id, on.lower(), what, acts))
            it.setData(Qt.UserRole, ("event", e.id))
            self.logic_list.addItem(it)
        for t in getattr(p, "topics", []):
            who = ", ".join(t.classes + ["фракция " + f for f in t.factions]) or "никому"
            q = t.text.get(lang) or t.text.get(p.base_language) or "(нет вопроса)"
            it = QListWidgetItem("\u2709 %s\n   «%s» \u2014 %s" % (t.id, q, who))
            it.setData(Qt.UserRole, ("topic", t.id))
            self.logic_list.addItem(it)
        from ..termdata import STYLE_NAMES
        for t in getattr(p, "terminals", []):
            lock = (" · взлом %d" % t.lock_level) if t.lock_level else ""
            it = QListWidgetItem("▣ %s\n   %s · %s · записей %d%s" % (t.id, t.label(lang, p.base_language),
                                 STYLE_NAMES.get(t.style, t.style).split(" (")[0].lower(), len(t.all_entries()), lock))
            it.setData(Qt.UserRole, ("terminal", t.id))
            self.logic_list.addItem(it)

    def new_event(self, on="when"):
        if not self.project:
            return
        n = 1
        while self.project.event("event_%d" % n):
            n += 1
        eid, ok = QInputDialog.getText(self, "Новое событие", "id события (латиница, цифры, _):", text="event_%d" % n)
        eid = (eid or "").strip()
        if not ok or not eid:
            return
        if self.project.event(eid) or not ID_RE.match(eid):
            QMessageBox.warning(self, "Событие", "Такой id уже есть или в нём не только латиница, цифры, _ и -")
            return
        e = Event(eid)
        e.on = on
        self.project.events.append(e)
        self.model_changed(True)
        self.refresh_logic_list()
        self.left_tabs.setCurrentWidget(self.logic_tab)
        self.inspector.show_target(("event", eid))

    def new_topic(self):
        if not self.project:
            return
        n = 1
        while self.project.topic("topic_%d" % n):
            n += 1
        tid, ok = QInputDialog.getText(self, "Новая общая тема", "id темы (латиница, цифры, _):", text="topic_%d" % n)
        tid = (tid or "").strip()
        if not ok or not tid:
            return
        if self.project.topic(tid) or not ID_RE.match(tid):
            QMessageBox.warning(self, "Тема", "Такой id уже есть или в нём не только латиница, цифры, _ и -")
            return
        t = Topic(tid)
        t.classes = ["npc_citizen"]
        base = self.project.base_language
        t.text[base] = "Что тут происходит?" if base == "russian" else "What's going on here?"
        t.line[base] = "Да ничего хорошего." if base == "russian" else "Nothing good."
        self.project.topics.append(t)
        self.model_changed(True)
        self.refresh_logic_list()
        self.left_tabs.setCurrentWidget(self.logic_tab)
        self.inspector.show_target(("topic", tid))

    def delete_selected_event(self):
        it = self.logic_list.currentItem() if hasattr(self, "logic_list") else None
        t = it.data(Qt.UserRole) if it else None
        if t and t[0] == "topic":
            top = self.project.topic(t[1])
            if top and QMessageBox.question(self, "Удалить тему", "Удалить общую тему «%s»?" % top.id) == QMessageBox.Yes:
                self.project.topics.remove(top)
                self.model_changed(True)
                self.refresh_logic_list()
                self.inspector.show_target(("globals",))
            return
        if t and t[0] == "terminal":
            term = self.project.terminal(t[1])
            if term and QMessageBox.question(self, "Удалить терминал", "Удалить терминал «%s»?" % term.id) == QMessageBox.Yes:
                self.project.terminals.remove(term)
                self.model_changed(True)
                self.refresh_logic_list()
                self.inspector.show_target(("globals",))
            return
        if not t or t[0] != "event":
            return
        e = self.project.event(t[1])
        if e and QMessageBox.question(self, "Удалить событие", "Удалить событие «%s»?" % e.id) == QMessageBox.Yes:
            self.project.events.remove(e)
            self.model_changed(True)
            self.refresh_logic_list()
            self.inspector.show_target(("globals",))


# --- inspector pages ---------------------------------------------------------------------------------
def event_page(insp, e):
    ctx = insp.ctx
    p = ctx.project
    w, lay = insp._page("Событие  %s" % e.id, "Когда случается выбранное и все условия верны — выполняются действия. "
                                              "«Один раз» — только в первый раз за игру.")

    def changed(structural=False):
        insp.changed(structural)
        ctx.refresh_logic_list()

    f = _form()
    idl = QLineEdit(e.id)

    def rename():
        new = idl.text().strip()
        if new == e.id:
            return
        if not new or not ID_RE.match(new) or p.event(new):
            QMessageBox.warning(insp, "id события", "Пустой, не латиница или уже существует")
            idl.setText(e.id)
            return
        old = e.id
        e.id = new
        for _w, _k, lst in logic.iter_logic(p):
            for x in lst or []:
                if x.get("type") == "event" and x.get("name") == old:
                    x["name"] = new
        changed(True)
        insp.show_target(("event", new))
    idl.editingFinished.connect(rename)
    f.addRow("id:", idl)
    on = QComboBox()
    for tid, label, _tk, _vl in logic.EVENT_TYPES:
        on.addItem(label, tid)
    on.setCurrentIndex(max(0, on.findData(e.on)))

    def set_on(_):
        e.on = on.currentData()
        changed()
        insp.rebuild_soon()
    on.currentIndexChanged.connect(set_on)
    f.addRow("Когда:", on)
    tid, label, tkind, vlabel = logic.EVENT_BY_ID.get(e.on, ("", "", "", ""))
    if tkind:
        tc = _Combo(choices(ctx, tkind), e.target, editable=True)
        tc.setToolTip("Пусто — любой. Имена через ;, * — любые символы (zombie*)")

        def set_t(*_):
            e.target = tc.value()
            changed()
        tc.currentIndexChanged.connect(set_t)
        tc.lineEdit().editingFinished.connect(set_t)
        f.addRow({"zone": "Зона:", "npc": "NPC:", "target": "Объект (targetname):", "quest": "Задание:"}.get(tkind, "Что:"), tc)
    if vlabel:
        vs = QDoubleSpinBox() if e.on == "hour" else QSpinBox()
        vs.setRange(0, 23 if e.on == "hour" else 99)
        if e.on == "hour":
            vs.setDecimals(0)
        try:
            vs.setValue(float(e.value) if e.value != "" else 0)
        except ValueError:
            vs.setValue(0)

        def set_v(v):
            e.value = str(int(v))
            changed()
        vs.valueChanged.connect(set_v)
        if e.value == "":
            e.value = "0"
        f.addRow(vlabel + ":", vs)
    mc = _Combo(choices(ctx, "map"), e.map, editable=True, empty_label="— любая карта —")

    def set_map(*_):
        e.map = mc.value()
        changed()
    mc.currentIndexChanged.connect(set_map)
    f.addRow("Только на карте:", mc)
    row = QHBoxLayout()
    once = QCheckBox("Один раз")
    once.setChecked(e.once)
    once.toggled.connect(lambda v: (setattr(e, "once", bool(v)), changed()))
    row.addWidget(once)
    row.addWidget(QLabel("   Задержка:"))
    dl = QDoubleSpinBox()
    dl.setRange(0, 3600)
    dl.setDecimals(1)
    dl.setSuffix(" с")
    dl.setValue(e.delay)

    def set_d(v):
        e.delay = round(v, 1)
        changed()
    dl.valueChanged.connect(set_d)
    row.addWidget(dl)
    row.addStretch(1)
    f.addRow("", row)
    lay.addLayout(f)
    if e.on == "when":
        lay.addWidget(hint("Срабатывает в тот момент, когда условия становятся верными (проверка 4 раза в секунду). "
                           "Без «Один раз» — каждый раз, когда они снова становятся верными."))
    g, gl = _group("Условия")
    cl = LogicList(ctx, e.conditions, self_label="NPC события")
    cl.changed.connect(lambda: changed())
    gl.addWidget(cl)
    lay.addWidget(g)
    g, gl = _group("Действия")
    al = LogicList(ctx, e.actions, actions=True, self_label="NPC события")
    al.changed.connect(lambda: changed())
    gl.addWidget(al)
    lay.addWidget(g)
    g, gl = _group("Заметка")
    note = AutoText(e.note, 2, 5)
    note.edited.connect(lambda t: (setattr(e, "note", t), insp.changed(False)))
    gl.addWidget(note)
    lay.addWidget(g)
    de = QPushButton("Удалить событие")

    def delete():
        if QMessageBox.question(insp, "Удалить событие", "Удалить событие «%s»?" % e.id) == QMessageBox.Yes:
            p.events.remove(e)
            changed(True)
            insp.show_target(("globals",))
    de.clicked.connect(delete)
    lay.addWidget(de)
    return w, lay


def topic_page(insp, t):
    ctx = insp.ctx
    p = ctx.project
    w, lay = insp._page("Общая тема  %s" % t.id,
                        "Вопрос появится в диалоге каждого NPC проекта выбранных классов или фракций (перед «Пока»); "
                        "после ответа — снова список вопросов, как в New Vegas. У NPC без своих реплик будет короткий "
                        "диалог: приветствие ниже, темы, «Пока». NPC, поставленные только в Hammer, тем не получают.")

    def changed(structural=False):
        insp.changed(structural)
        ctx.refresh_logic_list()

    f = _form()
    idl = QLineEdit(t.id)

    def rename():
        new = idl.text().strip()
        if new == t.id:
            return
        if not new or not ID_RE.match(new) or p.topic(new):
            QMessageBox.warning(insp, "id темы", "Пустой, не латиница или уже существует")
            idl.setText(t.id)
            return
        t.id = new
        changed(True)
        insp.show_target(("topic", new))
    idl.editingFinished.connect(rename)
    f.addRow("id:", idl)
    classes = sorted({n.actor.classname for n in p.npcs} | {"npc_citizen", "npc_metropolice", "npc_combine_s", "npc_vortigaunt"})
    cl = QLineEdit("; ".join(t.classes))
    cl.setPlaceholderText("npc_citizen; npc_metropolice")
    cl.setToolTip("Классы NPC через «;». В проекте: " + ", ".join(classes))

    def set_cl():
        t.classes = [x.strip() for x in cl.text().replace(",", ";").split(";") if x.strip()]
        changed()
    cl.editingFinished.connect(set_cl)
    f.addRow("Классы:", cl)
    fl = QLineEdit("; ".join(t.factions))
    fl.setPlaceholderText(", ".join(x.id for x in p.factions) or "фракции проекта")
    fl.setToolTip("Фракции через «;» (вкладка NPC → фракции): тема у всех их членов")

    def set_fl():
        t.factions = [x.strip() for x in fl.text().replace(",", ";").split(";") if x.strip()]
        changed()
    fl.editingFinished.connect(set_fl)
    f.addRow("Фракции:", fl)
    once = QCheckBox("Спросить у каждого NPC один раз")
    once.setChecked(t.once)
    once.toggled.connect(lambda v: (setattr(t, "once", bool(v)), changed()))
    f.addRow("", once)
    lay.addLayout(f)
    who = [n.targetname for n in p.npcs if not n.actor.is_template and t.matches(n.actor.resolved(p).classname, n.actor.resolved(p).faction)]
    lay.addWidget(hint("Получат: " + (", ".join(who) if who else "пока никто из NPC проекта")))
    g, gl = _group("Вопрос игрока")
    qt = LangTexts(ctx, t.text, None, multiline=False, byte_limit=255, placeholder="Что тут происходит?")
    qt.changed.connect(lambda: changed())
    gl.addWidget(qt)
    lay.addWidget(g)
    g, gl = _group("Ответ NPC")
    at = LangTexts(ctx, t.line, None, multiline=True, placeholder="Да ничего хорошего: ГО опять…")
    at.changed.connect(lambda: changed())
    gl.addWidget(at)
    lay.addWidget(g)
    g, gl = _group("Условия (когда вопрос виден)")
    cnd = LogicList(ctx, t.conditions, self_label="этот NPC")
    cnd.changed.connect(lambda: changed())
    gl.addWidget(cnd)
    lay.addWidget(g)
    g, gl = _group("Действия (когда спросили)")
    act = LogicList(ctx, t.actions, actions=True, self_label="этот NPC")
    act.changed.connect(lambda: changed())
    gl.addWidget(act)
    lay.addWidget(g)
    g, gl = _group("Приветствие NPC без своих реплик (общее для всех тем)")
    gr = LangTexts(ctx, p.topic_greeting, None, multiline=False, placeholder="Чего тебе?")
    gr.changed.connect(lambda: changed())
    gl.addWidget(gr)
    lay.addWidget(g)
    g, gl = _group("Заметка")
    note = AutoText(t.note, 2, 5)
    note.edited.connect(lambda s: (setattr(t, "note", s), insp.changed(False)))
    gl.addWidget(note)
    lay.addWidget(g)
    de = QPushButton("Удалить тему")

    def delete():
        if QMessageBox.question(insp, "Удалить тему", "Удалить общую тему «%s»?" % t.id) == QMessageBox.Yes:
            p.topics.remove(t)
            changed(True)
            insp.show_target(("globals",))
    de.clicked.connect(delete)
    lay.addWidget(de)
    return w, lay


def globals_page(insp):
    ctx = insp.ctx
    p = ctx.project
    w, lay = insp._page("Флаги и переменные",
                        "Флаг — да/нет («дверь склада открыта»), переменная — число («сколько крыс убито»). "
                        "Их ставят действия, проверяют условия. Здесь — список с описаниями и значения новой игры; "
                        "имена, придуманные прямо в условиях и действиях, тоже работают (их видно ниже).")
    tbl = QTableWidget(len(p.globals), 4)
    tbl.setHorizontalHeaderLabels(["Имя", "Тип", "В новой игре", "Описание"])
    tbl.horizontalHeader().setSectionResizeMode(3, QHeaderView.Stretch)
    tbl.verticalHeader().setVisible(False)
    tbl.setMinimumHeight(220)

    def store():
        out = []
        for r in range(tbl.rowCount()):
            name = (tbl.item(r, 0).text() if tbl.item(r, 0) else "").strip()
            if not name:
                continue
            kind = tbl.cellWidget(r, 1).currentData()
            g = Global(name, kind)
            g.default = tbl.cellWidget(r, 2).value()
            g.note = tbl.item(r, 3).text() if tbl.item(r, 3) else ""
            out.append(g)
        p.globals[:] = out
        insp.changed(False)
        ctx.refresh_logic_list()

    def add_row(g):
        r = tbl.rowCount() if g is None else p.globals.index(g)
        if g is None:
            tbl.insertRow(r)
            g = Global("new_flag_%d" % (r + 1), "flag")
        tbl.setItem(r, 0, QTableWidgetItem(g.name))
        kc = QComboBox()
        kc.addItem("флаг", "flag")
        kc.addItem("переменная", "var")
        kc.setCurrentIndex(0 if g.kind == "flag" else 1)
        kc.currentIndexChanged.connect(lambda _: store())
        tbl.setCellWidget(r, 1, kc)
        sp = QSpinBox()
        sp.setRange(-1000000, 1000000)
        sp.setValue(int(g.default))
        sp.valueChanged.connect(lambda _: store())
        tbl.setCellWidget(r, 2, sp)
        tbl.setItem(r, 3, QTableWidgetItem(g.note))
    tbl.blockSignals(True)
    for g in p.globals:
        add_row(g)
    tbl.blockSignals(False)
    tbl.itemChanged.connect(lambda _: store())
    lay.addWidget(tbl)
    bl = QHBoxLayout()
    ab = QPushButton("+ флаг / переменная")

    def add():
        tbl.blockSignals(True)
        add_row(None)
        tbl.blockSignals(False)
        store()
    ab.clicked.connect(add)
    db = QPushButton("− выбранная")

    def rm():
        r = tbl.currentRow()
        if r >= 0:
            tbl.removeRow(r)
            store()
    db.clicked.connect(rm)
    bl.addWidget(ab)
    bl.addWidget(db)
    bl.addStretch(1)
    lay.addLayout(bl)
    flags, vars_, events = logic.known_names(p)
    declared = {g.name for g in p.globals}
    loose = [n for n in flags + vars_ if n not in declared]
    if loose:
        g, gl = _group("Используются, но не описаны (%d)" % len(loose))
        gl.addWidget(hint(", ".join(loose)))
        b = QPushButton("Добавить их в список")

        def adopt():
            for n in loose:
                p.globals.append(Global(n, "flag" if n in flags else "var"))
            insp.changed(False)
            ctx.refresh_logic_list()
            insp.rebuild_soon()
        b.clicked.connect(adopt)
        gl.addWidget(b)
        lay.addWidget(g)
    return w, lay
