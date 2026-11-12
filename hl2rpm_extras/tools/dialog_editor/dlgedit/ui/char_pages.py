"""The "RPG" tab: rules of the character, attributes and skills, perks, traits and the new
game (scripts/rpg/rpg.txt, scripts/rpg/newgame.txt; game: hl2rpm_char.cpp)."""
import os
import re

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDialog, QDialogButtonBox, QDoubleSpinBox, QHBoxLayout, QHeaderView,
                               QInputDialog, QLabel, QLineEdit, QListWidget, QListWidgetItem, QMessageBox, QPushButton,
                               QSpinBox, QTableWidget, QTableWidgetItem, QToolButton, QTreeWidget, QTreeWidgetItem,
                               QVBoxLayout, QWidget)

from .. import logic
from .. import rpgchar as rc
from .logic_widgets import LogicList, _Combo, choices
from .widgets import AutoText, LangTexts, QuestCombo, hint

ID_RE = re.compile(r"^[A-Za-z0-9_]+$")


def _group(title):
    from .world_pages import _group as g
    return g(title)


def _form():
    from .world_pages import _form as f
    return f()


def _combo(items, current):
    c = QComboBox()
    for data, label in items:
        c.addItem(label, data)
    i = c.findData(current)
    c.setCurrentIndex(i if i >= 0 else 0)
    return c


def _char_choices(project, with_stats=True, with_skills=True):
    lang = "russian" if "russian" in project.languages else project.base_language
    out = []
    if with_stats:
        out += [(s, rc.char_text(project, s, "name").get(lang) or s) for s in rc.STAT_IDS]
    if with_skills:
        out += [(s, rc.char_text(project, s, "name").get(lang) or s) for s in rc.SKILL_IDS]
    return out


# --- effects -----------------------------------------------------------------------------------
class EffectDialog(QDialog):
    def __init__(self, ctx, eff, parent=None):
        super().__init__(parent)
        self.ctx = ctx
        self.eff = dict(eff)
        self.setWindowTitle("Эффект")
        self.setMinimumWidth(420)
        lay = QVBoxLayout(self)
        f = _form()
        self.type = _combo([(t, n) for t, n, _k in rc.EFFECT_TYPES], self.eff.get("type", "skill"))
        self.type.currentIndexChanged.connect(self._fill_ids)
        f.addRow("Что:", self.type)
        self.id = QComboBox()
        f.addRow("Чего:", self.id)
        self.value = QDoubleSpinBox()
        self.value.setRange(-1000, 1000)
        self.value.setDecimals(1)
        self.value.setValue(float(self.eff.get("value", 5)))
        f.addRow("Сколько (за ранг):", self.value)
        lay.addLayout(f)
        lay.addWidget(hint("Отрицательное значение — недостаток (у черт: «+1 к Ловкости, но −15 здоровья»)."))
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)
        lay.addWidget(bb)
        self._fill_ids()

    def _fill_ids(self, *_):
        kind = rc.EFFECT_ID_KIND.get(self.type.currentData(), "")
        cur = self.eff.get("id", "")
        self.id.clear()
        p = self.ctx.project
        if kind == "stat":
            items = _char_choices(p, True, False)
        elif kind == "skill":
            items = _char_choices(p, False, True)
        elif kind == "wskill":
            items = [("", "любого оружия")] + [x for x in _char_choices(p, False, True) if x[0] in rc.WEAPON_SKILLS]
        else:
            items = []
        for v, label in items:
            self.id.addItem(label, v)
        self.id.setEnabled(bool(items))
        i = self.id.findData(cur)
        self.id.setCurrentIndex(i if i >= 0 else 0)

    def result_effect(self):
        d = {"type": self.type.currentData(), "value": round(self.value.value(), 1)}
        if d["value"] == int(d["value"]):
            d["value"] = int(d["value"])
        if self.id.isEnabled() and self.id.currentData():
            d["id"] = self.id.currentData()
        return d


class EffectList(QWidget):
    def __init__(self, ctx, effects, on_change):
        super().__init__()
        self.ctx = ctx
        self.effects = effects
        self.on_change = on_change
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        self.list = QListWidget()
        self.list.setMaximumHeight(120)
        self.list.itemDoubleClicked.connect(lambda _: self.edit())
        lay.addWidget(self.list)
        row = QHBoxLayout()
        for text, fn in (("+ эффект", self.add), ("✎", self.edit), ("✕", self.remove)):
            b = QToolButton(text=text)
            b.clicked.connect(fn)
            row.addWidget(b)
        row.addStretch(1)
        lay.addLayout(row)
        self.refresh()

    def refresh(self):
        self.list.clear()
        for e in self.effects:
            self.list.addItem(rc.describe_effect(e))

    def add(self):
        d = EffectDialog(self.ctx, {"type": "skill", "id": "speech", "value": 5}, self)
        if d.exec():
            self.effects.append(d.result_effect())
            self.refresh()
            self.on_change()

    def edit(self):
        i = self.list.currentRow()
        if 0 <= i < len(self.effects):
            d = EffectDialog(self.ctx, self.effects[i], self)
            if d.exec():
                self.effects[i] = d.result_effect()
                self.refresh()
                self.on_change()

    def remove(self):
        i = self.list.currentRow()
        if 0 <= i < len(self.effects):
            del self.effects[i]
            self.refresh()
            self.on_change()


# --- the left tab ----------------------------------------------------------------------------------
class CharMixin:
    def _build_char_tab(self):
        w = QWidget()
        l = QVBoxLayout(w)
        l.setContentsMargins(4, 4, 4, 4)
        l.addWidget(hint("Персонаж как в New Vegas: 7 характеристик, 12 навыков, перки за уровни, черты при создании. "
                         "И новая игра: где начинается и что у игрока с собой."))
        self.char_tree = QTreeWidget()
        self.char_tree.setHeaderHidden(True)
        self.char_tree.itemClicked.connect(lambda it, _c: self.inspector.show_target(it.data(0, Qt.UserRole)) if it.data(0, Qt.UserRole) else None)
        l.addWidget(self.char_tree, 1)
        row = QHBoxLayout()
        b = QPushButton("+ перк")
        b.clicked.connect(lambda: self.new_perk(False))
        row.addWidget(b)
        b = QPushButton("+ черта")
        b.clicked.connect(lambda: self.new_perk(True))
        row.addWidget(b)
        l.addLayout(row)
        b = QPushButton("Удалить выбранное")
        b.clicked.connect(self.delete_char_selected)
        l.addWidget(b)
        self.char_tab = w
        return w

    def refresh_char_tree(self):
        if not hasattr(self, "char_tree"):
            return
        t = self.char_tree
        t.clear()
        p = self.project
        if not p:
            return
        lang, base = self.display_lang, p.base_language
        for label, target in (("Правила", ("rpgrules",)), ("Характеристики и навыки", ("rpgchars",)), ("Новая игра", ("newgame",))):
            it = QTreeWidgetItem([label])
            it.setData(0, Qt.UserRole, target)
            t.addTopLevelItem(it)
        for title, lst, kind in (("Перки (%d)" % len(p.perks), p.perks, "perk"), ("Черты (%d)" % len(p.traits), p.traits, "trait")):
            top = QTreeWidgetItem([title])
            top.setData(0, Qt.UserRole, ("perks",) if kind == "perk" else ("traits",))
            t.addTopLevelItem(top)
            for d in sorted(lst, key=lambda x: (getattr(x, "level", 0), x.label(lang, base).lower())):
                txt = d.label(lang, base) + (("  · ур. %d" % d.level) if kind == "perk" else "")
                c = QTreeWidgetItem([txt])
                c.setData(0, Qt.UserRole, (kind, d.id))
                top.addChild(c)
            top.setExpanded(True)

    def _char_changed(self, target=None):
        self.model_changed(True)
        self.refresh_char_tree()
        self.left_tabs.setCurrentWidget(self.char_tab)
        if target:
            self.inspector.show_target(target)

    def add_stock_rpg(self):
        p = self.project
        have = {x.id for x in p.perks} | {x.id for x in p.traits}
        p.perks += [x for x in rc.stock_perks() if x.id not in have]
        p.traits += [x for x in rc.stock_traits() if x.id not in have]
        self._char_changed(("perks",))

    def new_perk(self, trait=False):
        p = self.project
        if not p:
            return
        lst = p.traits if trait else p.perks
        used = {x.id for x in lst}
        n = 1
        base = "trait" if trait else "perk"
        while "%s_%d" % (base, n) in used:
            n += 1
        pid, ok = QInputDialog.getText(self, "Новая черта" if trait else "Новый перк", "id (латиница, цифры, _):", text="%s_%d" % (base, n))
        pid = (pid or "").strip()
        if not ok or not pid:
            return
        if pid in used or not ID_RE.match(pid):
            QMessageBox.warning(self, "id", "Такой id уже есть или в нём не только латиница, цифры и _")
            return
        d = rc.Trait(pid) if trait else rc.Perk(pid)
        d.name = {p.base_language: pid}
        lst.append(d)
        self._char_changed(("trait" if trait else "perk", pid))

    def delete_char_selected(self):
        it = self.char_tree.currentItem() if hasattr(self, "char_tree") else None
        t = it.data(0, Qt.UserRole) if it else None
        if not t or t[0] not in ("perk", "trait"):
            return
        lst = self.project.perks if t[0] == "perk" else self.project.traits
        if QMessageBox.question(self, "Удалить", "Удалить «%s»?" % t[1]) == QMessageBox.Yes:
            lst[:] = [x for x in lst if x.id != t[1]]
            self._char_changed(("perks",) if t[0] == "perk" else ("traits",))


# --- inspector pages ---------------------------------------------------------------------------------
def rules_page(insp):
    ctx = insp.ctx
    p = ctx.project
    w, lay = insp._page("Правила персонажа",
                        "Числа New Vegas, подогнанные под HL2: при всех характеристиках 5 здоровье 100 и вес 40 кг — как у "
                        "игрока HL2. Навык = основа + своя характеристика × N + Удача / N + отмеченный. "
                        "Изменённые значения подсвечены; по умолчанию — подсказка поля.")
    groups = {}
    preview = QLabel()
    preview.setWordWrap(True)
    preview.setObjectName("hint")

    def update_preview():
        r = lambda k: float(rc.rule(p, k))
        skill = int(r("skill_base") + r("skill_per_stat") * 5 + -(-5 // max(1, int(r("skill_luck_div")))))
        hp = int(r("hp_base") + r("hp_per_end") * 5)
        carry = r("carry_base") + r("carry_per_str") * 5
        xp = [int(25 * (3 * L + 2) * (L - 1) * r("xp_scale")) for L in (2, 5, 10, 20, 30)]
        preview.setText("При всех характеристиках 5: здоровье %d, вес %.0f кг, навык %d (отмеченный %d), очков навыков за "
                        "уровень %d. Опыт до уровней 2 / 5 / 10 / 20 / 30: %s." % (
                            hp, carry, skill, skill + r("tag_bonus"), int(r("points_base") + 5 * r("points_per_int")),
                            " / ".join(str(x) for x in xp)))
    for key, default, label, group, dec in rc.RULES:
        if group not in groups:
            g, gl = _group(group)
            f = _form()
            gl.addLayout(f)
            lay.addWidget(g)
            groups[group] = f
        sp = QDoubleSpinBox()
        sp.setRange(-100000, 100000)
        sp.setDecimals(dec)
        sp.setSingleStep(1 if dec == 0 else 10 ** -dec * 5)
        sp.setValue(float(rc.rule(p, key)))
        sp.setToolTip("По умолчанию: %g" % default)

        def mark(s=sp, d=default):
            s.setStyleSheet("" if abs(s.value() - float(d)) < 1e-9 else "color:#ffd27a;")

        def changed(v, key=key, d=default, s=sp):
            if abs(v - float(d)) < 1e-9:
                p.rpg_rules.pop(key, None)
            else:
                p.rpg_rules[key] = int(v) if s.decimals() == 0 else round(v, s.decimals())
            mark(s, d)
            update_preview()
            insp.changed(False)
        sp.valueChanged.connect(changed)
        mark()
        groups[group].addRow(label + ":", sp)
    lay.addWidget(preview)
    update_preview()
    b = QPushButton("Вернуть всё по умолчанию")

    def reset():
        p.rpg_rules.clear()
        insp.changed(False)
        insp.rebuild_soon()
    b.clicked.connect(reset)
    lay.addWidget(b)
    return w, lay


def chars_page(insp):
    ctx = insp.ctx
    p = ctx.project
    w, lay = insp._page("Характеристики и навыки",
                        "Названия и описания (их видит игрок в окнах персонажа), у навыка — от какой характеристики он "
                        "растёт. Сами наборы фиксированы (7 и 12) — на них завязаны оружие, взлом, торговля, лечение.")
    langs = p.languages
    cols = ["id"] + ["Название (%s)" % l[:2].upper() for l in langs] + ["Описание (%s)" % l[:2].upper() for l in langs] + ["Растёт от"]
    ids = rc.STAT_IDS + rc.SKILL_IDS
    tbl = QTableWidget(len(ids), len(cols))
    tbl.setHorizontalHeaderLabels(cols)
    tbl.verticalHeader().setVisible(False)
    tbl.horizontalHeader().setSectionResizeMode(QHeaderView.Interactive)
    tbl.setMinimumHeight(560)
    tbl.blockSignals(True)
    for r, cid in enumerate(ids):
        it = QTableWidgetItem(cid)
        it.setFlags(it.flags() & ~Qt.ItemIsEditable)
        tbl.setItem(r, 0, it)
        for li, lang in enumerate(langs):
            tbl.setItem(r, 1 + li, QTableWidgetItem(rc.char_text(p, cid, "name").get(lang, "")))
            tbl.setItem(r, 1 + len(langs) + li, QTableWidgetItem(rc.char_text(p, cid, "desc").get(lang, "")))
        if cid in rc.SKILL_IDS:
            cb = _combo([(s, s) for s in rc.STAT_IDS], rc.skill_stat(p, cid))

            def set_stat(_, cid=cid, cb=cb):
                default = dict((s, st) for s, st, _n, _d in rc.SKILLS)[cid]
                ov = p.rpg_texts.setdefault(cid, {})
                if cb.currentData() == default:
                    ov.pop("stat", None)
                else:
                    ov["stat"] = cb.currentData()
                if not ov:
                    p.rpg_texts.pop(cid, None)
                insp.changed(False)
            cb.currentIndexChanged.connect(set_stat)
            tbl.setCellWidget(r, len(cols) - 1, cb)
    tbl.blockSignals(False)
    tbl.resizeColumnsToContents()

    def edited(item):
        r, c = item.row(), item.column()
        if c == 0 or c >= len(cols) - 1:
            return
        cid = ids[r]
        key = "name" if c <= len(langs) else "desc"
        lang = langs[(c - 1) % len(langs)]
        text = item.text().strip()
        default = rc.default_text(cid, key).get(lang, "")
        ov = p.rpg_texts.setdefault(cid, {})
        t = ov.setdefault(key, {})
        if text == default or not text:
            t.pop(lang, None)
        else:
            t[lang] = text
        if not t:
            ov.pop(key, None)
        if not ov:
            p.rpg_texts.pop(cid, None)
        insp.changed(False)
    tbl.itemChanged.connect(edited)
    lay.addWidget(tbl)
    return w, lay


def perks_page(insp, traits=False):
    ctx = insp.ctx
    p = ctx.project
    lst = p.traits if traits else p.perks
    w, lay = insp._page("Черты" if traits else "Перки",
                        ("Черты выбирают при создании персонажа (до 2): преимущество с недостатком." if traits else
                         "Перк — раз в 2 уровня (правила): выбирают из доступных по уровню и требованиям. У перка может быть "
                         "несколько рангов."))
    if not lst:
        g, gl = _group("Начать со стандартного набора")
        gl.addWidget(hint("20 перков и 8 черт в духе New Vegas с HL2-уклоном («Мастер монтировки», «Техника Надзора», "
                          "«Перебежчик»…). Их можно править и удалять."))
        b = QPushButton("Добавить стандартные перки и черты")
        b.clicked.connect(ctx.add_stock_rpg)
        gl.addWidget(b)
        lay.addWidget(g)
    else:
        lang, base = ctx.display_lang, p.base_language
        for d in sorted(lst, key=lambda x: (getattr(x, "level", 0), x.label(lang, base).lower())):
            effects = ", ".join(rc.describe_effect(e) for e in d.effects) or "без эффектов"
            head = d.label(lang, base) + ("" if traits else "  (уровень %d%s)" % (d.level, ", рангов %d" % d.ranks if d.ranks > 1 else ""))
            b = QPushButton("%s — %s" % (head, effects))
            b.setStyleSheet("text-align:left; padding:4px 8px;")
            b.clicked.connect(lambda _=False, i=d.id: insp.show_target(("trait" if traits else "perk", i)))
            lay.addWidget(b)
    b = QPushButton("+ черта" if traits else "+ перк")
    b.clicked.connect(lambda: ctx.new_perk(traits))
    lay.addWidget(b)
    return w, lay


def perk_page(insp, d, trait=False):
    ctx = insp.ctx
    p = ctx.project
    w, lay = insp._page(("Черта  %s" if trait else "Перк  %s") % d.id,
                        "Название и описание видит игрок в окне персонажа. Эффекты действуют всё время "
                        + ("(черта выбрана при создании)." if trait else "(умножаются на ранг)."))

    def changed(structural=False):
        insp.changed(structural)
        ctx.refresh_char_tree()
    f = _form()
    idl = QLineEdit(d.id)

    def rename():
        new = idl.text().strip()
        if new == d.id:
            return
        lst = p.traits if trait else p.perks
        if not new or not ID_RE.match(new) or any(x.id == new for x in lst):
            QMessageBox.warning(insp, "id", "Пустой, не латиница или уже существует")
            idl.setText(d.id)
            return
        old = d.id
        d.id = new
        if not trait:
            for _w, _k, items in logic.iter_logic(p):
                for x in items or []:
                    if x.get("type") == "perk" and x.get("name") == old:
                        x["name"] = new
        changed(True)
        insp.show_target(("trait" if trait else "perk", new))
    idl.editingFinished.connect(rename)
    f.addRow("id:", idl)
    if not trait:
        lv = QSpinBox()
        lv.setRange(1, 100)
        lv.setValue(d.level)
        lv.valueChanged.connect(lambda v: (setattr(d, "level", v), changed(True)))
        f.addRow("С уровня:", lv)
        rk = QSpinBox()
        rk.setRange(1, 20)
        rk.setValue(d.ranks)
        rk.valueChanged.connect(lambda v: (setattr(d, "ranks", v), changed()))
        f.addRow("Рангов:", rk)
    lay.addLayout(f)
    g, gl = _group("Название")
    lt = LangTexts(ctx, d.name, None, multiline=False)
    lt.changed.connect(lambda: changed(True))
    gl.addWidget(lt)
    lay.addWidget(g)
    g, gl = _group("Описание")
    lt = LangTexts(ctx, d.desc, None, multiline=True)
    lt.changed.connect(lambda: changed())
    gl.addWidget(lt)
    lay.addWidget(g)
    if not trait:
        g, gl = _group("Требования (не меньше)")
        tbl = QTableWidget(0, 3)
        tbl.setHorizontalHeaderLabels(["Характеристика / навык", "Минимум", ""])
        tbl.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        tbl.verticalHeader().setVisible(False)
        tbl.setMaximumHeight(140)

        def store():
            req = {}
            for r in range(tbl.rowCount()):
                cid = tbl.cellWidget(r, 0).currentData()
                if cid:
                    req[cid] = tbl.cellWidget(r, 1).value()
            d.req = req
            changed()

        def add_row(cid, v):
            r = tbl.rowCount()
            tbl.insertRow(r)
            cb = _combo(_char_choices(p), cid)
            cb.currentIndexChanged.connect(lambda _: store())
            tbl.setCellWidget(r, 0, cb)
            sp = QSpinBox()
            sp.setRange(0, 100)
            sp.setValue(int(v))
            sp.valueChanged.connect(lambda _: store())
            tbl.setCellWidget(r, 1, sp)
            xb = QToolButton(text="✕")

            def rm(_=False, cb=cb):
                for rr in range(tbl.rowCount()):
                    if tbl.cellWidget(rr, 0) is cb:
                        tbl.removeRow(rr)
                        break
                store()
            xb.clicked.connect(rm)
            tbl.setCellWidget(r, 2, xb)
        for cid, v in d.req.items():
            add_row(cid, v)
        gl.addWidget(tbl)
        ab = QPushButton("+ требование")
        ab.clicked.connect(lambda: (add_row("speech", 30), store()))
        gl.addWidget(ab)
        lay.addWidget(g)
    g, gl = _group("Эффекты")
    gl.addWidget(EffectList(ctx, d.effects, lambda: changed(True)))
    lay.addWidget(g)
    if not trait:
        users = [where for where, kind, items in logic.iter_logic(p) for x in (items or [])
                 if x.get("type") == "perk" and x.get("name") == d.id]
        g, gl = _group("Где используется (%d)" % len(users))
        gl.addWidget(hint(", ".join(users) if users else "Выбирается игроком при повышении уровня. Можно и дать действием «Дать перк» или "
                                                         "проверять условием «У игрока перк»."))
        lay.addWidget(g)
    g, gl = _group("Заметка")
    note = AutoText(d.note, 1, 4)
    note.edited.connect(lambda t: (setattr(d, "note", t), insp.changed(False)))
    gl.addWidget(note)
    lay.addWidget(g)
    return w, lay


def newgame_page(insp):
    ctx = insp.ctx
    p = ctx.project
    ng = p.newgame
    w, lay = insp._page("Новая игра",
                        "Что происходит в начале новой игры: на какой карте, где появляется игрок, создание персонажа "
                        "(имя, характеристики, навыки, черты), время, погода, снаряжение, первое задание. Файл игры — "
                        "scripts/rpg/newgame.txt; «Новая игра» в меню запускает карту из cfg/chapter1.cfg.")

    def changed():
        insp.changed(False)
    f = _form()
    files = ctx.map_files() if hasattr(ctx, "map_files") else {}
    maps = sorted(set(list(p.maps.keys()) + list(files.keys() if isinstance(files, dict) else files)))
    mc = _Combo([(m, m) for m in maps], ng.get("map", ""), editable=True, empty_label="— любая (первая загруженная) —")

    def set_map(*_):
        ng["map"] = mc.value()
        changed()
    mc.currentIndexChanged.connect(set_map)
    mc.lineEdit().editingFinished.connect(set_map)
    f.addRow("Карта:", mc)
    sp_row = QHBoxLayout()
    spins = []
    cur = ng.get("spawn") or [0.0, 0.0, 0.0]
    for k in range(3):
        s = QDoubleSpinBox()
        s.setRange(-32768, 32768)
        s.setDecimals(0)
        s.setValue(float(cur[k]))
        spins.append(s)
        sp_row.addWidget(s)
    yaw = QSpinBox()
    yaw.setRange(0, 359)
    yaw.setWrapping(True)
    yaw.setSuffix("°")
    yaw.setValue(int(ng.get("yaw", 0)) % 360)
    sp_row.addWidget(yaw)
    use = QCheckBox("своя точка")
    use.setChecked(bool(ng.get("spawn")))
    sp_row.addWidget(use)

    def set_spawn(*_):
        if use.isChecked():
            ng["spawn"] = [s.value() for s in spins]
            ng["yaw"] = yaw.value()
        else:
            ng.pop("spawn", None)
            ng.pop("yaw", None)
        changed()
    for s in spins:
        s.valueChanged.connect(set_spawn)
    yaw.valueChanged.connect(set_spawn)
    use.toggled.connect(set_spawn)
    f.addRow("Точка (x y z, поворот):", sp_row)
    pick = QPushButton("Указать на карте…")

    def do_pick():
        def got(pos):
            if pos is None:
                return
            ng["spawn"] = [round(pos[0]), round(pos[1]), round(pos[2]) + 8]
            ng.setdefault("yaw", 0)
            if not ng.get("map") and hasattr(ctx, "current_map_name"):
                ng["map"] = ctx.current_map_name()
            changed()
            insp.rebuild_soon()
        ctx.start_pick_point(got, "Клик — точка, где игрок появляется в новой игре · Esc — отмена")
    pick.clicked.connect(do_pick)
    f.addRow("", pick)
    cr = QCheckBox("Открыть окно создания персонажа")
    cr.setChecked(ng.get("create", True))
    cr.toggled.connect(lambda v: (ng.__setitem__("create", bool(v)), changed()))
    f.addRow("", cr)
    nm = QLineEdit(ng.get("name", ""))
    nm.setPlaceholderText("имя по умолчанию в окне создания")
    nm.editingFinished.connect(lambda: (ng.__setitem__("name", nm.text().strip()), changed()))
    f.addRow("Имя:", nm)
    hr = QDoubleSpinBox()
    hr.setRange(-1, 24)
    hr.setDecimals(1)
    hr.setSpecialValueText("как на карте")
    hr.setValue(float(ng["time"]) if ng.get("time") not in (None, "") else -1)

    def set_time(v):
        if v < 0:
            ng.pop("time", None)
        else:
            ng["time"] = round(v, 1)
        changed()
    hr.valueChanged.connect(set_time)
    f.addRow("Время, ч:", hr)
    wc = _combo([("", "как на карте")] + [(x, x) for x in logic.WEATHERS], ng.get("weather", ""))
    wc.currentIndexChanged.connect(lambda _: (ng.__setitem__("weather", wc.currentData()), changed()))
    f.addRow("Погода:", wc)
    mo = QSpinBox()
    mo.setRange(0, 1000000)
    mo.setValue(int(ng.get("money", 0) or 0))
    mo.valueChanged.connect(lambda v: (ng.__setitem__("money", v), changed()))
    f.addRow("Жетонов:", mo)
    qc = QuestCombo(ctx, ng.get("quest", ""), "— без задания —")
    qc.picked.connect(lambda q: (ng.__setitem__("quest", q), changed()))
    f.addRow("Первое задание:", qc)
    lay.addLayout(f)
    g, gl = _group("Снаряжение")
    tbl = QTableWidget(0, 3)
    tbl.setHorizontalHeaderLabels(["Предмет", "Сколько", ""])
    tbl.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
    tbl.verticalHeader().setVisible(False)
    tbl.setMaximumHeight(170)
    items = ng.setdefault("items", [])

    def store():
        out = []
        for r in range(tbl.rowCount()):
            iid = tbl.cellWidget(r, 0).value()
            if iid:
                out.append([iid, tbl.cellWidget(r, 1).value()])
        items[:] = out
        changed()

    def add_row(iid, n):
        r = tbl.rowCount()
        tbl.insertRow(r)
        cb = _Combo(choices(ctx, "item"), iid, editable=True)
        cb.currentIndexChanged.connect(lambda _: store())
        cb.lineEdit().editingFinished.connect(store)
        tbl.setCellWidget(r, 0, cb)
        sp = QSpinBox()
        sp.setRange(1, 9999)
        sp.setValue(int(n))
        sp.valueChanged.connect(lambda _: store())
        tbl.setCellWidget(r, 1, sp)
        xb = QToolButton(text="✕")

        def rm(_=False, cb=cb):
            for rr in range(tbl.rowCount()):
                if tbl.cellWidget(rr, 0) is cb:
                    tbl.removeRow(rr)
                    break
            store()
        xb.clicked.connect(rm)
        tbl.setCellWidget(r, 2, xb)
    for iid, n in list(items):
        add_row(iid, n)
    gl.addWidget(tbl)
    ab = QPushButton("+ предмет")
    ab.clicked.connect(lambda: (add_row("item_healthkit", 1), store()))
    gl.addWidget(ab)
    lay.addWidget(g)
    g, gl = _group("Ещё действия в начале")
    al = LogicList(ctx, ng.setdefault("actions", []), actions=True, self_label="—", empty_text="Нет.")
    al.changed.connect(changed)
    gl.addWidget(al)
    lay.addWidget(g)
    g, gl = _group("Меню «Новая игра»")
    gl.addWidget(hint("Пункт главного меню «Новая игра» выполняет cfg/chapter1.cfg мода. Кнопка запишет туда загрузку карты "
                      "выше (прежний файл сохранится как .bak)."))
    b = QPushButton("Записать cfg/chapter1.cfg")

    def write_cfg():
        m = ng.get("map")
        mod = p.settings.mod_dir
        if not m or not mod or not os.path.isdir(mod):
            QMessageBox.warning(insp, "Новая игра", "Выберите карту и укажите папку мода (Проект → Настройки).")
            return
        path = os.path.join(mod, "cfg", "chapter1.cfg")
        if os.path.exists(path):
            try:
                import shutil
                shutil.copyfile(path, path + ".bak")
            except OSError:
                pass
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write("map %s\n" % m)
        QMessageBox.information(insp, "Новая игра", "Записано: %s\n«Новая игра» в меню загрузит карту %s." % (path, m))
    b.clicked.connect(write_cfg)
    gl.addWidget(b)
    lay.addWidget(g)
    return w, lay
