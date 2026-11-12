"""Terminals in the "Logic" tab (M6.5): the inspector page of a terminal - style, title,
welcome text, the lock (hacking with the word game of Fallout, a password item), the
tree of entries (records, commands, submenus) with conditions and actions, and a preview
of the screen as the game draws it (game/client/hl2rpm_terminal_ui.cpp)."""
import re

from PySide6.QtCore import QRectF, Qt
from PySide6.QtGui import QColor, QFont, QFontMetrics, QPainter
from PySide6.QtWidgets import (QCheckBox, QComboBox, QHBoxLayout, QInputDialog, QLabel, QLineEdit, QMessageBox,
                               QPushButton, QSpinBox, QToolButton, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget)

from .. import logic
from .. import termdata as td
from .logic_widgets import LogicList, _Combo, choices
from .widgets import AutoText, LangTexts, hint

ID_RE = re.compile(r"^[A-Za-z0-9_\-]+$")


def _group(title):
    from .inspector import _group as g
    return g(title)


def _form():
    from .inspector import _form as f
    return f()


def difficulty(level):
    if level <= 0:
        return "открыт"
    if level < 13:
        return "очень легко"
    if level < 38:
        return "легко"
    if level < 63:
        return "средне"
    if level < 88:
        return "сложно"
    return "очень сложно"


class TerminalPreview(QWidget):
    """The screen of the terminal like in the game: the menu of a list or a record."""

    def __init__(self, ctx, term):
        super().__init__()
        self.ctx = ctx
        self.term = term
        self.entries = term.entries      # the list shown
        self.selected = None             # entry highlighted / read
        self.setMinimumHeight(300)

    def show_entry(self, entries, entry):
        self.entries = entries
        self.selected = entry
        self.update()

    def paintEvent(self, ev):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        t = self.term
        pr = self.ctx.project
        lang = getattr(self.ctx, "display_lang", pr.base_language)
        base = pr.base_language
        combine = t.style == "combine"
        bg = QColor(2, 10, 16) if combine else QColor(4, 14, 6)
        fg = QColor(120, 225, 240) if combine else QColor(110, 250, 130)
        dim = QColor(60, 140, 160) if combine else QColor(60, 160, 80)
        hi_text = QColor(2, 14, 20) if combine else QColor(4, 18, 8)
        r = self.rect()
        # the screen keeps 4:3, centered
        w = min(r.width(), int(r.height() * 4 / 3))
        h = int(w * 3 / 4)
        x0, y0 = (r.width() - w) // 2, (r.height() - h) // 2
        p.fillRect(x0 - 8, y0 - 8, w + 16, h + 16, QColor(40, 38, 34) if not combine else QColor(20, 24, 30))
        p.fillRect(x0, y0, w, h, bg)
        if combine:
            p.setPen(QColor(40, 110, 130, 40))
            for gx in range(x0, x0 + w, 18):
                p.drawLine(gx, y0, gx, y0 + h)
            for gy in range(y0, y0 + h, 18):
                p.drawLine(x0, gy, x0 + w, gy)
        font = QFont("Lucida Console")
        font.setStyleHint(QFont.Monospace)
        font.setPixelSize(max(9, h // 26))
        p.setFont(font)
        fm = QFontMetrics(font)
        lh = fm.height() + 2
        cols = max(10, (w - 20) // max(1, fm.horizontalAdvance("M")))
        x, y = x0 + 10, y0 + 8 + fm.ascent()

        def line(s, color=fg, sel=False):
            nonlocal y
            if sel:
                p.fillRect(x - 3, y - fm.ascent() - 1, fm.horizontalAdvance(s) + 6, lh, fg)
                p.setPen(hi_text)
            else:
                p.setPen(color)
            p.drawText(x, y, s)
            y += lh

        def wrap(s):
            out = []
            for para in (s or "").replace("\\n", "\n").split("\n"):
                cur = ""
                for word in para.split(" "):
                    if len(cur) + len(word) + 1 > cols:
                        out.append(cur)
                        cur = word
                    else:
                        cur = (cur + " " + word) if cur else word
                out.append(cur)
            return out

        title = t.title.get(lang) or t.title.get(base) or ("OVERWATCH" if combine else "ТЕРМИНАЛ")
        if combine:
            p.fillRect(x0, y0, w, lh + 8, QColor(8, 30, 44))
            line("СЕТЬ НАДЗОРА // " + title)
            y += 6
        else:
            line("ТЕРМИНАЛ ГОРСЕТИ · ВЕРСИЯ 2.11")
            line(title)
        header = t.header.get(lang) or t.header.get(base) or ""
        for s in wrap(header)[:4]:
            line(s)
        y += lh // 2
        sel = self.selected
        if sel is not None and sel.kind() == "text":
            line(sel.label(lang, base))
            p.setPen(dim)
            p.drawLine(x, y - lh + 4, x0 + w - 10, y - lh + 4)
            for s in wrap(sel.text.get(lang) or sel.text.get(base) or ""):
                if y > y0 + h - lh * 2:
                    break
                line(s)
            y = y0 + h - 8 - lh + fm.ascent()
            line("[< Назад]", sel=True)
        else:
            for e in self.entries:
                cond = "  (если …)" if e.conditions else ""
                line("[%s]%s" % (e.label(lang, base), cond), sel=e is sel)
            if t.lock_level:
                p.setPen(dim)
                p.drawText(x, y0 + h - 8, "взлом: %s (%d), навык %s" % (difficulty(t.lock_level), t.lock_level,
                                                                          logic.SKILL_NAMES.get(t.lock_skill, t.lock_skill)))
        p.end()


def terminal_page(insp, t):
    ctx = insp.ctx
    p = ctx.project
    w, lay = insp._page("Терминал  %s" % t.id,
                        "Компьютер или консоль Альянса: в игре по E камера подъезжает к экрану, интерфейс — на самом "
                        "экране. Записи читаются (лор, дневники, приказы), команды выполняют действия (двери, силовые "
                        "поля, турели, любые действия CK), подменю. Замок — взлом словами как в Fallout. На карту — "
                        "объект «Терминал» (палитра / ПКМ по карте → Поставить), в его свойствах выбрать этот терминал.")

    def changed(structural=False):
        insp.changed(structural)
        ctx.refresh_logic_list()
        preview.update()

    f = _form()
    idl = QLineEdit(t.id)

    def rename():
        new = idl.text().strip()
        if new == t.id:
            return
        if not new or not ID_RE.match(new) or p.terminal(new):
            QMessageBox.warning(insp, "id терминала", "Пустой, не латиница или уже существует")
            idl.setText(t.id)
            return
        old = t.id
        t.id = new
        for layer in p.maps.values():
            for r in layer.refs:
                if r.type == "terminal" and r.terminal == old:
                    r.terminal = new
        changed(True)
        insp.show_target(("terminal", new))
    idl.editingFinished.connect(rename)
    f.addRow("id:", idl)

    st = QComboBox()
    for sid, label in td.STYLES:
        st.addItem(label, sid)
    st.setCurrentIndex(max(0, st.findData(t.style)))

    def set_style(_):
        t.style = st.currentData()
        changed()
    st.currentIndexChanged.connect(set_style)
    f.addRow("Вид:", st)
    lay.addLayout(f)

    g, gl = _group("Название (строка вверху экрана)")
    tt = LangTexts(ctx, t.title, None, multiline=False, placeholder="Терминал смотрителя")
    tt.changed.connect(lambda: changed())
    gl.addWidget(tt)
    lay.addWidget(g)
    g, gl = _group("Приветствие (под названием, до 4 строк)")
    ht = LangTexts(ctx, t.header, None, multiline=True, placeholder="Добро пожаловать…")
    ht.changed.connect(lambda: changed())
    gl.addWidget(ht)
    lay.addWidget(g)

    # the lock
    g, gl = _group("Замок")
    lf = _form()
    lv = QSpinBox()
    lv.setRange(0, 100)
    lv.setValue(int(t.lock_level))
    dl = QLabel(difficulty(t.lock_level))
    row = QHBoxLayout()
    row.addWidget(lv)
    row.addWidget(dl)
    row.addStretch(1)

    def set_level(v):
        t.lock_level = int(v)
        dl.setText(difficulty(v))
        changed()
    lv.valueChanged.connect(set_level)
    lf.addRow("Сложность взлома:", row)
    sk = _Combo(list(logic.SKILLS), t.lock_skill or "science", editable=False)

    def set_skill(*_):
        t.lock_skill = sk.value() or "science"
        changed()
    sk.currentIndexChanged.connect(set_skill)
    lf.addRow("Навык:", sk)
    pw = _Combo(choices(ctx, "item"), t.password_item, editable=True, empty_label="— нет —")

    def set_pw(*_):
        t.password_item = pw.value()
        changed()
    pw.currentIndexChanged.connect(set_pw)
    pw.lineEdit().editingFinished.connect(set_pw)
    lf.addRow("Пароль (предмет):", pw)
    gl.addLayout(lf)
    gl.addWidget(hint("0 — открыт. Иначе слова-пароли длиной от 4 (лёгкий) до 12 букв (очень сложный); попыток 4 "
                      "(3, если навык ниже сложности, 5 — если выше на 25); пытаться можно с навыком от «сложность − 25». "
                      "Скобки (), [], {}, <> в «мусоре» убирают ложное слово или возвращают попытки. Неудача — блокировка "
                      "навсегда. Предмет-пароль (записка) у игрока открывает сразу. Опыт за взлом — правило xp_hack."))
    lay.addWidget(g)

    # entries
    g, gl = _group("Записи")
    tree = QTreeWidget()
    tree.setHeaderLabels(["Запись", "Тип"])
    tree.setColumnWidth(0, 230)
    tree.setMinimumHeight(160)
    gl.addWidget(tree)
    bl = QHBoxLayout()
    editor_box = QVBoxLayout()
    state = {"entry": None, "list": t.entries}
    lang = getattr(ctx, "display_lang", p.base_language)
    kinds = {"text": "запись", "action": "команда", "menu": "подменю"}

    def parent_list(entry):
        def walk(lst):
            for e in lst:
                if e is entry:
                    return lst
                r = walk(e.entries)
                if r is not None:
                    return r
            return None
        return walk(t.entries)

    def fill(sel=None):
        tree.blockSignals(True)
        tree.clear()

        def add(parent, lst):
            for e in lst:
                it = QTreeWidgetItem([e.label(lang, p.base_language), kinds[e.kind()] + (" · раз" if e.once else "")])
                it.setData(0, Qt.UserRole, id(e))
                if parent is None:
                    tree.addTopLevelItem(it)
                else:
                    parent.addChild(it)
                add(it, e.entries)
                if e is sel:
                    tree.setCurrentItem(it)
        add(None, t.entries)
        tree.expandAll()
        tree.blockSignals(False)

    def find(eid):
        for e in t.all_entries():
            if id(e) == eid:
                return e
        return None

    def clear_editor():
        while editor_box.count():
            it = editor_box.takeAt(0)
            if it.widget():
                it.widget().deleteLater()

    def show_editor(e):
        clear_editor()
        state["entry"] = e
        lst = parent_list(e) if e else t.entries
        preview.show_entry(lst if lst is not None else t.entries, e)
        if e is None:
            return
        box = QWidget()
        bl2 = QVBoxLayout(box)
        bl2.setContentsMargins(0, 0, 0, 0)
        bl2.addWidget(QLabel("<b>Название в меню</b>"))
        et = LangTexts(ctx, e.title, None, multiline=False, placeholder="Дневник: день 41")
        et.changed.connect(lambda: (changed(), fill(e)))
        bl2.addWidget(et)
        if not e.entries:
            bl2.addWidget(QLabel("<b>Текст записи</b> (пусто — это команда: выполняет действия)"))
            tx = LangTexts(ctx, e.text, None, multiline=True, placeholder="Текст, который читают на экране")
            tx.changed.connect(lambda: (changed(), fill(e)))
            bl2.addWidget(tx)
            bl2.addWidget(QLabel("<b>Ответ экрана после команды</b> (пусто — «> Команда выполнена»)"))
            rs = LangTexts(ctx, e.result, None, multiline=False, placeholder="> Ворота открыты")
            rs.changed.connect(lambda: changed())
            bl2.addWidget(rs)
        once = QCheckBox("Один раз (после использования строка исчезает)")
        once.setChecked(e.once)
        once.toggled.connect(lambda v: (setattr(e, "once", bool(v)), changed(), fill(e)))
        bl2.addWidget(once)
        bl2.addWidget(QLabel("<b>Показывать, если</b>"))
        cl = LogicList(ctx, e.conditions, self_label="этот терминал")
        cl.changed.connect(lambda: changed())
        bl2.addWidget(cl)
        if not e.entries:
            bl2.addWidget(QLabel("<b>Действия</b> (у записи — при первом прочтении, у команды — каждый раз)"))
            al = LogicList(ctx, e.actions, actions=True, self_label="этот терминал")
            al.changed.connect(lambda: changed())
            bl2.addWidget(al)
            bl2.addWidget(hint("Для турелей без имени — «Турели и камеры» с пустым «Какие»: все в 1500 ед. от терминала."))
        editor_box.addWidget(box)

    def on_select():
        it = tree.currentItem()
        show_editor(find(it.data(0, Qt.UserRole)) if it else None)
    tree.itemSelectionChanged.connect(on_select)

    def add_entry(child=False):
        cur = state["entry"]
        if child and cur is not None:
            lst = cur.entries
            cur.text = {}
        else:
            lst = parent_list(cur) if cur is not None else t.entries
            if lst is None:
                lst = t.entries
        e = td.TermEntry(td.new_key(lst))
        e.title[p.base_language] = "Новая запись"
        lst.append(e)
        changed()
        fill(e)
        show_editor(e)

    def move(d):
        e = state["entry"]
        lst = parent_list(e) if e else None
        if not lst:
            return
        i = lst.index(e)
        j = i + d
        if 0 <= j < len(lst):
            lst[i], lst[j] = lst[j], lst[i]
            changed()
            fill(e)

    def remove():
        e = state["entry"]
        lst = parent_list(e) if e else None
        if lst is None:
            return
        if QMessageBox.question(insp, "Удалить запись", "Удалить «%s»%s?" % (e.label(lang, p.base_language),
                                " с подменю" if e.entries else "")) == QMessageBox.Yes:
            lst.remove(e)
            changed()
            fill()
            show_editor(None)

    for text, tip, fn in (("+ запись", "Запись или команда рядом с выбранной", lambda: add_entry(False)),
                          ("+ в подменю", "Вложить запись в выбранную (она станет подменю)", lambda: add_entry(True)),
                          ("↑", "Выше", lambda: move(-1)), ("↓", "Ниже", lambda: move(1)), ("✕", "Удалить", remove)):
        b = QToolButton(text=text)
        b.setToolTip(tip)
        b.clicked.connect(fn)
        bl.addWidget(b)
    bl.addStretch(1)
    gl.addLayout(bl)
    gl.addLayout(editor_box)
    lay.addWidget(g)

    g, gl = _group("Экран (превью)")
    preview = TerminalPreview(ctx, t)
    gl.addWidget(preview)
    gl.addWidget(hint("Так экран выглядит в игре (без взлома). Выберите запись — превью покажет её меню или текст."))
    lay.addWidget(g)
    fill()

    g, gl = _group("Заметка")
    note = AutoText(t.note, 2, 5)
    note.edited.connect(lambda s: (setattr(t, "note", s), insp.changed(False)))
    gl.addWidget(note)
    lay.addWidget(g)
    used = ["%s #%d" % (m, r.id) for m, layer in p.maps.items() for r in layer.refs if r.type == "terminal" and r.terminal == t.id]
    lay.addWidget(hint("На картах: " + (", ".join(used) if used else "нигде (поставьте объект «Терминал»)")))
    de = QPushButton("Удалить терминал")

    def delete():
        if QMessageBox.question(insp, "Удалить терминал", "Удалить терминал «%s»?" % t.id) == QMessageBox.Yes:
            p.terminals.remove(t)
            changed(True)
            insp.show_target(("globals",))
    de.clicked.connect(delete)
    lay.addWidget(de)
    return w, lay


def new_terminal(ctx, style="pc"):
    """Asks an id, makes a terminal with one record and one command, shows its page."""
    p = ctx.project
    if not p:
        return None
    n = 1
    while p.terminal("terminal_%d" % n):
        n += 1
    tid, ok = QInputDialog.getText(ctx, "Новый терминал", "id терминала (латиница, цифры, _):", text="terminal_%d" % n)
    tid = (tid or "").strip()
    if not ok or not tid:
        return None
    if p.terminal(tid) or not ID_RE.match(tid):
        QMessageBox.warning(ctx, "Терминал", "Такой id уже есть или в нём не только латиница, цифры, _ и -")
        return None
    t = td.TerminalDef(tid)
    t.style = style
    base = p.base_language
    t.title[base] = "Терминал" if base == "russian" else "Terminal"
    e = td.TermEntry("1")
    e.title[base] = "Запись 1" if base == "russian" else "Record 1"
    e.text[base] = "Текст записи." if base == "russian" else "The text of the record."
    t.entries.append(e)
    p.terminals.append(t)
    ctx.model_changed(True)
    ctx.refresh_logic_list()
    ctx.left_tabs.setCurrentWidget(ctx.logic_tab)
    ctx.inspector.show_target(("terminal", tid))
    return t
