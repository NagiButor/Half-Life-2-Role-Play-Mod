"""Wizards of the Creation Kit (menu "Мастера"): a few questions make a working piece of the game -
a trader (NPC + goods + trade dialog + his place on the map), a quest "bring / kill / talk" (the quest,
its objective and the giver's dialog options to take and to hand it in), a Civil Protection patrol
(NPC + a square route around the camera + the patrol package). Everything they make is ordinary data
of the project and is edited further on its pages."""
import math
import re

from PySide6.QtWidgets import (QComboBox, QDialog, QDialogButtonBox, QFormLayout, QLabel, QLineEdit, QMessageBox,
                               QSpinBox, QVBoxLayout)

from .. import rpgdata
from .. import worlddata as wd
from ..model import NPC, Node, Objective, Option, Quest, Stage

ID_RE = re.compile(r"^[A-Za-z0-9_\-]+$")


def _t(project, ru, en):
    """A text in the project's languages (Russian and English; others get the base one)."""
    d = {}
    for lang in project.languages:
        d[lang] = ru if lang == "russian" else en
    return d


class _Form(QDialog):
    def __init__(self, parent, title, intro):
        super().__init__(parent)
        self.setWindowTitle(title)
        self.setMinimumWidth(460)
        lay = QVBoxLayout(self)
        lab = QLabel(intro)
        lab.setWordWrap(True)
        lay.addWidget(lab)
        self.form = QFormLayout()
        lay.addLayout(self.form)
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)
        lay.addWidget(bb)

    def line(self, label, text=""):
        e = QLineEdit(text)
        self.form.addRow(label, e)
        return e

    def combo(self, label, items, editable=False):
        c = QComboBox()
        c.setEditable(editable)
        for v, t in items:
            c.addItem(t, v)
        self.form.addRow(label, c)
        return c

    def spin(self, label, lo, hi, v, suffix=""):
        s = QSpinBox()
        s.setRange(lo, hi)
        s.setValue(v)
        s.setSuffix(suffix)
        self.form.addRow(label, s)
        return s


def _value(c):
    if c.currentIndex() >= 0 and c.currentText() == c.itemText(c.currentIndex()):
        return c.currentData()
    return c.currentText().strip()


def _new_targetname(p, base):
    n = 1
    while p.npc("%s_%d" % (base, n)):
        n += 1
    return "%s_%d" % (base, n)


def _place_point(ctx, ahead=200.0):
    """In front of the camera of the map window, on the floor; None without an open map."""
    if getattr(ctx, "layer", None) is None or getattr(ctx, "worldview", None) is None:
        return None
    cam = ctx.worldview.cam
    y = math.radians(cam.yaw)
    pos = [cam.pos[0] + math.cos(y) * ahead, cam.pos[1] + math.sin(y) * ahead, cam.pos[2]]
    scene = getattr(ctx, "scene", None)
    if scene is not None:
        try:
            pos = scene.drop_to_floor(pos, up=16.0)
        except Exception:
            pass
    return [round(float(v), 1) for v in pos], (cam.yaw + 180.0) % 360


def _check_name(ctx, tn):
    if not tn or not ID_RE.match(tn):
        QMessageBox.warning(ctx, "Мастер", "targetname: только латиница, цифры, _ и -")
        return False
    if ctx.project.npc(tn):
        QMessageBox.warning(ctx, "Мастер", "NPC «%s» уже есть" % tn)
        return False
    return True


def _finish(ctx, npc=None, what=""):
    ctx.model_changed(True)
    for fn in ("refresh_logic_list", "refresh_quest_list", "refresh_rpg_list"):
        if hasattr(ctx, fn):
            try:
                getattr(ctx, fn)()
            except Exception:
                pass
    if npc is not None and hasattr(ctx, "select_npc"):
        ctx.select_npc(npc)
    if hasattr(ctx, "statusBar"):
        ctx.statusBar().showMessage("Мастер: " + what, 8000)


# --- trader --------------------------------------------------------------------------------------------
def new_trader(ctx):
    p = ctx.project
    if not p:
        return
    f = _Form(ctx, "Мастер: новый торговец",
              "Создаст NPC-торговца: список товаров, деньги, диалог «Покажи товар» / «Пока» и (если открыта карта) "
              "поставит его перед камерой с пакетом «стоять на месте» весь день.")
    tn = f.line("targetname:", _new_targetname(p, "trader"))
    name = f.line("Имя (как видит игрок):", "Торговец")
    cls = f.combo("Кто:", [("npc_citizen", "горожанин"), ("npc_vortigaunt", "вортигонт"), ("npc_metropolice", "ГО (продажный)")])
    lists = [("__new", "— новый список: аптечки, батареи, патроны, шпильки —")] + [(x.id, x.id) for x in p.lootlists]
    goods = f.combo("Товары:", lists)
    money = f.spin("Деньги торговца:", 0, 100000, 300)
    markup = f.combo("Наценка:", [(1.0, "как есть"), (1.25, "+25 %"), (1.5, "+50 %"), (2.0, "×2")])
    markup.setCurrentIndex(1)
    if f.exec() != QDialog.Accepted:
        return
    targetname = tn.text().strip()
    if not _check_name(ctx, targetname):
        return
    list_id = goods.currentData()
    if list_id == "__new":
        list_id = "goods_" + targetname
        if p.lootlist(list_id) is None:
            ll = rpgdata.LootList(id=list_id, note="Мастер: товары %s" % targetname)
            ll.entries = [
                {"item": "item_healthvial", "min": 2, "max": 4, "chance": 100, "level": 0},
                {"item": "item_healthkit", "min": 1, "max": 2, "chance": 70, "level": 0},
                {"item": "item_battery", "min": 1, "max": 2, "chance": 60, "level": 0},
                {"item": "item_ammo_pistol", "min": 18, "max": 36, "chance": 80, "level": 0},
                {"item": "bobby_pin", "min": 3, "max": 6, "chance": 100, "level": 0},
            ]
            p.lootlists.append(ll)
    npc = NPC(targetname)
    npc.display_name = _t(p, name.text().strip() or "Торговец", name.text().strip() or "Trader")
    n0 = Node(0)
    n0.pos = [260.0, 0.0]
    n0.line = _t(p, "Чего желаешь?", "What will it be?")
    o1 = Option()
    o1.text = _t(p, "Покажи товар.", "Show me your goods.")
    o1.trade = True
    o2 = Option()
    o2.text = _t(p, "Пока.", "Bye.")
    o2.close = True
    n0.options = [o1, o2]
    npc.nodes = [n0]
    a = npc.actor
    a.classname = cls.currentData()
    a.essential = True
    a.vendor = {"list": "list:" + list_id, "money": int(money.value()), "markup": float(markup.currentData()),
                "pays": 0.5, "restock_hours": 72, "currency": "rpm_credits"}
    place = _place_point(ctx)
    if place is not None:
        pos, yaw = place
        pk = wd.Package("guard")
        pk.name = "У прилавка"
        pk.start, pk.duration = 0.0, 24.0
        pk.location = "point"
        pk.point = list(pos)
        pk.point_map = getattr(ctx, "cur_map", "") or ""
        pk.yaw = yaw
        a.packages = [pk]
        r = wd.Ref(ctx.layer.new_id(), "npc", pos)
        r.angles = [0.0, round(yaw / 15) * 15 % 360, 0.0]
        r.npc = targetname
        ctx.layer.refs.append(r)
    p.npcs.append(npc)
    _finish(ctx, npc, "торговец %s%s" % (targetname, "" if place else " (карта не открыта — поставьте его на карту сами)"))


# --- quest: bring / kill / talk ---------------------------------------------------------------------------
def new_quest(ctx):
    p = ctx.project
    if not p:
        return
    f = _Form(ctx, "Мастер: задание",
              "Создаст задание с одной целью и, если выбран NPC-заказчик, варианты в его диалоге: «Есть работа?» "
              "(выдаёт задание) и «Готово» (виден, когда цель выполнена; сдаёт задание, награда). Дальше задание "
              "правится на его странице.")
    n = 1
    while p.quest("quest_%d" % n):
        n += 1
    qid = f.line("id задания:", "quest_%d" % n)
    title = f.line("Название:", "Новое задание")
    kind = f.combo("Цель:", [("collect", "принести предмет"), ("kill", "убить"), ("talk", "поговорить с NPC")])
    items = [(x.id, "предмет: %s" % x.id) for x in p.items] + [("", "— или впишите id / targetname —")]
    target = f.combo("Что / кого:", items, editable=True)
    count = f.spin("Сколько:", 1, 999, 1)
    givers = [("", "— без заказчика (выдать событием) —")] + [(x.targetname, x.targetname) for x in p.npcs if not x.actor.is_template]
    giver = f.combo("Заказчик:", givers)
    xp = f.spin("Опыт:", 0, 100000, 50)
    money = f.spin("Жетоны:", 0, 100000, 20)
    if f.exec() != QDialog.Accepted:
        return
    quest_id = qid.text().strip()
    if not quest_id or not ID_RE.match(quest_id) or p.quest(quest_id):
        QMessageBox.warning(ctx, "Мастер", "id задания пустой, не латиница или уже есть")
        return
    k = kind.currentData()
    tgt = _value(target)
    q = Quest(quest_id)
    q.title = _t(p, title.text().strip() or quest_id, title.text().strip() or quest_id)
    st = Stage()
    st.title = q.title
    st.final = True
    o = Objective("o1")
    o.type = k
    if k == "collect":
        o.item = tgt
        o.count = int(count.value())
        o.text = _t(p, "Добыть: %s ×%d" % (tgt, count.value()), "Get: %s x%d" % (tgt, count.value()))
    elif k == "kill":
        o.target = tgt
        o.count = int(count.value())
        o.text = _t(p, "Убить: %s" % tgt, "Kill: %s" % tgt)
    else:
        o.target = tgt
        o.text = _t(p, "Поговорить: %s" % tgt, "Talk to: %s" % tgt)
    st.objectives = [o]
    q.stages = [st]
    q.xp = int(xp.value())
    if money.value():
        q.on_complete = [{"type": "money", "value": int(money.value())}]
    q.note = "Мастер: задание «%s»" % kind.currentText()
    p.quests.append(q)
    gnpc = p.npc(giver.currentData()) if giver.currentData() else None
    if gnpc is not None:
        hub = gnpc.node(gnpc.start_node)
        if hub is None:
            hub = Node(0)
            hub.line = _t(p, "Да?", "Yes?")
            gnpc.nodes.append(hub)
            gnpc.start_node = 0
        used = set(gnpc.node_ids())
        nid = 100
        while nid in used or nid + 1 in used:
            nid += 10
        take = Option()
        take.text = _t(p, "Есть работа?", "Any work?")
        take.next = nid
        take.conditions = [{"type": "quest_state", "quest": quest_id, "value": "inactive"}]
        take.actions = [{"type": "quest_start", "quest": quest_id}]
        done = Option()
        done.text = _t(p, "Готово.", "It's done.")
        done.next = nid + 1
        done.conditions = [{"type": "objective", "quest": quest_id, "obj": "o1"}, {"type": "quest_state", "quest": quest_id, "value": "active"}]
        done.actions = ([{"type": "take", "item": tgt, "count": int(count.value())}] if k == "collect" else []) + \
            [{"type": "quest_complete", "quest": quest_id}]
        at = len(hub.options)
        while at > 0 and hub.options[at - 1].close:
            at -= 1
        hub.options[at:at] = [take, done]
        n1 = Node(nid)
        n1.pos = [hub.pos[0] + 320, hub.pos[1] - 80]
        n1.line = _t(p, "Есть. %s" % (title.text().strip() or quest_id), "There is. %s" % (title.text().strip() or quest_id))
        n1.end = True
        n2 = Node(nid + 1)
        n2.pos = [hub.pos[0] + 320, hub.pos[1] + 80]
        n2.line = _t(p, "Хорошая работа. Держи.", "Good work. Here.")
        n2.end = True
        gnpc.nodes += [n1, n2]
    _finish(ctx, gnpc, "задание %s%s" % (quest_id, " у %s" % gnpc.targetname if gnpc else ""))


# --- Civil Protection patrol ---------------------------------------------------------------------------------
def new_patrol(ctx):
    p = ctx.project
    if not p:
        return
    if getattr(ctx, "layer", None) is None:
        QMessageBox.information(ctx, "Мастер", "Откройте карту в окне карты: патруль ставится вокруг камеры.")
        return
    f = _Form(ctx, "Мастер: патруль ГО",
              "Создаст метрокопа с дубинкой, маршрут-квадрат вокруг точки перед камерой и пакет «патруль» на весь день "
              "(ночью можно добавить второй пакет на его странице). Фракция police, если она есть в проекте.")
    tn = f.line("targetname:", _new_targetname(p, "cp_patrol"))
    side = f.spin("Сторона квадрата маршрута:", 128, 4096, 512, " ед.")
    weapon = f.combo("Оружие:", [("weapon_stunstick", "дубинка"), ("weapon_pistol", "пистолет"), ("weapon_smg1", "MP7")])
    if f.exec() != QDialog.Accepted:
        return
    targetname = tn.text().strip()
    if not _check_name(ctx, targetname):
        return
    pos, yaw = _place_point(ctx, ahead=side.value() * 0.5 + 64)
    L = ctx.layer
    h = side.value() * 0.5
    path = wd.Ref(L.new_id(), "path", pos)
    path.name = "route_" + targetname
    pts = []
    for dx, dy in ((-h, -h), (h, -h), (h, h), (-h, h)):
        pt = [pos[0] + dx, pos[1] + dy, pos[2]]
        scene = getattr(ctx, "scene", None)
        if scene is not None:
            try:
                pt = scene.drop_to_floor(pt, up=64.0)
            except Exception:
                pass
        pp = wd.PathPoint(pt)
        pts.append(pp)
    pts[0].wait = 3.0
    pts[2].wait = 3.0
    path.points = pts
    L.refs.append(path)
    npc = NPC(targetname)
    npc.display_name = _t(p, "Юнит ГО", "CP Unit")
    a = npc.actor
    a.classname = "npc_metropolice"
    a.weapon = weapon.currentData()
    if any(x.id == "police" for x in p.factions):
        a.faction = "police"
    pk = wd.Package("patrol")
    pk.name = "Патруль"
    pk.start, pk.duration = 0.0, 24.0
    pk.path = path.name
    pk.path_mode = "loop"
    a.packages = [pk]
    r = wd.Ref(L.new_id(), "npc", pts[0].pos)
    r.angles = [0.0, round(yaw / 15) * 15 % 360, 0.0]
    r.npc = targetname
    L.refs.append(r)
    p.npcs.append(npc)
    _finish(ctx, npc, "патруль %s, маршрут %s" % (targetname, path.name))
