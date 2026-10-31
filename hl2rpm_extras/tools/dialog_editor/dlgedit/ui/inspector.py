"""Right panel: properties of the selected NPC (dialog, actor, routine) / line / reply /
returning visit / quest / placed object / faction."""
import os

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (QButtonGroup, QCheckBox, QComboBox, QDoubleSpinBox, QFormLayout, QFrame, QGroupBox,
                               QHBoxLayout, QLabel, QLineEdit, QListWidget, QListWidgetItem, QPushButton, QRadioButton,
                               QScrollArea, QSpinBox, QTableWidget, QTableWidgetItem, QToolButton, QVBoxLayout, QWidget,
                               QInputDialog, QMessageBox, QHeaderView, QAbstractItemView, QTabWidget)

from ..model import (LANGUAGE_NAMES, QUEST_STATES, QUEST_STATE_NAMES, TARGETNAME_RE, Option, Quest, Stage, short_lang,
                     text_for)
from .widgets import LangTexts, NodeCombo, QuestCombo, AutoText, completer_line, hint
from . import theme
from . import world_pages

COMMON_SEQUENCES = ["g_fist", "g_point", "g_shrug", "g_answer", "g_explain", "g_nod", "g_headshake", "g_plead",
                    "gesture_agitated_1", "g_present", "g_frustrated", "g_despair", "g_question"]


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


class Inspector(QScrollArea):
    navigate = Signal(object)      # ("node", id) etc.

    def __init__(self, ctx):
        super().__init__()
        self.ctx = ctx
        self.setWidgetResizable(True)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.setMinimumWidth(400)
        self.target = None
        self.npc_tab = 0          # Диалог / Персонаж / Распорядок
        self.pkg_sel = 0          # selected AI package on the routine tab
        self.show_target(None)

    # --- helpers ------------------------------------------------------------------------
    def changed(self, structural=False):
        self.ctx.model_changed(structural)

    def _page(self, title, subtitle=""):
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(10, 10, 10, 10)
        lay.setSpacing(10)
        t = QLabel(title)
        t.setObjectName("title")
        t.setWordWrap(True)
        lay.addWidget(t)
        if subtitle:
            lay.addWidget(hint(subtitle))
        return w, lay

    def show_target(self, target):
        self.target = target
        p = getattr(self.ctx, "project", None)
        npc = getattr(self.ctx, "npc", None)
        # take the old page out first: setWidget() would delete it at once, possibly
        # from inside a signal of one of its own buttons
        old = self.takeWidget()
        layer = getattr(self.ctx, "layer", None)
        if p is None:
            w, lay = self._page("Нет проекта", "Файл → Новый проект или Открыть.")
        elif target and target[0] == "quest":
            w, lay = self.quest_page(target[1])
        elif target and target[0] in ("ref", "pathpt") and layer is not None and layer.ref(target[1]) is not None:
            w, lay = world_pages.ref_page(self, layer, layer.ref(target[1]))
        elif target and target[0] == "hammer" and getattr(self.ctx, "scene", None) is not None:
            w, lay = world_pages.hammer_page(self, self.ctx.scene, target[1])
        elif target and target[0] == "nav" and layer is not None:
            w, lay = world_pages.nav_page(self, layer, target[1])
        elif target and target[0] == "faction" and p.faction(target[1]) is not None:
            w, lay = world_pages.faction_page(self, p.faction(target[1]))
        elif npc is None:
            w, lay = self._page("Нет NPC", "Слева «+ NPC» — создать, или Файл → Импорт диалогов игры.")
        elif not target or target[0] in ("npc", "multi"):
            w, lay = self.npc_page(npc)
            if target and target[0] == "multi":
                lay.insertWidget(1, hint("Выделено объектов: %d. Delete — удалить реплики, перетаскивание — двигать." % target[1]))
        elif target[0] == "node" and npc.node(target[1]):
            w, lay = self.node_page(npc, npc.node(target[1]))
        elif target[0] == "option" and npc.node(target[1]) and target[2] < len(npc.node(target[1]).options):
            w, lay = self.option_page(npc, npc.node(target[1]), target[2])
        elif target[0] == "returning":
            w, lay = self.returning_page(npc)
        elif target[0] == "start":
            w, lay = self.npc_page(npc)
        elif target[0] == "edge":
            ref = target[1:]
            if ref[0] in ("option", "after"):
                w, lay = self.option_page(npc, npc.node(ref[1]), ref[2])
            elif ref[0] == "auto":
                w, lay = self.node_page(npc, npc.node(ref[1]))
            elif ref[0] == "returning":
                w, lay = self.returning_page(npc)
            else:
                w, lay = self.npc_page(npc)
        else:
            w, lay = self.npc_page(npc)
        lay.addStretch(1)
        self.setWidget(w)
        if old is not None:
            old.deleteLater()

    # --- NPC --------------------------------------------------------------------------------
    def npc_page(self, npc):
        w, lay = self._page("NPC  %s" % npc.targetname)
        tabs = QTabWidget()
        tabs.setDocumentMode(True)
        dw = QWidget()
        dl = QVBoxLayout(dw)
        dl.setContentsMargins(0, 6, 0, 0)
        dl.setSpacing(10)
        self.npc_dialog_tab(npc, dl)
        dl.addStretch(1)
        tabs.addTab(dw, "Диалог")
        tabs.addTab(world_pages.actor_tab(self, npc), "Персонаж")
        tabs.addTab(world_pages.routine_tab(self, npc), "Распорядок (%d)" % len(npc.actor.packages))
        tabs.setCurrentIndex(self.npc_tab if 0 <= self.npc_tab < 3 else 0)
        tabs.currentChanged.connect(lambda i: setattr(self, "npc_tab", i))
        lay.addWidget(tabs)
        return w, lay

    def npc_dialog_tab(self, npc, lay):
        p = self.ctx.project
        w = lay.parentWidget()
        lay.addWidget(hint("targetname — имя NPC в Hammer: по нему игра находит этот диалог. "
                           "Отображаемое имя — в окне диалога и при обыске трупа."))
        f = _form()
        tn = QLineEdit(npc.targetname)
        tn.setToolTip("Должен совпадать с targetname NPC на карте (латиница, цифры, _)")

        def rename():
            new = tn.text().strip()
            if new == npc.targetname:
                return
            if not new or not TARGETNAME_RE.match(new):
                QMessageBox.warning(self, "targetname", "Только латиница, цифры и _ - .")
                tn.setText(npc.targetname)
                return
            if p.npc(new) and p.npc(new) is not npc:
                QMessageBox.warning(self, "targetname", "NPC «%s» уже есть в проекте" % new)
                tn.setText(npc.targetname)
                return
            self.ctx.rename_npc(npc, new)
        tn.editingFinished.connect(rename)
        f.addRow("targetname:", tn)
        lay.addLayout(f)

        g, gl = _group("Отображаемое имя")
        names = LangTexts(self.ctx, npc.display_name, multiline=False, placeholder="например, Доктор Кейв Джонсон")
        names.changed.connect(lambda: self.changed(False))
        gl.addWidget(names)
        ents = self.ctx.mapindex.find(npc.targetname)
        if ents:
            hl = [e.display for e in ents if e.display]
            if hl:
                b = QPushButton("Взять имя из Hammer: «%s»" % hl[0])

                def take():
                    npc.display_name[p.base_language] = hl[0]
                    self.changed(False)
                    self.show_target(self.target)
                b.clicked.connect(take)
                gl.addWidget(b)
        lay.addWidget(g)

        g, gl = _group("Начало разговора")
        f = _form()
        sc = NodeCombo(self.ctx, npc, npc.start_node, allow_none=False)

        def set_start(v):
            npc.start_node = v
            self.changed(True)
        sc.picked.connect(set_start)
        f.addRow("Стартовая реплика:", sc)
        af = QCheckBox("Камера поворачивается к NPC")
        af.setChecked(npc.auto_face_npc)

        def set_af(v):
            npc.auto_face_npc = bool(v)
            self.changed(False)
        af.toggled.connect(set_af)
        f.addRow("", af)
        gl.addLayout(f)
        rb = QPushButton("Повторный визит…")
        rb.clicked.connect(lambda: self.navigate.emit(("returning",)))
        gl.addWidget(rb)
        lay.addWidget(g)

        # where on maps
        g, gl = _group("На картах")
        if ents:
            for e in ents:
                l = QLabel("<b>%s</b>: %s%s%s" % (e.map, e.classname,
                           (" · «%s»" % e.display) if e.display else "",
                           (" · %s" % e.model) if e.model else ""))
                l.setWordWrap(True)
                gl.addWidget(l)
        elif self.ctx.mapindex.maps:
            gl.addWidget(hint("Не найден ни на одной из %d карт. Поставьте NPC с targetname «%s» в Hammer." %
                              (len(self.ctx.mapindex.maps), npc.targetname)))
        else:
            gl.addWidget(hint("Папки карт (.vmf) не заданы — Проект → Настройки."))
        lay.addWidget(g)

        # statistics
        g, gl = _group("Сводка")
        nopt = sum(len(n.options) for n in npc.nodes)
        rows = ["Реплик: %d, вариантов ответа: %d" % (len(npc.nodes), nopt)]
        for lg in p.languages:
            nt = sum(1 for n in npc.nodes if n.line.get(lg))
            nv = sum(1 for n in npc.nodes if n.voice.get(lg) and not n.voice[lg].is_empty())
            rows.append("%s: текст %d/%d, озвучка %d/%d" % (LANGUAGE_NAMES.get(lg, lg), nt, len(npc.nodes), nv, len(npc.nodes)))
        qs = [q.id for q in p.quests if q.owner == npc.targetname]
        if qs:
            rows.append("Квесты в файле NPC: " + ", ".join(qs))
        gl.addWidget(QLabel("\n".join(rows)))
        files = QLabel("Файлы игры:\nscripts/dialogs/%s.txt\nsound/dialogs/%s/<язык>/\nscenes/dialogs/%s/<язык>/" %
                       ((npc.targetname,) * 3))
        files.setObjectName("hint")
        files.setWordWrap(True)
        files.setTextInteractionFlags(Qt.TextSelectableByMouse)
        gl.addWidget(files)
        lay.addWidget(g)

        g, gl = _group("Заметка")
        note = AutoText(npc.note, 2, 6)

        def set_note(t):
            npc.note = t
            self.changed(False)
        note.edited.connect(set_note)
        gl.addWidget(note)
        lay.addWidget(g)

    # --- node ----------------------------------------------------------------------------------
    def node_page(self, npc, n):
        p = self.ctx.project
        w, lay = self._page("Реплика NPC  #%d%s" % (n.id, "  · СТАРТ" if n.id == npc.start_node else ""))
        top = QHBoxLayout()
        idsp = QSpinBox()
        idsp.setRange(0, 99999)
        idsp.setValue(n.id)
        ren = QPushButton("Сменить номер")

        def renumber():
            new = idsp.value()
            if new == n.id:
                return
            if npc.node(new):
                QMessageBox.warning(self, "Номер", "Реплика #%d уже есть" % new)
                return
            self.ctx.renumber_node(npc, n.id, new)
        ren.clicked.connect(renumber)
        top.addWidget(QLabel("Номер:"))
        top.addWidget(idsp)
        top.addWidget(ren)
        top.addStretch(1)
        if n.id != npc.start_node:
            sb = QPushButton("Сделать стартовой")

            def mk_start():
                npc.start_node = n.id
                self.changed(True)
                self.show_target(self.target)
            sb.clicked.connect(mk_start)
            top.addWidget(sb)
        lay.addLayout(top)

        g, gl = _group("Что говорит NPC")
        lt = LangTexts(self.ctx, n.line, n.voice, multiline=True, placeholder="реплика NPC")
        lt.changed.connect(lambda: self.changed(False))
        gl.addWidget(lt)
        lay.addWidget(g)

        g, gl = _group("Что дальше")
        mode = QButtonGroup(w)
        r_opts = QRadioButton("Варианты ответа игрока (%d)" % len(n.options))
        r_auto = QRadioButton("Сразу следующая реплика:")
        r_end = QRadioButton("Конец диалога")
        for i, r in enumerate((r_opts, r_auto, r_end)):
            mode.addButton(r, i)
        kind = n.flow_kind()
        {"options": r_opts, "auto": r_auto, "end": r_end, "none": r_end if n.end else r_opts}[kind].setChecked(True)
        gl.addWidget(r_opts)
        # option list
        ol = QListWidget()
        ol.setMaximumHeight(170)
        for i, o in enumerate(n.options):
            t, fb = text_for(o.text, self.ctx.display_lang, p.base_language)
            it = QListWidgetItem("%d. %s" % (i + 1, t or "(нет текста)"))
            ol.addItem(it)
        ol.itemDoubleClicked.connect(lambda it: self.navigate.emit(("option", n.id, ol.row(it))))
        gl.addWidget(ol)
        bl = QHBoxLayout()
        add = QPushButton("+ вариант")
        add.clicked.connect(lambda: self.ctx.add_option(npc, n))
        up = QToolButton(text="▲")
        dn = QToolButton(text="▼")
        de = QToolButton(text="✕")
        de.setToolTip("Удалить выбранный вариант")
        edit = QPushButton("Открыть")

        def move(d):
            i = ol.currentRow()
            j = i + d
            if 0 <= i < len(n.options) and 0 <= j < len(n.options):
                n.options[i], n.options[j] = n.options[j], n.options[i]
                self.changed(True)
                self.show_target(self.target)

        def delete():
            i = ol.currentRow()
            if 0 <= i < len(n.options):
                del n.options[i]
                self.changed(True)
                self.show_target(self.target)
        up.clicked.connect(lambda: move(-1))
        dn.clicked.connect(lambda: move(1))
        de.clicked.connect(delete)
        edit.clicked.connect(lambda: ol.currentRow() >= 0 and self.navigate.emit(("option", n.id, ol.currentRow())))
        for b in (add, edit, up, dn, de):
            bl.addWidget(b)
        bl.addStretch(1)
        gl.addLayout(bl)
        ar = QHBoxLayout()
        ar.addWidget(r_auto)
        ac = NodeCombo(self.ctx, npc, n.auto_next)
        ar.addWidget(ac, 1)
        gl.addLayout(ar)
        gl.addWidget(r_end)
        sp = QCheckBox("После неё NPC больше не заговорит")
        sp.setChecked(n.spoils)
        gl.addWidget(sp)

        def upd_mode():
            m = mode.checkedId()
            ol.setEnabled(m == 0)
            add.setEnabled(m == 0)
            ac.setEnabled(m == 1)

        def apply_mode(_=None):
            m = mode.checkedId()
            if m == 1:
                n.end = False
                if n.options and QMessageBox.question(self, "Варианты", "Удалить варианты ответа (%d)? Без этого «далее» не работает."
                                                      % len(n.options)) == QMessageBox.Yes:
                    n.options = []
                n.auto_next = int(ac.currentData())
            elif m == 2:
                n.end = True
                n.auto_next = -1
                if n.options and QMessageBox.question(self, "Варианты", "Удалить варианты ответа (%d)?" % len(n.options)) == QMessageBox.Yes:
                    n.options = []
            else:
                n.end = False
                n.auto_next = -1
                if not n.options:
                    n.options.append(Option())
            upd_mode()
            self.changed(True)
            self.show_target(self.target)
        mode.idClicked.connect(apply_mode)

        def set_auto(v):
            n.auto_next = v
            n.end = False
            self.changed(True)
        ac.picked.connect(set_auto)

        def set_sp(v):
            n.spoils = bool(v)
            self.changed(True)
        sp.toggled.connect(set_sp)
        upd_mode()
        lay.addWidget(g)

        g, gl = _group("Анимация и время")
        f = _form()
        seq = completer_line(n.sequence, COMMON_SEQUENCES, "анимация NPC (sequence), необязательно")

        def set_seq():
            n.sequence = seq.text().strip()
            self.changed(False)
        seq.editingFinished.connect(set_seq)
        f.addRow("Анимация:", seq)
        ss = completer_line(n.server_sequence, self.ctx.mapindex.targetnames(), "targetname scripted_sequence на карте")

        def set_ss():
            n.server_sequence = ss.text().strip()
            self.changed(False)
        ss.editingFinished.connect(set_ss)
        f.addRow("Запустить на карте:", ss)
        dl = QHBoxLayout()
        auto = QCheckBox("авто (после реплики)")
        auto.setChecked(n.option_delay == "auto")
        ds = QDoubleSpinBox()
        ds.setRange(0, 60)
        ds.setDecimals(2)
        ds.setSuffix(" с")
        try:
            ds.setValue(float(n.option_delay) if n.option_delay != "auto" else 0.0)
        except ValueError:
            ds.setValue(0.0)
        ds.setEnabled(n.option_delay != "auto")

        def set_delay(*_):
            n.option_delay = "auto" if auto.isChecked() else ("%g" % ds.value())
            ds.setEnabled(not auto.isChecked())
            self.changed(False)
        auto.toggled.connect(set_delay)
        ds.valueChanged.connect(set_delay)
        dl.addWidget(auto)
        dl.addWidget(ds)
        f.addRow("Варианты появятся:", dl)
        gl.addLayout(f)
        lay.addWidget(g)

        g, gl = _group("Заметка")
        note = AutoText(n.note, 2, 6)

        def set_note(t):
            n.note = t
            self.changed(False)
        note.edited.connect(set_note)
        gl.addWidget(note)
        lay.addWidget(g)

        # incoming links
        inc = []
        for x in npc.nodes:
            if x.auto_next == n.id:
                inc.append(("#%d «далее»" % x.id, ("node", x.id)))
            for i, o in enumerate(x.options):
                if o.next == n.id or (o.has_after() and o.after_next == n.id):
                    inc.append(("#%d вариант %d" % (x.id, i + 1), ("option", x.id, i)))
        g, gl = _group("Сюда ведут (%d)" % len(inc))
        if not inc and n.id != npc.start_node:
            gl.addWidget(hint("Ничего — реплика недостижима."))
        for label, tgt in inc:
            b = QPushButton(label)
            b.setFlat(True)
            b.setStyleSheet("text-align:left; color:#7fb6ff;")
            b.clicked.connect(lambda _=False, t=tgt: self.navigate.emit(t))
            gl.addWidget(b)
        lay.addWidget(g)
        dele = QPushButton("Удалить реплику #%d" % n.id)
        dele.clicked.connect(lambda: self.ctx.delete_nodes([n.id]))
        lay.addWidget(dele)
        return w, lay

    # --- option --------------------------------------------------------------------------------
    def option_page(self, npc, n, oi):
        p = self.ctx.project
        o = n.options[oi]
        w, lay = self._page("Ответ игрока  #%d · вариант %d" % (n.id, oi + 1))
        back = QPushButton("← к реплике #%d" % n.id)
        back.setFlat(True)
        back.setStyleSheet("text-align:left; color:#7fb6ff;")
        back.clicked.connect(lambda: self.navigate.emit(("node", n.id)))
        lay.addWidget(back)

        g, gl = _group("Текст ответа (до 255 байт)")
        lt = LangTexts(self.ctx, o.text, None, multiline=True, byte_limit=255, placeholder="что отвечает игрок")
        lt.changed.connect(lambda: self.changed(False))
        gl.addWidget(lt)
        lay.addWidget(g)

        g, gl = _group("Куда ведёт")
        f = _form()
        nc = NodeCombo(self.ctx, npc, o.next, none_label="— никуда —")

        def set_next(v):
            o.next = v
            self.changed(True)
            self.show_target(self.target)
        nc.picked.connect(set_next)
        f.addRow("Следующая реплика:", nc)
        cl = QCheckBox("Закрыть диалог")
        cl.setChecked(o.close)
        cl.setEnabled(o.next == -1)

        def set_close(v):
            o.close = bool(v)
            self.changed(True)
        cl.toggled.connect(set_close)
        f.addRow("", cl)
        sp = QCheckBox("NPC больше не заговорит")
        sp.setChecked(o.spoils)

        def set_sp(v):
            o.spoils = bool(v)
            self.changed(True)
        sp.toggled.connect(set_sp)
        f.addRow("", sp)
        ss = completer_line(o.server_sequence, self.ctx.mapindex.targetnames(), "targetname scripted_sequence / logic")

        def set_ss():
            o.server_sequence = ss.text().strip()
            self.changed(True)
        ss.editingFinished.connect(set_ss)
        f.addRow("Запустить на карте:", ss)
        gl.addLayout(f)
        lay.addWidget(g)

        g, gl = _group("Квест")
        f = _form()
        for label, attr, tip in (("Выдать:", "grant", "игрок получает квест (вариант скрывается, когда квест уже взят)"),
                                 ("Завершить:", "complete", "квест засчитывается, игрок получает награду"),
                                 ("Провалить:", "fail", "квест проваливается")):
            qc = QuestCombo(self.ctx, getattr(o, attr))
            qc.setToolTip(tip)

            def setq(v, a=attr):
                setattr(o, a, v)
                self.changed(True)
                self.show_target(self.target)
            qc.picked.connect(setq)
            row = QHBoxLayout()
            row.addWidget(qc, 1)
            if attr == "grant":
                nb = QToolButton(text="+ новый")
                nb.setToolTip("Создать новый квест и выдавать его этим ответом")
                nb.clicked.connect(lambda: self.ctx.new_quest(assign=(o, "grant")))
                row.addWidget(nb)
            f.addRow(label, row)
        srow = QHBoxLayout()
        sq = QuestCombo(self.ctx, o.set_stage[0] if o.set_stage else "")
        sn = QSpinBox()
        sn.setRange(0, 50)
        sn.setValue(o.set_stage[1] if o.set_stage else 0)
        sn.setPrefix("этап ")

        def set_stage(*_):
            q = sq.currentData()
            o.set_stage = [q, sn.value()] if q else None
            self.changed(True)
        sq.picked.connect(set_stage)
        sn.valueChanged.connect(set_stage)
        srow.addWidget(sq, 1)
        srow.addWidget(sn)
        f.addRow("Поставить этап:", srow)
        gl.addLayout(f)
        if o.complete:
            ag, al = _group("После сдачи квеста")
            al.addWidget(hint("Работает, если «Следующая реплика» не задана."))
            af = _form()
            anc = NodeCombo(self.ctx, npc, o.after_next, none_label="— своя реплика ниже —")

            def set_after(v):
                o.after_next = v
                self.changed(True)
                self.show_target(self.target)
            anc.picked.connect(set_after)
            af.addRow("Перейти к реплике:", anc)
            al.addLayout(af)
            if o.after_next == -1:
                al.addWidget(hint("Или NPC скажет эту реплику (варианты текущей реплики останутся):"))
                at = LangTexts(self.ctx, o.after_line, o.after_voice, multiline=True, placeholder="реплика после сдачи квеста")
                at.changed.connect(lambda: self.changed(True))
                al.addWidget(at)
            gl.addWidget(ag)
        lay.addWidget(g)

        g, gl = _group("Условия показа")
        gl.addWidget(hint("Вариант скрыт, пока условие не выполнено."))
        f = _form()
        rrow = QHBoxLayout()
        rc = QCheckBox("репутация ≥")
        rc.setChecked(o.require_rep is not None)
        rs = QSpinBox()
        rs.setRange(-1000, 1000)
        rs.setValue(o.require_rep or 0)
        rs.setEnabled(o.require_rep is not None)

        def set_rep(*_):
            o.require_rep = rs.value() if rc.isChecked() else None
            rs.setEnabled(rc.isChecked())
            self.changed(True)
        rc.toggled.connect(set_rep)
        rs.valueChanged.connect(set_rep)
        rrow.addWidget(rc)
        rrow.addWidget(rs)
        rrow.addStretch(1)
        f.addRow("Репутация:", rrow)
        strow = QHBoxLayout()
        stq = QuestCombo(self.ctx, o.require_state[0] if o.require_state else "", none_label="— не важно —")
        sts = QComboBox()
        for s in QUEST_STATES:
            sts.addItem(QUEST_STATE_NAMES[s], s)
        sts.setCurrentIndex(max(0, sts.findData(o.require_state[1] if o.require_state else "inactive")))

        def set_state(*_):
            q = stq.currentData()
            o.require_state = [q, sts.currentData()] if q else None
            self.changed(True)
        stq.picked.connect(set_state)
        sts.currentIndexChanged.connect(set_state)
        strow.addWidget(stq, 1)
        strow.addWidget(QLabel("—"))
        strow.addWidget(sts)
        f.addRow("Квест в состоянии:", strow)
        sgrow = QHBoxLayout()
        sgq = QuestCombo(self.ctx, o.require_stage[0] if o.require_stage else "", none_label="— не важно —")
        sgn = QSpinBox()
        sgn.setRange(0, 50)
        sgn.setPrefix("этап ≥ ")
        sgn.setValue(o.require_stage[1] if o.require_stage else 0)

        def set_rstage(*_):
            q = sgq.currentData()
            o.require_stage = [q, sgn.value()] if q else None
            self.changed(True)
        sgq.picked.connect(set_rstage)
        sgn.valueChanged.connect(set_rstage)
        sgrow.addWidget(sgq, 1)
        sgrow.addWidget(sgn)
        f.addRow("Квест активен и:", sgrow)
        gl.addLayout(f)
        gl.addWidget(hint("Вариант, выдающий квест, сам скрывается, когда квест уже взят или выполнен."))
        lay.addWidget(g)

        g, gl = _group("Заметка")
        note = AutoText(o.note, 2, 5)

        def set_note(t):
            o.note = t
            self.changed(False)
        note.edited.connect(set_note)
        gl.addWidget(note)
        lay.addWidget(g)
        bl = QHBoxLayout()
        de = QPushButton("Удалить вариант")

        def delete():
            del n.options[oi]
            self.changed(True)
            self.navigate.emit(("node", n.id))
        de.clicked.connect(delete)
        bl.addWidget(de)
        bl.addStretch(1)
        lay.addLayout(bl)
        return w, lay

    # --- returning ------------------------------------------------------------------------------
    def returning_page(self, npc):
        r = npc.returning
        w, lay = self._page("Повторный визит  ↺",
                            "Когда с NPC уже поговорили до конца, при следующем разговоре вместо стартовой реплики он скажет эту. "
                            "Пустой текст — повторного визита нет, разговор начинается с начала.")
        g, gl = _group("Что говорит NPC")
        lt = LangTexts(self.ctx, r.line, r.voice, multiline=True, placeholder="например: Опять ты?")
        lt.changed.connect(lambda: self.changed(True))
        gl.addWidget(lt)
        lay.addWidget(g)
        g, gl = _group("Что дальше")
        bg = QButtonGroup(w)
        r1 = QRadioButton("Показать варианты ответа реплики:")
        r2 = QRadioButton("Без вариантов: после реплики")
        bg.addButton(r1, 0)
        bg.addButton(r2, 1)
        (r2 if r.no_options else r1).setChecked(True)
        row1 = QHBoxLayout()
        row1.addWidget(r1)
        nc1 = NodeCombo(self.ctx, npc, r.next if not r.no_options else -1, none_label="— стартовой (по цепочке «далее») —")
        row1.addWidget(nc1, 1)
        gl.addLayout(row1)
        gl.addWidget(hint("Сейчас будут показаны варианты реплики #%d." % npc.returning_target()) if not r.no_options else hint(""))
        row2 = QHBoxLayout()
        row2.addWidget(r2)
        nc2 = NodeCombo(self.ctx, npc, r.next if r.no_options else -1, none_label="— закрыть диалог —")
        row2.addWidget(nc2, 1)
        gl.addLayout(row2)

        def apply(*_):
            r.no_options = bg.checkedId() == 1
            if r.no_options:
                r.next = int(nc2.currentData())
                r.auto_close = r.next == -1
            else:
                r.next = int(nc1.currentData())
                r.auto_close = False
            nc1.setEnabled(not r.no_options)
            nc2.setEnabled(r.no_options)
            self.changed(True)
        bg.idClicked.connect(apply)
        nc1.picked.connect(apply)
        nc2.picked.connect(apply)
        nc1.setEnabled(not r.no_options)
        nc2.setEnabled(r.no_options)
        lay.addWidget(g)
        return w, lay

    # --- quest ------------------------------------------------------------------------------------
    def quest_page(self, qid):
        p = self.ctx.project
        q = p.quest(qid)
        if q is None:
            return self._page("Квест не найден")
        w, lay = self._page("Квест  %s" % q.id, "Квесты общие для всей игры: выдать/сдать квест может любой NPC. "
                                                 "Записывается в файл диалога NPC-владельца.")
        f = _form()
        idl = QLineEdit(q.id)

        def rename():
            new = idl.text().strip()
            if new != q.id:
                if not new or any(c in new for c in ' "|;~:') or p.quest(new):
                    QMessageBox.warning(self, "id квеста", "Пустой, с пробелом/: ; | ~ или уже существует")
                    idl.setText(q.id)
                    return
                self.ctx.rename_quest(q, new)
        idl.editingFinished.connect(rename)
        f.addRow("id:", idl)
        own = QComboBox()
        for n in p.npcs:
            own.addItem(n.targetname, n.targetname)
        own.setCurrentIndex(max(0, own.findData(q.owner)))

        def set_owner(_):
            q.owner = own.currentData()
            self.changed(False)
        own.currentIndexChanged.connect(set_owner)
        f.addRow("В файле NPC:", own)
        mr = QSpinBox()
        mr.setRange(-1000, 1000)
        mr.setValue(q.min_rep)

        def set_mr(v):
            q.min_rep = v
            self.changed(False)
        mr.valueChanged.connect(set_mr)
        f.addRow("Мин. репутация:", mr)
        names = self.ctx.mapindex.targetnames(npc_only=True)
        fk = completer_line(";".join(q.fail_on_kill), names, "targetname через ;  — убийство проваливает квест")

        def set_fk():
            q.fail_on_kill = [t.strip() for t in fk.text().split(";") if t.strip()]
            self.changed(False)
        fk.editingFinished.connect(set_fk)
        f.addRow("Провал, если убит:", fk)
        fp = QSpinBox()
        fp.setRange(-1000, 0)
        fp.setValue(q.fail_rep_penalty)

        def set_fp(v):
            q.fail_rep_penalty = v
            self.changed(False)
        fp.valueChanged.connect(set_fp)
        f.addRow("Штраф репутации:", fp)
        lay.addLayout(f)

        g, gl = _group("Название")
        t = LangTexts(self.ctx, q.title, multiline=False)
        t.changed.connect(lambda: self.changed(False))
        gl.addWidget(t)
        lay.addWidget(g)
        g, gl = _group("Описание")
        d = LangTexts(self.ctx, q.desc, multiline=True)
        d.changed.connect(lambda: self.changed(False))
        gl.addWidget(d)
        lay.addWidget(g)

        g, gl = _group("Этапы (%d)" % len(q.stages))
        for si, s in enumerate(q.stages):
            sg, sl = _group("Этап %d" % si)
            st = LangTexts(self.ctx, s.title, multiline=False, placeholder="название этапа")
            st.changed.connect(lambda: self.changed(False))
            sl.addWidget(st)
            sd = LangTexts(self.ctx, s.desc, multiline=True, placeholder="что сделать")
            sd.changed.connect(lambda: self.changed(False))
            sl.addWidget(sd)
            sf = _form()
            kt = completer_line(";".join(s.kill_targets), names, "targetname через ;  (пусто — этап без убийств)")
            kc = QSpinBox()
            kc.setRange(0, 999)
            kc.setValue(s.kill_count)

            def set_kill(*_, s=s, kt=kt, kc=kc):
                s.kill_targets = [t.strip() for t in kt.text().split(";") if t.strip()]
                s.kill_count = kc.value()
                self.changed(False)
            kt.editingFinished.connect(set_kill)
            kc.valueChanged.connect(set_kill)
            sf.addRow("Убить:", kt)
            sf.addRow("Сколько:", kc)
            sl.addLayout(sf)
            sl.addWidget(hint("Этап с целями переключается на следующий сам, когда цели убиты."))
            rb = QPushButton("Удалить этап %d" % si)

            def rm(_=False, si=si):
                del q.stages[si]
                self.changed(False)
                self.show_target(self.target)
            rb.clicked.connect(rm)
            sl.addWidget(rb)
            gl.addWidget(sg)
        ab = QPushButton("+ этап")

        def add_stage():
            q.stages.append(Stage())
            self.changed(False)
            self.show_target(self.target)
        ab.clicked.connect(add_stage)
        gl.addWidget(ab)
        lay.addWidget(g)

        g, gl = _group("Награда")
        tbl = QTableWidget(len(q.rewards), 2)
        tbl.setHorizontalHeaderLabels(["Предмет (classname)", "Кол-во"])
        tbl.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        tbl.verticalHeader().setVisible(False)
        tbl.setMaximumHeight(36 + 30 * max(2, len(q.rewards)))
        items = self.ctx.item_names()

        def rebuild_rewards():
            q.rewards = []
            for r in range(tbl.rowCount()):
                cb = tbl.cellWidget(r, 0)
                sp = tbl.cellWidget(r, 1)
                name = cb.currentText().strip()
                if name:
                    q.rewards.append([name, sp.value()])
            self.changed(False)
        for r, (item, amount) in enumerate(q.rewards):
            cb = QComboBox()
            cb.setEditable(True)
            cb.addItems(items)
            cb.setCurrentText(item)
            cb.currentTextChanged.connect(lambda *_: rebuild_rewards())
            sp = QSpinBox()
            sp.setRange(1, 999)
            sp.setValue(int(amount))
            sp.valueChanged.connect(lambda *_: rebuild_rewards())
            tbl.setCellWidget(r, 0, cb)
            tbl.setCellWidget(r, 1, sp)
        gl.addWidget(tbl)
        rl = QHBoxLayout()
        addr = QPushButton("+ предмет")

        def add_reward():
            q.rewards.append(["item_healthkit", 1])
            self.changed(False)
            self.show_target(self.target)

        def del_reward():
            r = tbl.currentRow()
            if 0 <= r < len(q.rewards):
                del q.rewards[r]
                self.changed(False)
                self.show_target(self.target)
        addr.clicked.connect(add_reward)
        delr = QPushButton("− выбранный")
        delr.clicked.connect(del_reward)
        rl.addWidget(addr)
        rl.addWidget(delr)
        rl.addStretch(1)
        gl.addLayout(rl)
        lay.addWidget(g)

        # usages
        uses = []
        for npc in p.npcs:
            for n in npc.nodes:
                for i, o in enumerate(n.options):
                    kinds = []
                    if o.grant == q.id:
                        kinds.append("выдаёт")
                    if o.complete == q.id:
                        kinds.append("завершает")
                    if o.fail == q.id:
                        kinds.append("проваливает")
                    if o.set_stage and o.set_stage[0] == q.id:
                        kinds.append("этап")
                    if (o.require_state and o.require_state[0] == q.id) or (o.require_stage and o.require_stage[0] == q.id):
                        kinds.append("условие")
                    if kinds:
                        uses.append(("%s #%d вариант %d: %s" % (npc.targetname, n.id, i + 1, ", ".join(kinds)),
                                     (npc.targetname, ("option", n.id, i))))
        g, gl = _group("Где используется (%d)" % len(uses))
        for label, (tn, tgt) in uses:
            b = QPushButton(label)
            b.setFlat(True)
            b.setStyleSheet("text-align:left; color:#7fb6ff;")
            b.clicked.connect(lambda _=False, tn=tn, tgt=tgt: self.ctx.goto(tn, tgt))
            gl.addWidget(b)
        if not uses:
            gl.addWidget(hint("Пока ни один ответ не выдаёт этот квест."))
        lay.addWidget(g)
        de = QPushButton("Удалить квест")
        de.clicked.connect(lambda: self.ctx.delete_quest(q))
        lay.addWidget(de)
        return w, lay
