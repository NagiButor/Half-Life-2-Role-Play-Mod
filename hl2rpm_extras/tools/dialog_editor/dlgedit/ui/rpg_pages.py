"""The RPG part of the main window: items (weapons, ammo, apparel, medicine, food,
notes, keys, money) and loot lists - the left tab "Предметы" and their inspector pages."""
import re

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor, QIcon, QPixmap
from PySide6.QtWidgets import (QCheckBox, QDoubleSpinBox, QHBoxLayout, QHeaderView, QInputDialog, QLabel, QLineEdit,
                               QMessageBox, QPushButton, QSpinBox, QTableWidget, QTableWidgetItem, QToolButton, QTreeWidget,
                               QTreeWidgetItem, QVBoxLayout, QWidget)

from .. import rpgdata as rd
from .widgets import LangTexts, hint
from .world_pages import _form, _group, bind_combo, combo

ID_RE = re.compile(r"^[a-z0-9_]+$")


class RPGMixin:
    # ------------------------------------------------------------------ the left tab
    def _build_items_tab(self):
        w = QWidget()
        l = QVBoxLayout(w)
        l.setContentsMargins(4, 4, 4, 4)
        self.item_filter = QLineEdit()
        self.item_filter.setPlaceholderText("фильтр предметов…")
        self.item_filter.textChanged.connect(lambda _: self.refresh_item_tree())
        l.addWidget(self.item_filter)
        self.item_tree = QTreeWidget()
        self.item_tree.setHeaderLabels(["Предмет", "кг", "цена"])
        self.item_tree.setColumnWidth(0, 170)
        self.item_tree.setColumnWidth(1, 40)
        self.item_tree.itemClicked.connect(self._item_clicked)
        l.addWidget(self.item_tree, 1)
        row = QHBoxLayout()
        b = QPushButton("+ предмет")
        b.clicked.connect(self.new_item)
        row.addWidget(b)
        b = QPushButton("+ список лута")
        b.clicked.connect(self.new_lootlist)
        row.addWidget(b)
        m = QToolButton(text="…")
        m.setToolTip("Стандартные предметы HL2")
        m.clicked.connect(self.add_stock_items)
        row.addWidget(m)
        l.addLayout(row)
        return w

    def refresh_item_tree(self):
        t = self.item_tree
        t.clear()
        p = self.project
        if not p:
            return
        flt = self.item_filter.text().strip().lower()
        lang, base = self.display_lang, p.base_language
        cats = {}
        for cid, cname in rd.ITEM_CATEGORIES:
            it = QTreeWidgetItem([cname, "", ""])
            f = it.font(0)
            f.setBold(True)
            it.setFont(0, f)
            cats[cid] = it
        for item in sorted(p.items, key=lambda x: x.label(lang, base).lower()):
            lbl = "%s  (%s)" % (item.label(lang, base), item.id)
            if flt and flt not in lbl.lower():
                continue
            x = QTreeWidgetItem([lbl, ("%g" % item.weight) if item.weight >= 0 else "—", str(item.value) if item.value else ""])
            x.setData(0, Qt.UserRole, ("item", item.id))
            if item.quest:
                x.setForeground(0, QColor("#ffd27a"))
            cats.get(item.category, cats["misc"]).addChild(x)
        for cid, _ in rd.ITEM_CATEGORIES:
            it = cats[cid]
            if it.childCount():
                it.setText(0, "%s (%d)" % (it.text(0), it.childCount()))
                t.addTopLevelItem(it)
                it.setExpanded(bool(flt) or it.childCount() <= 12)
        ll = QTreeWidgetItem(["Списки лута", "", ""])
        f = ll.font(0)
        f.setBold(True)
        ll.setFont(0, f)
        for x in sorted(p.lootlists, key=lambda x: x.id):
            if flt and flt not in x.id.lower():
                continue
            c = QTreeWidgetItem(["%s  · %d" % (x.id, len(x.entries)), "", ""])
            c.setData(0, Qt.UserRole, ("lootlist", x.id))
            ll.addChild(c)
        t.addTopLevelItem(ll)
        ll.setExpanded(True)
        if not p.items:
            e = QTreeWidgetItem(["Нет предметов — «…» добавит стандартные HL2", "", ""])
            e.setForeground(0, QColor("#8d97a4"))
            t.insertTopLevelItem(0, e)

    def _item_clicked(self, it, col):
        d = it.data(0, Qt.UserRole)
        if d:
            self.inspector.show_target(d)

    def _ask_id(self, title, text, default):
        while True:
            iid, ok = QInputDialog.getText(self, title, text, text=default)
            iid = (iid or "").strip().lower()
            if not ok or not iid:
                return ""
            if not ID_RE.match(iid):
                QMessageBox.warning(self, title, "Только латиница, цифры и _")
                continue
            return iid

    def new_item(self):
        if not self.project:
            return
        iid = self._ask_id("Новый предмет", "id предмета (латиница; для своих предметов — например, note_letter_anna, key_warehouse):",
                           "rpm_item")
        if not iid:
            return
        if self.project.item(iid):
            QMessageBox.warning(self, "Предмет", "Такой предмет уже есть")
            return
        it = rd.ItemDef(id=iid, base="" if iid.startswith(("weapon_", "item_")) else "generic")
        it.name[self.project.base_language] = iid
        self.project.items.append(it)
        self.model_changed(True)
        self.refresh_item_tree()
        self.inspector.show_target(("item", iid))

    def add_stock_items(self):
        if not self.project:
            return
        n = 0
        for it in rd.stock_items():
            if not self.project.item(it.id):
                self.project.items.append(it)
                n += 1
        self.model_changed(True)
        self.refresh_item_tree()
        QMessageBox.information(self, "Предметы", "Добавлено стандартных предметов HL2: %d (оружие, патроны, аптечки, костюм, "
                                "жетоны, паёк, вода). Названия, вес, цены и урон можно менять." % n)

    def new_lootlist(self):
        if not self.project:
            return
        lid = self._ask_id("Список лута", "id списка (например, loot_cp_body, loot_kitchen):", "loot_list")
        if not lid or self.project.lootlist(lid):
            return
        ll = rd.LootList(id=lid)
        self.project.lootlists.append(ll)
        self.model_changed(True)
        self.refresh_item_tree()
        self.inspector.show_target(("lootlist", lid))

    def delete_item(self, item):
        users = [r.label() for layer in self.project.maps.values() for r in layer.refs if r.type == "item" and r.classname == item.id]
        if QMessageBox.question(self, "Предмет", "Удалить предмет «%s»?%s" % (item.id, ("\nОн стоит на картах: " + ", ".join(users[:10])) if users else "")) != QMessageBox.Yes:
            return
        self.project.items.remove(item)
        self.model_changed(True)
        self.refresh_item_tree()
        self.inspector.show_target(None)

    def all_item_ids(self):
        """Database items + the game's classnames (for loot tables, placing, quest rewards)."""
        ids = [x.id for x in self.project.items] if self.project else []
        for x in self.item_names():
            if x not in ids:
                ids.append(x)
        return ids


def _num_spin(value, lo, hi, step=1.0, dec=0, special=None):
    sp = QDoubleSpinBox() if dec else QSpinBox()
    sp.setRange(lo, hi)
    if dec:
        sp.setDecimals(dec)
        sp.setSingleStep(step)
    if special is not None:
        sp.setSpecialValueText(special)
    sp.setValue(value)
    return sp


def item_page(insp, iid):
    ctx = insp.ctx
    p = ctx.project
    it = p.item(iid)
    if it is None:
        return insp._page("Предмет не найден")
    w, lay = insp._page("Предмет  %s" % it.id, "Записывается в scripts/rpg/items.txt. Игра берёт оттуда название, описание, вес, цену; "
                                              "свои предметы (записки, ключи, деньги, еда) появляются в мире как item_rpm.")

    def ch(regroup=False):
        insp.changed(False)
        if regroup:
            ctx.refresh_item_tree()
            insp.rebuild_soon()

    top = QHBoxLayout()
    pic = QLabel()
    pic.setFixedSize(112, 112)
    th = ctx.thumbnailer() if hasattr(ctx, "thumbnailer") else None
    img = th.get(it.model) if (th and it.model) else None
    if img is not None:
        pic.setPixmap(QPixmap.fromImage(img))
    else:
        pic.setText("нет модели")
        pic.setAlignment(Qt.AlignCenter)
    top.addWidget(pic)
    f = _form()
    cat = combo(rd.ITEM_CATEGORIES, it.category)
    cat.currentIndexChanged.connect(lambda _: (setattr(it, "category", cat.currentData()), ch(True)))
    f.addRow("Категория:", cat)
    bs = combo(rd.ITEM_BASES, it.base)
    bs.currentIndexChanged.connect(lambda _: (setattr(it, "base", bs.currentData()), ch(True)))
    f.addRow("Что это:", bs)
    mrow = QHBoxLayout()
    ml = QLineEdit(it.model)
    ml.editingFinished.connect(lambda: (setattr(it, "model", ml.text().strip().replace("\\", "/").lower()), ch(True)) if ml.text().strip() != it.model else None)
    mrow.addWidget(ml, 1)
    br = QToolButton(text="…")

    def browse():
        m = ctx.browse_model(it.model, "models/")
        if m:
            it.model = m
            ch(True)
    br.clicked.connect(browse)
    mrow.addWidget(br)
    f.addRow("Модель:", mrow)
    top.addLayout(f, 1)
    lay.addLayout(top)

    g, gl = _group("Название")
    nm = LangTexts(ctx, it.name, multiline=False, placeholder="как предмет называется в игре")
    nm.changed.connect(lambda: (ch(), ctx.refresh_item_tree()))
    gl.addWidget(nm)
    lay.addWidget(g)
    g, gl = _group("Описание (в инвентаре)")
    ds = LangTexts(ctx, it.desc, multiline=True, placeholder="что видит игрок в карточке предмета")
    ds.changed.connect(lambda: ch())
    gl.addWidget(ds)
    lay.addWidget(g)
    if it.category == "note" or it.base == "note":
        g, gl = _group("Текст записки")
        tx = LangTexts(ctx, it.text, multiline=True, placeholder="текст, который откроется при чтении")
        tx.changed.connect(lambda: ch())
        gl.addWidget(tx)
        lay.addWidget(g)

    f = _form()
    wsp = _num_spin(it.weight, -1, 500, 0.1, 2, "по умолчанию")
    wsp.valueChanged.connect(lambda v: (setattr(it, "weight", float(v)), ch()))
    f.addRow("Вес, кг:", wsp)
    vsp = _num_spin(it.value, 0, 1000000)
    vsp.valueChanged.connect(lambda v: (setattr(it, "value", int(v)), ch()))
    f.addRow("Цена:", vsp)
    if it.category in ("aid", "food"):
        hs = _num_spin(it.heal, 0, 1000)
        hs.valueChanged.connect(lambda v: (setattr(it, "heal", int(v)), ch()))
        f.addRow("Лечит:", hs)
    st = QCheckBox("Складывается в одну ячейку (деньги, расходники)")
    st.setChecked(it.stackable)
    st.toggled.connect(lambda v: (setattr(it, "stackable", bool(v)), ch()))
    f.addRow("", st)
    qs = QCheckBox("Квестовый: нельзя выбросить и продать")
    qs.setChecked(it.quest)
    qs.toggled.connect(lambda v: (setattr(it, "quest", bool(v)), ch(True)))
    f.addRow("", qs)
    lay.addLayout(f)

    if it.category == "weapon":
        g, gl = _group("Оружие")
        wf = _form()
        dm = _num_spin(it.damage, 0, 10000, 1, 1, "по умолчанию (skill.cfg)")
        dm.valueChanged.connect(lambda v: (setattr(it, "damage", float(v)), ch()))
        wf.addRow("Урон:", dm)
        fr = _num_spin(it.fire_rate, 0, 100, 0.1, 2, "—")
        fr.valueChanged.connect(lambda v: (setattr(it, "fire_rate", float(v)), ch()))
        wf.addRow("Выстрелов в секунду:", fr)
        cl = _num_spin(it.clip, 0, 1000, special="—")
        cl.valueChanged.connect(lambda v: (setattr(it, "clip", int(v)), ch()))
        wf.addRow("Магазин:", cl)
        sk = combo(rd.WEAPON_SKILLS, it.skill)
        sk.currentIndexChanged.connect(lambda _: (setattr(it, "skill", sk.currentData()), ch()))
        wf.addRow("Навык:", sk)
        sr = _num_spin(it.skill_req, 0, 100, special="нет")
        sr.valueChanged.connect(lambda v: (setattr(it, "skill_req", int(v)), ch()))
        wf.addRow("Требуется навыка:", sr)
        gl.addLayout(wf)
        gl.addWidget(hint("Урон и требования навыка начнут действовать вместе с РПГ-системой (характеристики и навыки игрока); "
                          "пока они видны в описании оружия."))
        lay.addWidget(g)

    users = []
    for mname, layer in sorted(p.maps.items()):
        for r in layer.refs:
            if r.type == "item" and r.classname.lower() == it.id.lower():
                users.append("%s: %s" % (mname, r.label()))
    for ll in p.lootlists:
        if any(e.get("item") == it.id for e in ll.entries):
            users.append("лут: " + ll.id)
    if users:
        lay.addWidget(hint("Где используется: " + ", ".join(users[:20])))
    row = QHBoxLayout()
    if hasattr(ctx, "start_place"):
        b = QPushButton("Поставить на карту")
        b.clicked.connect(lambda: ctx.start_place(("item", it.id)))
        row.addWidget(b)
    b = QPushButton("Дублировать")

    def dup():
        nid = ctx._ask_id("Копия предмета", "id копии:", it.id + "_2")
        if nid and not p.item(nid):
            c = it.clone()
            c.id = nid
            p.items.append(c)
            ch(True)
            insp.show_target(("item", nid))
    b.clicked.connect(dup)
    row.addWidget(b)
    b = QPushButton("Удалить")
    b.clicked.connect(lambda: ctx.delete_item(it))
    row.addWidget(b)
    row.addStretch(1)
    lay.addLayout(row)
    return w, lay


def lootlist_page(insp, lid):
    ctx = insp.ctx
    p = ctx.project
    ll = p.lootlist(lid)
    if ll is None:
        return insp._page("Список не найден")
    w, lay = insp._page("Список лута  %s" % ll.id,
                        "Что окажется в контейнере, у трупа или у торговца: каждая строка выпадает со своим шансом "
                        "(или одна случайная из всех). Ссылка в луте: list:%s." % ll.id)

    def ch():
        insp.changed(False)

    mode = combo([(True, "каждая строка — по своему шансу"), (False, "одна случайная строка (по весу шанса)")], ll.all)
    mode.currentIndexChanged.connect(lambda _: (setattr(ll, "all", bool(mode.currentData())), ch()))
    lay.addWidget(mode)
    tbl = QTableWidget(len(ll.entries), 5)
    tbl.setHorizontalHeaderLabels(["Предмет / list:<id>", "от", "до", "шанс %", "с уровня"])
    tbl.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
    tbl.verticalHeader().setVisible(False)
    names = ctx.all_item_ids() + ["list:" + x.id for x in p.lootlists if x.id != ll.id]

    def sync():
        out = []
        for r in range(tbl.rowCount()):
            cb = tbl.cellWidget(r, 0)
            name = cb.currentText().strip() if cb else ""
            if not name:
                continue
            e = {"item": name}
            for c, k, d in ((1, "min", 1), (2, "max", 1), (3, "chance", 100), (4, "level", 0)):
                v = tbl.cellWidget(r, c).value()
                if v != d:
                    e[k] = v
            out.append(e)
        ll.entries = out
        ch()
    from PySide6.QtWidgets import QComboBox
    for r, e in enumerate(ll.entries):
        cb = QComboBox()
        cb.setEditable(True)
        cb.addItems(names)
        cb.setCurrentText(e.get("item", ""))
        cb.currentTextChanged.connect(lambda *_: sync())
        tbl.setCellWidget(r, 0, cb)
        for c, k, d, hi in ((1, "min", 1, 9999), (2, "max", 1, 9999), (3, "chance", 100, 100), (4, "level", 0, 50)):
            sp = QSpinBox()
            sp.setRange(0, hi)
            sp.setValue(int(e.get(k, d)))
            sp.valueChanged.connect(lambda *_: sync())
            tbl.setCellWidget(r, c, sp)
    tbl.setMinimumHeight(60 + 30 * max(3, len(ll.entries)))
    lay.addWidget(tbl)
    row = QHBoxLayout()
    b = QPushButton("+ строка")

    def add():
        ll.entries.append({"item": "item_healthvial"})
        ch()
        insp.rebuild_soon()
    b.clicked.connect(add)
    row.addWidget(b)
    b = QPushButton("− выбранная")

    def rem():
        r = tbl.currentRow()
        if 0 <= r < len(ll.entries):
            del ll.entries[r]
            ch()
            insp.rebuild_soon()
    b.clicked.connect(rem)
    row.addWidget(b)
    b = QPushButton("Удалить список")

    def delete():
        if QMessageBox.question(w, "Список лута", "Удалить «%s»?" % ll.id) == QMessageBox.Yes:
            p.lootlists.remove(ll)
            ch()
            ctx.refresh_item_tree()
            insp.show_target(None)
    b.clicked.connect(delete)
    row.addWidget(b)
    row.addStretch(1)
    lay.addLayout(row)
    lay.addWidget(hint("Использовать: у контейнера или NPC в поле лута — «list:%s» (можно вместе с предметами: "
                       "list:%s;item_healthvial:2)." % (ll.id, ll.id)))
    return w, lay
