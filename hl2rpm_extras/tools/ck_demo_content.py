"""Demo content of the Creation Kit on the test map ck_test (stays in the test project as an example):
a locked storage locker (Lockpick 50, key behind the house, or the caretaker's terminal), a junk crate with
bobby pins (loot list ck_junk), the caretaker's note = the terminal password, a PC terminal on the kitchen table,
a Combine post (console sector17, floor turret ck_turret neutral to the map's citizens, barrier ck_field,
cache), the quest "The caretaker's storage" given on entering the map.
Part 2 - a "living" quarter: the trader Syoma at a stall (goods, a ration item, his crate - stealing), barks of
the patrol / trader / citizens with conditions, shared dialog topics, zombies in the pen behind the house
(spawner + template; the walls: make_ck_test.py --pen), music by day / night / combat / location, locations with
soundscapes (the yard: own ones at night and in the rain), a Combine hum, NPC confidence.
Parts 3-5 - the user's addons (Combine units, workers, melee weapons, props, suits, zombies) and the Beretta 92.
Re-running replaces the same records (ids 40-79 of the layer, items, lists, terminals, quest, event, NPCs,
tracks, soundscapes, bark set, topics).
    <CK venv python> ck_demo_content.py [--export]      (--export: write the game files as Ctrl+S does)"""
import os
import sys
CK = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dialog_editor")
sys.path.insert(0, CK)
from dlgedit import model, rpgdata, termdata, validate, exporter  # noqa: E402
from dlgedit.worlddata import Ref  # noqa: E402

PATH = r"E:/SourceModding/HL2RPMSOURCE/dialogsrc/test_dialogs.hl2dlg"
p = model.Project.load(PATH)


def t(en, ru):
    return {"english": en, "russian": ru}


# ---- items ----------------------------------------------------------------
def put_item(**kw):
    p.items[:] = [i for i in p.items if i.id != kw["id"]]
    p.items.append(rpgdata.ItemDef(**kw))


put_item(id="key_storage", base="key", category="key", name=t("Storage key", "Ключ от склада"),
         desc=t("A heavy key from the padlock of the storage locker by the gate.",
                "Тяжёлый ключ от навесного замка на шкафчике склада у ворот."),
         model="models/hl2rpm/items/key_old.mdl", weight=0.1, value=5, quest=True)
# (no model of its own: every key without one is the brass key with a tag)
put_item(id="key_spare", base="key", category="key", name=t("Spare key", "Запасной ключ"),
         desc=t("A key on a ring, with a tag: No. 17. Which door it opens, nobody remembers.",
                "Ключ на кольце с биркой: №17. От какой двери - уже никто не помнит."),
         weight=0.05, value=2)
put_item(id="note_caretaker", base="note", category="note", name=t("Caretaker's note", "Записка смотрителя"),
         desc=t("A crumpled sheet with a password written on it.", "Мятый листок, на нём записан пароль."),
         text=t("I never remember the terminal password, so here it is: GROZA-17.\n\n"
                "The storage key is hidden behind the house, by the east wall.\n"
                "If I lose both - there are bobby pins in the junk crate at the gate.\n\n- The caretaker",
                "Пароль от терминала я так и не запомнил, пусть будет тут: ГРОЗА-17.\n\n"
                "Ключ от шкафчика склада спрятал за домом, у восточной стены.\n"
                "Потеряю и то и другое - в ящике с хламом у ворот есть шпильки.\n\n- Смотритель"),
         model="models/props_c17/paper01.mdl", weight=0.0, value=0)
put_item(id="storage_ledger", base="generic", category="quest", name=t("Storage ledger", "Журнал склада"),
         desc=t("Who took what from the block storage. Civil Protection would pay for it.",
                "Кто и что брал со склада блока. Гражданская оборона за него заплатила бы."),
         model="models/props_lab/binderblue.mdl", weight=0.5, value=40, quest=True)

# ---- loot list --------------------------------------------------------------
p.lootlists[:] = [l for l in p.lootlists if l.id != "ck_junk"]
junk = rpgdata.LootList(id="ck_junk", note="Ящик с хламом у ворот ck_test: шпильки, жетоны, мелочь")
junk.entries = [
    {"item": "bobby_pin", "min": 2, "max": 4, "chance": 100, "level": 0},
    {"item": "rpm_credits", "min": 5, "max": 25, "chance": 80, "level": 0},
    {"item": "item_battery", "min": 1, "max": 1, "chance": 40, "level": 0},
    {"item": "item_healthvial", "min": 1, "max": 1, "chance": 50, "level": 0},
    {"item": "key_spare", "min": 1, "max": 1, "chance": 100, "level": 0},
]
p.lootlists.append(junk)


# ---- terminals ----------------------------------------------------------------
def entry(key, title, text=None, actions=None, result=None, entries=None, once=False):
    e = termdata.TermEntry(key)
    e.title = title
    e.text = text or {}
    e.actions = actions or []
    e.result = result or {}
    e.entries = entries or []
    e.once = once
    return e


pc = termdata.TerminalDef("office_pc")
pc.note = "ck_test: дом, кухня, стол. Пароль - записка смотрителя на кровати"
pc.style = "pc"
pc.title = t("Caretaker terminal", "Терминал смотрителя")
pc.header = t("RESIDENTIAL BLOCK 7 - CARETAKER\nCity Administration network", "ЖИЛОЙ БЛОК 7 - СМОТРИТЕЛЬ\nСеть городской администрации")
pc.lock_level = 50
pc.password_item = "note_caretaker"
pc.entries = [
    entry("1", t("Diary", "Дневник"),
          t("Day 212. The CPs took the generator again. I locked what was left in the locker by the gate.\n\n"
            "Day 215. Wrote the ledger of who took what. If they find it, I am done. Keep it in the locker.\n\n"
            "Day 216. Hid the key behind the house. The terminal opens the lock too - the old wiring still works.",
            "День 212. ГО опять забрали генератор. Что осталось, запер в шкафчике у ворот.\n\n"
            "День 215. Записал в журнал, кто и что брал. Найдут - мне конец. Пусть лежит в шкафчике.\n\n"
            "День 216. Ключ спрятал за домом. Замок открывается и отсюда - старая проводка ещё работает.")),
    entry("2", t("Orders", "Приказы"), entries=[
        entry("1", t("Civil Protection order 17", "Приказ ГО №17"),
              t("All residents of block 7 report to the inspection point at 18:00.\nNon-compliance - relocation.",
                "Всем жильцам блока 7 явиться на пункт досмотра в 18:00.\nНеявка - переселение.")),
        entry("2", t("Ration norm", "Норма пайков"),
              t("Ration per resident: 1 unit per day.\nLoyal residents: 2 units.\nThe caretaker distributes rations from the storage.",
                "Паёк на жильца: 1 единица в сутки.\nЛояльным - 2 единицы.\nПайки выдаёт смотритель со склада.")),
    ]),
    entry("3", t("Unlock the storage locker", "Отпереть шкафчик склада"),
          actions=[{"type": "door", "target": "ck_storage", "op": "unlock"}],
          result=t("The lock of the locker clicks open.", "Замок шкафчика щёлкнул и открылся.")),
]

post = termdata.TerminalDef("sector17")
post.note = "ck_test: пост Альянса в юго-западном углу - турель ck_turret и барьер ck_field"
post.style = "combine"
post.title = t("Overwatch post 17", "Пост Надзора 17")
post.header = t("OVERWATCH SECTOR 17\nAutomated defense", "НАДЗОР: СЕКТОР 17\nАвтоматическая оборона")
post.lock_level = 25
post.entries = [
    entry("1", t("Post status", "Состояние поста"),
          t("TURRET: ACTIVE\nBARRIER: ACTIVE\nCACHE: SEALED\n\nUnauthorized access is logged.",
            "ТУРЕЛЬ: АКТИВНА\nБАРЬЕР: АКТИВЕН\nТАЙНИК: ОПЕЧАТАН\n\nНесанкционированный доступ фиксируется.")),
    entry("2", t("Disable the turret", "Отключить турель"),
          actions=[{"type": "turret", "target": "ck_turret", "op": "off"}],
          result=t("Turret: offline.", "Турель: отключена."), once=True),
    entry("3", t("Retarget the turret (friendly)", "Перепрограммировать турель (свой)"),
          actions=[{"type": "turret", "target": "ck_turret", "op": "friendly"}],
          result=t("Turret: friendly identification accepted.", "Турель: опознавание «свой» принято."), once=True),
    entry("4", t("Lower the barrier", "Снять барьер"),
          actions=[{"type": "field", "target": "ck_field", "op": "off"}],
          result=t("Barrier: down.", "Барьер: снят."), once=True),
]
p.terminals[:] = [x for x in p.terminals if x.id not in ("office_pc", "sector17")] + [pc, post]

# ---- quest and the event that gives it ----------------------------------------
q = model.Quest("ck_storage")
q.title = t("The caretaker's storage", "Склад смотрителя")
q.desc = t("The block caretaker locked the storage locker at the gate. His ledger is inside.",
           "Смотритель жилого блока запер шкафчик склада у ворот. Внутри - его журнал.")
st = model.Stage()
st.title = t("Open the storage locker", "Открыть шкафчик склада")
st.desc = t("The locker by the gate is locked (Lockpick 50). The key or the caretaker's terminal may help - look in the house.",
            "Шкафчик у ворот заперт (Взлом 50). Помогут ключ или терминал смотрителя - поищите в доме.")
o1 = model.Objective("o1")
o1.type, o1.target, o1.radius = "go", "ck_storage", 96
o1.text = t("Find the storage locker at the gate", "Найти шкафчик склада у ворот")
o2 = model.Objective("o2")
o2.type, o2.item = "collect", "storage_ledger"
o2.text = t("Take the storage ledger", "Забрать журнал склада")
st.objectives = [o1, o2]
st.final = True
q.stages = [st]
q.xp = 100
q.note = "Демо ck_test (M6-M7): замок, ключ, записка-пароль, терминал"
p.quests[:] = [x for x in p.quests if x.id != q.id] + [q]

ev = model.Event("ck_demo_start")
ev.on, ev.map, ev.once, ev.delay = "map_enter", "ck_test", True, 3.0
ev.actions = [{"type": "quest_start", "quest": "ck_storage"}]
ev.note = "Демо ck_test: задание «Склад смотрителя» при входе на карту"
p.events[:] = [x for x in p.events if x.id != ev.id] + [ev]

# ---- the map layer -----------------------------------------------------------------
layer = p.maps["ck_test"]
DEMO_IDS = set(range(40, 60))
layer.refs[:] = [r for r in layer.refs if r.id not in DEMO_IDS]


def ref(rid, rtype, pos, yaw=0.0, **kw):
    r = Ref(rid, rtype, pos)
    r.angles = [0.0, float(yaw), 0.0]
    for k, v in kw.items():
        setattr(r, k, v)
    layer.refs.append(r)
    return r


# furniture markers where the bodies fit the furniture (seen with the CK mannequins): sitcouch1 puts the pelvis
# 24 units behind the marker (the marker stood in the middle of the couch - the body sat behind its back),
# laycouch1 lies along the marker's own y axis (it lay across the bed)
for r in layer.refs:
    if r.id == 13 and r.name == "ck_couch":
        r.pos = [904.0, -220.0, 4.0]
    elif r.id == 10 and r.name == "ck_bed":
        r.pos = [934.0, 218.0, 4.0]
        r.angles = [0.0, 270.0, 0.0]

# the existing locker (#31) becomes the storage: lock 50, the key, the ledger inside
locker = next(r for r in layer.refs if r.id == 31)
locker.name = "ck_storage"
locker.display_name = t("Storage locker", "Шкафчик склада")
locker.loot_items = [["storage_ledger", 1], ["weapon_pistol", 1], ["item_ammo_pistol", 36], ["rpm_credits", 60]]
locker.keyvalues["lock_level"] = "50"
locker.keyvalues["lock_key"] = "key_storage"
locker.note = "Демо: Взлом 50, ключ key_storage за домом, или терминал office_pc"

ref(40, "container", (-1000, 300, 20), 10, name="ck_junkbox", model="models/props_junk/wood_crate001a.mdl",
    loot_items=[["list:ck_junk", 1]], display_name=t("Junk crate", "Ящик с хламом"), note="Демо: шпильки для взлома")
ref(49, "prop", (1090, 60, 20), 20, model="models/props_junk/wood_crate001a.mdl", note="Демо: ящик за домом, на нём ключ")
ref(41, "item", (1090, 60, 42), 70, classname="key_storage", note="Демо: ключ от шкафчика ck_storage (на ящике за домом)")
ref(42, "item", (930, 225, 44), 30, classname="note_caretaker", note="Демо: записка с паролем терминала office_pc")
ref(50, "item", (-1000, 300, 44), 40, classname="bobby_pin", note="Демо: шпилька на ящике с хламом")
# the suits in a row, west of the junk crate (folded on the ground since 2026-11-02, part 4 adds three)
for n, cls in enumerate(("item_suit_cp", "item_combine_soldier_suit", "item_suit_prison", "item_suit_elite", "item_suit_pcv")):
    ref(51 + n, "item", (-1180, -120 + n * 60, 4), 0, classname=cls, note="Демо: костюм - сложенная одежда и твёрдые части рядом")
ref(43, "terminal", (520, -200, 52.1), 180, name="office_pc", terminal="office_pc", model="models/props_lab/monitor01a.mdl",
    note="Демо: терминал ПК (Наука 50 или записка)")
# Combine post in the south-west corner: console, turret looking into the corner at the cache, barrier
ref(44, "terminal", (-1000, -1150, 0), 90, name="sector17", terminal="sector17",
    model="models/props_combine/combine_intmonitor001.mdl", note="Демо: консоль Альянса (Наука 25)")
ref(45, "entity", (-1250, -1150, 0), 225, name="ck_turret", classname="npc_turret_floor", note="Демо: турель поста 17")
ref(46, "entity", (-1250, -1150, 72), 0, name="ck_turret_rel", classname="ai_relationship",
    keyvalues={"subject": "ck_turret", "target": "npc_citizen", "disposition": "3", "rank": "99", "StartActive": "1", "reciprocal": "1"},
    note="Демо: турель и горожане карты - свои (именно D_LI: горожане HL2 боятся любой активной турели, и страх держит их в бою)")
ref(47, "prop", (-1250, -1040, 30), 90, name="ck_field", model="models/props_combine/combine_barricade_short01a.mdl",
    note="Демо: барьер поста 17 (снимается с консоли)")
ref(48, "container", (-1420, -1420, 20), 45, name="ck_cache", model="models/props_junk/wood_crate001a.mdl",
    loot_items=[["rpm_credits", 40], ["item_battery", 1], ["item_ammo_smg1", 45], ["bobby_pin", 2]],
    display_name=t("Combine cache", "Тайник Альянса"), keyvalues={"owner": "police"},
    note="Демо: тайник под турелью (имущество фракции police: если патрульный видит - кража)")

# ==== part 2: a "living" quarter - trader, patrol and citizens with barks, zombies in the old pen,
# music day / night / combat / locations, locations with soundscapes, a sound on the map ====
from dlgedit import sounddata  # noqa: E402
from dlgedit.worlddata import Actor, Package  # noqa: E402
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_ck_test import lowest_offset  # noqa: E402

put_item(id="ck_ration", base="generic", category="food", name=t("Ration pack", "Паёк Альянса"),
         desc=t("A standard Combine ration: a nutrient bar and a can of water.",
                "Стандартный паёк Альянса: питательный брикет и банка воды."),
         model="models/weapons/w_package.mdl", weight=0.4, value=12, heal=10, stackable=True)
p.lootlists[:] = [l for l in p.lootlists if l.id != "ck_trader_goods"]
goods = rpgdata.LootList(id="ck_trader_goods", note="Товар торговца Сёмы на ck_test")
goods.entries = [
    {"item": "bobby_pin", "min": 4, "max": 8, "chance": 100, "level": 0},
    {"item": "ck_ration", "min": 3, "max": 6, "chance": 100, "level": 0},
    {"item": "item_healthvial", "min": 2, "max": 4, "chance": 100, "level": 0},
    {"item": "item_healthkit", "min": 1, "max": 2, "chance": 70, "level": 0},
    {"item": "item_battery", "min": 1, "max": 2, "chance": 60, "level": 0},
    {"item": "item_ammo_pistol", "min": 18, "max": 36, "chance": 80, "level": 0},
    # (part 5: the Beretta 92 - hl2rpm_beretta.cpp)
    {"item": "weapon_rpm_beretta", "min": 1, "max": 1, "chance": 100, "level": 0},
]
p.lootlists.append(goods)


def put_npc(n):
    p.npcs[:] = [x for x in p.npcs if x.targetname != n.targetname]
    p.npcs.append(n)
    return n


def node(nid, line, options=(), end=False, actions=None, pos=(0, 0)):
    nd = model.Node(nid)
    nd.line = line
    nd.end = end
    nd.actions = actions or []
    nd.pos = [float(pos[0]), float(pos[1])]
    nd.options = list(options)
    return nd


def opt(text, nxt=-1, close=False, trade=False, conditions=None, actions=None):
    o = model.Option()
    o.text = text
    o.next = nxt
    o.close = close
    o.trade = trade
    o.conditions = conditions or []
    o.actions = actions or []
    return o


def bark(bid, event, text, cooldown=30.0, weight=1.0, conditions=None, once=False):
    b = sounddata.Bark(bid, event)
    b.text = text
    b.cooldown = cooldown
    b.weight = weight
    b.conditions = conditions or []
    b.once = once
    return b


QUEST_DONE = {"type": "quest_state", "quest": "ck_storage", "value": "completed"}

# the trader at a stall in the north of the yard: trade, a rumour, buys the storage ledger
tr = model.NPC("ck_trader")
tr.display_name = t("Syoma the trader", "Торговец Сёма")
tr.note = "Демо ck_test: торговец (вариант «Покажи товар» = торговля), фразы, покупает журнал склада"
tr.nodes = [
    node(0, t("Got tokens? Then I've got goods. Bobby pins, bandages, rations - all honest.",
              "Жетоны есть? Тогда и товар есть. Шпильки, бинты, пайки - всё честно."), pos=(260, 0), options=[
        opt(t("Show me your goods.", "Покажи товар."), trade=True),
        opt(t("What's new in the block?", "Что слышно в блоке?"), nxt=10),
        opt(t("I have the storage ledger. Want it?", "Журнал склада у меня. Возьмёшь?"), nxt=20,
            conditions=[{"type": "item", "item": "storage_ledger", "op": ">=", "value": 1}],
            actions=[{"type": "take", "item": "storage_ledger", "count": 1}, {"type": "money", "value": 80},
                     {"type": "xp", "value": 25}, {"type": "flag", "name": "ck_ledger_sold", "value": 1}]),
        opt(t("See you.", "Бывай."), close=True),
    ]),
    node(10, t("CPs walk the yard at night. And behind the house, in the old pen, something moans again. "
               "I don't go there, and I wouldn't advise you to.",
               "ГО по ночам ходят по двору. А за домом, в старом загоне, опять кто-то стонет. "
               "Я туда не хожу и тебе не советую."), pos=(560, -60), options=[
        opt(t("One more thing...", "Ещё кое-что..."), nxt=0),
        opt(t("See you.", "Бывай."), close=True),
    ]),
    node(20, t("Hm... The CPs would pay more, but I sleep better when it's with me. Here are your tokens.",
               "Хм... ГО за такое заплатили бы больше, но мне спокойнее, когда он у меня. Держи жетоны."),
         end=True, pos=(560, 120)),
]
tr.returning.line = t("You again? Well, take your pick.", "Опять ты? Ну, выбирай.")
tr.returning.next = 0
tr.actor = Actor()
tr.actor.classname = "npc_citizen"
tr.actor.model = "models/humans/group02/male_08.mdl"
tr.actor.faction = "workers"
tr.actor.essential = True
tr.actor.confidence = 0          # a coward: runs from any enemy at once (zombies from the pen)
tr.actor.vendor = {"list": "list:ck_trader_goods", "money": 300, "markup": 1.25, "pays": 0.5, "restock_hours": 24,
                   "currency": "rpm_credits"}
stall = Package("guard")
stall.name = "Прилавок"
stall.start, stall.duration = 0.0, 24.0
stall.location, stall.ref = "ref", "ck_stall"
tr.actor.packages = [stall]
tr.actor.barks = [
    bark("greet1", "greet", t("Come over, don't be shy!", "Подходи, не стесняйся!"), cooldown=90),
    bark("greet2", "greet", t("Rations, fresh from the dispenser!", "Пайки, свежие, прямо из раздатчика!"), cooldown=90),
    bark("greet3", "greet", t("Heard the caretaker's storage got opened. Wasn't you, was it?",
                              "Слыхал, склад смотрителя вскрыли. Не ты ли?"), cooldown=120, weight=3, conditions=[QUEST_DONE]),
    bark("theft1", "theft", t("Hey! Put that back!", "Эй! Положи на место!"), cooldown=5),
]
put_npc(tr)

# a zombie template for the spawn point (GECK template: the spawner picks it by weight)
zt = model.NPC("tpl_zombie")
zt.display_name = t("Zombie", "Зомби")
zt.note = "Демо ck_test: шаблон зомби для точки появления в загоне"
zt.actor = Actor()
zt.actor.classname = "npc_zombie"
zt.actor.is_template = True
zt.actor.xp = 20
zt.actor.loot_items = [["rpm_credits", 3]]
put_npc(zt)

# shared dialog topics (New Vegas): every citizen / every Civil Protection officer of the project answers them;
# the workers have no lines of their own - they get a short dialog: the greeting, the topics, "Bye"
from dlgedit.model import Topic  # noqa: E402


def topic(tid, q, a, classes=(), factions=(), conditions=None, once=False, note=""):
    tp = Topic(tid)
    tp.classes, tp.factions = list(classes), list(factions)
    tp.text, tp.line = q, a
    tp.conditions = conditions or []
    tp.once = once
    tp.note = note
    return tp


ON_CK_TEST = {"type": "map", "value": "ck_test"}
CK_TOPICS = [
    topic("ck_t_life", t("How's life here?", "Как тут живётся?"),
          t("CPs walk the yard at night, rations were cut again, and something moans in the old pen behind the house. We live, more or less.",
            "ГО по ночам ходят по двору, пайки опять урезали, а за домом в старом загоне кто-то стонет. Живём, в общем."),
          classes=["npc_citizen"], conditions=[ON_CK_TEST], note="Демо ck_test: общая тема всех горожан (только на ck_test)"),
    topic("ck_t_key", t("Do you know where the caretaker's storage key is?", "Не знаешь, где ключ от склада смотрителя?"),
          t("He always hid things behind the house, by the east wall. But I told you nothing.",
            "Смотритель вечно что-то прятал за домом, у восточной стены. Только я тебе ничего не говорил."),
          classes=["npc_citizen"], conditions=[ON_CK_TEST, {"type": "quest_state", "quest": "ck_storage", "value": "active"}], once=True,
          note="Демо ck_test: подсказка к заданию «Склад смотрителя», пока оно идёт; у каждого - один раз"),
    topic("ck_t_rules", t("What are the rules in the block?", "Какие правила в блоке?"),
          t("Curfew from 22:00. No gatherings. No questions. Move along.",
            "Комендантский час с 22:00. Не собираться. Не задавать вопросов. Проходи."),
          factions=["police"], conditions=[ON_CK_TEST], note="Демо ck_test: тема фракции police"),
]
ids_t = {x.id for x in CK_TOPICS}
p.topics[:] = [x for x in p.topics if x.id not in ids_t] + CK_TOPICS
p.topic_greeting = t("Yes?", "Чего тебе?")

# the talker's lines were a hand-written file (scripts/dialogs/ck_talker.txt) - now the project's, so the topics join them
talker = next(n for n in p.npcs if n.targetname == "ck_talker")
if not talker.has_dialog():
    talker.nodes = [node(0, t("Hey! Wait up, I wanted to talk to you.", "Эй! Погоди, я хотел с тобой поговорить."), pos=(260, 0),
                         options=[opt(t("Bye.", "Пока."), close=True)])]
    talker.start_node = 0

# the workers flee when hurt to half of their health (New Vegas "cautious")
for n in p.npcs:
    if n.targetname == "ck_worker":
        n.actor.confidence = 1

# the patrolling Civil Protection officer speaks: greetings, the curfew at night, the storage after the quest
guard = next(n for n in p.npcs if n.targetname == "ck_guard")
guard.actor.barks = [
    bark("greet1", "greet", t("Move along, citizen.", "Проходи, гражданин."), cooldown=60),
    bark("greet2", "greet", t("Somebody opened the caretaker's storage. Seen anything, citizen?",
                              "Склад смотрителя кто-то вскрыл. Ничего не видел, гражданин?"),
         cooldown=120, weight=3, conditions=[QUEST_DONE]),
    bark("idle1", "idle", t("Sector seventeen: no violations.", "Сектор семнадцать: нарушений не выявлено."), cooldown=60),
    bark("idle2", "idle", t("Curfew. Return to your quarters.", "Комендантский час. Разойтись по квартирам."),
         cooldown=60, weight=3, conditions=[{"type": "time", "from": 22.0, "to": 6.0}]),
    bark("weapon1", "weapon", t("Put the weapon away!", "Убрать оружие!"), cooldown=20),
    bark("combat1", "combat", t("Violator! Applying force!", "Нарушитель! Применяю силу!"), cooldown=15),
    bark("theft1", "theft", t("Freeze! That is Combine property!", "Стоять! Это имущество Альянса!"), cooldown=5),
]

# shared barks of all citizens (also those placed in Hammer): by weather, time of day, news
cit = sounddata.BarkSet("ck_citizens")
cit.classes = ["npc_citizen"]
cit.note = "Демо ck_test: общие фразы горожан (погода, время суток)"
cit.barks = [
    bark("greet1", "greet", t("Hi.", "Привет."), cooldown=60),
    bark("greet2", "greet", t("Oh, a new face...", "О, новенький..."), cooldown=120, once=True),
    bark("idle1", "idle", t("They say the CPs will relocate the whole block soon...",
                            "Говорят, ГО скоро весь блок переселят..."), cooldown=90),
    bark("idle2", "idle", t("Pouring again... the roof leaks.", "Опять льёт... а крыша в блоке течёт."),
         cooldown=90, weight=3, conditions=[{"type": "weather", "value": "drizzle;rain;thunderstorm"}]),
    bark("idle3", "idle", t("Time to sleep, another shift tomorrow.", "Пора спать, завтра опять смена."),
         cooldown=90, weight=3, conditions=[{"type": "time", "from": 21.0, "to": 5.0}]),
    bark("hurt1", "hurt", t("Ow! What are you doing?!", "Ай! Ты что творишь?!"), cooldown=10),
    bark("combat1", "combat", t("Help!", "Помогите!"), cooldown=15),
]
p.bark_sets[:] = [x for x in p.bark_sets if x.id != cit.id] + [cit]

# music of the map: exploration day / night, combat, locations, stingers (HL2 tracks)


def track(tid, f, kind, time="any", vol=1.0, event="", note=""):
    m = sounddata.MusicTrack(tid)
    m.file, m.kind, m.time, m.volume, m.event, m.maps, m.note = f, kind, time, vol, event, ["ck_test"], note
    return m


CK_TRACKS = [
    track("ck_day", "music/hl2_song26_trainstation1.mp3", "explore", "day", 0.8, note="Демо ck_test: день"),
    track("ck_night", "music/hl2_song27_trainstation2.mp3", "explore", "night", note="Демо ck_test: ночь"),
    track("ck_fight", "music/hl2_song3.mp3", "combat", note="Демо ck_test: бой"),
    track("ck_fight2", "music/hl2_song20_submix0.mp3", "combat", note="Демо ck_test: бой"),
    track("ck_post", "music/hl2_song19.mp3", "location", note="Демо ck_test: пост Надзора"),
    track("ck_waste", "music/ravenholm_1.mp3", "location", note="Демо ck_test: пустырь с зомби"),
    track("ck_st_quest", "music/hl2_song23_suitsong3.mp3", "stinger", event="quest_complete", note="Демо ck_test"),
    track("ck_st_loc", "music/hl2_song25_teleporter.mp3", "stinger", event="location", note="Демо ck_test"),
]
ids = {m.id for m in CK_TRACKS}
p.music[:] = [m for m in p.music if m.id not in ids] + CK_TRACKS


def scape(name, note, loops, randoms, dsp=1):
    s = sounddata.Soundscape(name)
    s.dsp, s.note = dsp, note
    s.loops = [sounddata.new_loop(w, v) for w, v in loops]
    s.randoms = [sounddata.new_random(ws, tm, vol) for ws, tm, vol in randoms]
    return s


CK_SCAPES = [
    scape("hl2rpm.ck_yard", "Демо ck_test: двор - город, голуби, техника вдали",
          [("ambient/atmosphere/plaza_amb.wav", 0.45), ("ambient/atmosphere/city_rumble_loop1.wav", 0.25)],
          [(["ambient/creatures/pigeon_idle%d.wav" % i for i in range(1, 5)], (6, 15), (0.3, 0.5)),
           (["ambient/levels/streetwar/apc_distant%d.wav" % i for i in range(1, 4)], (25, 50), (0.3, 0.5)),
           (["ambient/machines/heli_pass_distant1.wav", "ambient/machines/aircraft_distant_flyby1.wav"], (40, 80), (0.3, 0.45))]),
    scape("hl2rpm.ck_yard_night", "Демо ck_test: двор ночью - тишина, шаги патрулей и вертолёт вдали",
          [("ambient/atmosphere/tone_quiet.wav", 0.4), ("ambient/atmosphere/city_rumble_loop1.wav", 0.15)],
          [(["ambient/levels/canals/critter%d.wav" % i for i in range(1, 9)], (5, 12), (0.2, 0.35)),
           (["ambient/levels/streetwar/marching_distant1.wav", "ambient/levels/streetwar/marching_distant2.wav"], (30, 60), (0.25, 0.4)),
           (["ambient/machines/heli_pass_distant1.wav"], (45, 90), (0.25, 0.4))]),
    scape("hl2rpm.ck_yard_rain", "Демо ck_test: двор в дождь - капель, город глуше",
          [("ambient/atmosphere/plaza_amb.wav", 0.25)],
          [(["ambient/levels/canals/drip%d.wav" % i for i in range(1, 5)], (2, 5), (0.3, 0.5)),
           (["ambient/levels/streetwar/apc_distant%d.wav" % i for i in range(1, 4)], (30, 60), (0.2, 0.35))]),
    scape("hl2rpm.ck_house", "Демо ck_test: в доме - тишина и радио за стеной",
          [("ambient/atmosphere/indoor1.wav", 0.35)],
          [(["ambient/levels/prison/radio_random%d.wav" % i for i in range(1, 16)], (6, 14), (0.2, 0.35))], dsp=2),
    scape("hl2rpm.ck_post", "Демо ck_test: пост Надзора - гул и консоли Альянса",
          [("ambient/atmosphere/city_beacon_loop1.wav", 0.35), ("ambient/atmosphere/drone1lp.wav", 0.25)],
          [(["ambient/machines/combine_terminal_idle%d.wav" % i for i in range(1, 5)], (5, 12), (0.3, 0.5))]),
    scape("hl2rpm.ck_waste", "Демо ck_test: пустырь - ветер, мухи, стоны",
          [("ambient/wind/wasteland_wind.wav", 0.5)],
          [(["ambient/creatures/town_zombie_call1.wav", "ambient/creatures/town_moan1.wav"], (12, 25), (0.3, 0.5)),
           (["ambient/wind/wind_moan1.wav", "ambient/wind/wind_moan2.wav", "ambient/wind/wind_moan4.wav"], (10, 20), (0.3, 0.5)),
           (["ambient/creatures/flies%d.wav" % i for i in range(1, 6)], (4, 9), (0.2, 0.35))]),
]
names = {s.name for s in CK_SCAPES}
p.soundscapes[:] = [s for s in p.soundscapes if s.name not in names] + CK_SCAPES

# the layer: stall (farther than 700 from ck_talker - he would hail the buyer), zombie pen (walls in the map: make_ck_test.py), locations, a Combine hum
DEMO2_IDS = set(range(60, 80))
layer.refs[:] = [r for r in layer.refs if r.id not in DEMO2_IDS]


def floor_prop(rid, model_, x, y, yaw, **kw):
    return ref(rid, "prop", (x, y, round(lowest_offset(model_, yaw), 2)), yaw, model=model_, **kw)


floor_prop(60, "models/props_c17/furnituretable002a.mdl", 250, 1000, 0, note="Демо: прилавок торговца Сёмы")
floor_prop(61, "models/props_junk/cardboard_box001a.mdl", 320, 1005, 25, note="Демо: прилавок")
ref(74, "container", (330, 1080, 20), 300, name="ck_trader_box", model="models/props_junk/wood_crate001a.mdl",
    loot_items=[["ck_ration", 4], ["rpm_credits", 30], ["item_healthvial", 1]], display_name=t("Trader's crate", "Ящик торговца"),
    keyvalues={"owner": "ck_trader"}, note="Демо: чужое - владелец Сёма (красное «Украсть»; увидит - убежит, фраза «кража»)")
ref(62, "marker", (250, 1060, 0), 270, name="ck_stall", kind="guard", owner="ck_trader", note="Демо: место торговца за прилавком")
ref(63, "npc", (250, 1060, 0), 270, npc="ck_trader", note="Демо: торговец Сёма")
floor_prop(64, "models/props_junk/trashdumpster01a.mdl", 1420, 1420, 200, note="Демо: загон")
floor_prop(65, "models/props_c17/oildrum001.mdl", 1120, 1060, 0, note="Демо: загон")
floor_prop(66, "models/props_c17/oildrum001.mdl", 1150, 1110, 40, note="Демо: загон")
ref(67, "spawner", (1300, 1250, 0), 0, name="ck_zombies", note="Демо: зомби в загоне за домом (пополняются через 6 ч)",
    keyvalues={"npcs": "tpl_zombie:3;npc_headcrab:1", "count_min": "2", "count_max": "3", "radius": "160",
               "activate_dist": "3000", "from_hour": "-1", "to_hour": "-1", "respawn_hours": "6"})
ref(68, "sound", (-1000, -1150, 40), 0, sound="ambient/machines/combine_terminal_loop1.wav", volume=0.5, radius=500.0,
    note="Демо: гул консоли поста")
floor_prop(69, "models/props_lab/citizenradio.mdl", 1000, 120, 180, note="Демо: радио в спальне (звук - в звуковой сцене дома)")
# locations: the inner ones first (location music = the first zone with music the player is in)
ref(70, "zone", (704, 0, 0), 0, name="z_house", size=[640.0, 640.0, 170.0], display_name=t("Caretaker's house", "Дом смотрителя"),
    discover_xp=10, soundscape="hl2rpm.ck_house", note="Демо: локация")
ref(71, "zone", (-1250, -1250, 0), 0, name="z_post", size=[560.0, 560.0, 256.0], display_name=t("Overwatch post 17", "Пост Надзора 17"),
    discover_xp=15, music=["ck_post"], soundscape="hl2rpm.ck_post", note="Демо: локация")
ref(72, "zone", (1268, 1218, 0), 0, name="z_waste", size=[536.0, 636.0, 256.0], display_name=t("Wasteland", "Пустырь"),
    discover_xp=25, music=["ck_waste"], soundscape="hl2rpm.ck_waste", note="Демо: локация (зомби)")
ref(73, "zone", (0, 0, 0), 0, name="z_yard", size=[3072.0, 3072.0, 1024.0], display_name=t("Block 7 yard", "Двор блока 7"),
    discover_xp=5, soundscape="hl2rpm.ck_yard", soundscape_night="hl2rpm.ck_yard_night", soundscape_rain="hl2rpm.ck_yard_rain",
    note="Демо: локация - весь двор (ночью и в дождь - свои звуковые сцены)")

# ---- part 3 (2026-11-02): the user's addons - Combine workers, beta Combine units, the concept elite, zombie clothes
DEMO3_IDS = set(range(80, 110))
layer.refs[:] = [r for r in layer.refs if r.id not in DEMO3_IDS]


def unit(tn, en, ru, cls, mdl, weapon="", skin=0, skin_max=0, faction="police", note="", templ=False):
    n = model.NPC(tn)
    n.display_name = t(en, ru)
    n.note = note
    n.actor = Actor()
    n.actor.classname = cls
    n.actor.model = mdl
    n.actor.skin = skin
    n.actor.skin_max = skin_max
    n.actor.weapon = weapon
    n.actor.faction = faction
    n.actor.is_template = templ
    if not templ:
        pk = Package("guard")
        pk.start, pk.duration = 0.0, 24.0
        n.actor.packages = [pk]
    return put_npc(n)


C = "models/hl2rpm/characters/"
LINEUP = [
    ("ck_synth_elite", "Synth elite", "Синт-элита", C + "synth_elite.mdl", "weapon_ar2", 0, 0),
    ("ck_synth_elite2", "Synth elite", "Синт-элита", C + "synth_elite_green.mdl", "weapon_ar2", 0, 0),
    ("ck_super_soldier", "Super soldier", "Суперсолдат", C + "super_soldier_beta.mdl", "weapon_smg1", 0, 0),
    ("ck_super_soldier2", "Arctic super soldier", "Суперсолдат (арктика)", C + "super_soldier_beta.mdl", "weapon_smg1", 1, 1),
    ("ck_cremator", "Cremator", "Кремировщик", C + "cremator.mdl", "weapon_shotgun", 0, 0),
    ("ck_concept_elite", "Elite Overwatch", "Элита Надзора", "models/conceptbine_elite/conceptbine_elite_npc.mdl", "weapon_ar2", 0, 0),
]
for i, (tn, en, ru, mdl, wpn, sk, skm) in enumerate(LINEUP):
    unit(tn, en, ru, "npc_combine_s", mdl, wpn, sk, skm, note="Демо ck_test: новые юниты Альянса (аддоны пользователя)")
    ref(80 + i, "npc", (-760 + i * 90, -760, 0), 90, npc=tn, note="Демо: строй новых юнитов Альянса у поста 17")
for i in range(2):
    tn = "ck_cmb_worker%d" % (i + 1)
    unit(tn, "Combine worker", "Рабочий Альянса", "npc_citizen", "models/puntleycmb/workernpc.mdl",
         ("weapon_rpm_shovel", "weapon_rpm_pickaxe")[i], 0, 2, faction="workers",
         note="Демо ck_test: рабочие Альянса (три скина) с лопатой / киркой (оружие ближнего боя - и у NPC)")
    ref(88 + i, "npc", (-260 + i * 70, -520, 0), 60 + i * 40, npc=tn, note="Демо: рабочие Альянса во дворе")
# the melee weapons of the HL2 Melee Pack in a row next to the workers (hl2rpm_melee.cpp)
for i, w in enumerate(("axe", "bottle", "brokenbottle", "hook", "pan", "pickaxe", "pipe", "pot", "shovel")):
    ref(90 + i, "item", (-600 + i * 44, -430, 10), 90, classname="weapon_rpm_" + w, note="Демо: оружие ближнего боя")
# props of the HL2 Props Expansion Pack (the user's addon) along the south edge of the yard
for i, (m, yaw) in enumerate((("props_vehicles/car001d", 0), ("props_c17/furniturecouch003c", 90), ("props_c17/lockers003a", 90),
                              ("props_c17/engine_i6", 30), ("props_canal/generator_panel01", 90), ("props_lab/workspacetable004", 90),
                              ("props_c17/streetsign004a", 90), ("props_c17/bike_rack001", 90))):
    floor_prop(100 + i, "models/%s.mdl" % m, 40 + i * 150, -1150, yaw, note="Демо: пропы из HL2 Props Expansion Pack")
# the pen: zombies in other clothes (hl2rpm_zombies.cpp), zombines and zombie workers among them
unit("tpl_zombie_worker", "Zombie worker", "Зомби-рабочий", "npc_zombie", "models/puntleycmb/zombie_worker.mdl", skin_max=2, faction="",
     note="Демо ck_test: зомби-рабочий (три скина)", templ=True)
for r in layer.refs:
    if r.id == 67:
        r.keyvalues["npcs"] = "tpl_zombie:4;tpl_zombie_worker:2;npc_zombine:1;npc_headcrab:1"
        r.keyvalues["count_max"] = "4"

# ---- part 4 (2026-11-02, later): the suits lie folded on the ground, the work suits (coverall, hazmat), hazmat workers,
# the hazmat zombie of our own (make_zombie_hazmat.py) - refs 110..119
DEMO4_IDS = set(range(110, 120))
layer.refs[:] = [r for r in layer.refs if r.id not in DEMO4_IDS]
# the row of suits (refs 51..55 of part 1 and three more): folded piles now (make_suit_piles.py)
for i, cls in enumerate(("item_suit", "item_suit_worker", "item_suit_hazmat")):
    ref(110 + i, "item", (-1180, -120 + (5 + i) * 60, 4), 0, classname=cls,
        note="Демо: костюм (рабочие - со своим интерфейсом)" if i else "Демо: костюм H.E.V. сложенный")
for i in range(2):
    tn = "ck_hazmat%d" % (i + 1)
    unit(tn, "Hazmat worker", "Рабочий в химзащите", "npc_citizen", "models/hl2_combine_hazmat_cit.mdl",
         ("weapon_rpm_pipe", "")[i], 0, 0, faction="workers",
         note="Демо ck_test: рабочий Альянса в химзащите (аддон пользователя)")
    ref(113 + i, "npc", (-120 + i * 60, -560 + i * 90, 0), 120 + i * 60, npc=tn, note="Демо: рабочие в химзащите во дворе")
unit("ck_hazmat_cmb", "Hazmat trooper", "Солдат в химзащите", "npc_combine_s", "models/hl2_combine_hazmat.mdl", "weapon_smg1",
     note="Демо ck_test: солдат Альянса в химзащите (модель аддона на анимациях солдата)")
ref(115, "npc", (-760 + 6 * 90, -760, 0), 90, npc="ck_hazmat_cmb", note="Демо: строй новых юнитов Альянса у поста 17")
unit("tpl_zombie_hazmat", "Hazmat zombie", "Зомби в химзащите", "npc_zombie", "models/hl2rpm/characters/zombie_hazmat.mdl", faction="",
     note="Демо ck_test: зомби в химзащите (make_zombie_hazmat.py: костюм в крови, голова и когти классического зомби)", templ=True)
for r in layer.refs:
    if r.id == 67:
        r.keyvalues["npcs"] = "tpl_zombie:4;tpl_zombie_worker:2;tpl_zombie_hazmat:2;npc_zombine:1;npc_headcrab:1"

# ---- part 5 (2026-11-03): the Beretta 92 (make_beretta.py, hl2rpm_beretta.cpp) - refs 120..129: on the ground at
# in front of the melee row with a box of 9 mm; the trader sells one too (ck_trader_goods)
DEMO5_IDS = set(range(120, 130))
layer.refs[:] = [r for r in layer.refs if r.id not in DEMO5_IDS]
ref(120, "item", (-590, -370, 8), 90, classname="weapon_rpm_beretta", note="Демо: Беретта 92 (одна, в руках костюма)")
ref(121, "item", (-530, -370, 4), 90, classname="item_ammo_pistol", note="Демо: патроны 9 мм к Беретте")

# ---- part 6 (2026-11-03): the animation library (hl2rpm_extras/tools/anims) - the chairs at the table and the couch
# use the library's sitting (enter / long idle / exit, fitted by the seat height rpm_seat_h); the markers stand where
# "Подогнать к мебели" of the kit puts them for these props (dlgedit/world/seatfit.py)
FIT = {"ck_chair_1": ((520.0, -237.4, 4.0), 90.0, 15.9, "rpm_eat_chair"),       # breakfast: eating from a bowl
       "ck_chair_2": ((520.0, -162.6, 4.0), 270.0, 15.9, "rpm_sit_chair"),
       "ck_couch": ((899.9, -219.8, 4.0), 180.0, 16.8, "rpm_sit_couch")}
for r in layer.refs:
    if r.type == "marker" and r.name in FIT:
        pos, yaw, h, anim = FIT[r.name]
        r.pos = list(pos)
        r.angles = [0.0, yaw, 0.0]
        r.seat_h = h
        r.anim = anim
        r.note = "Демо: анимация библиотеки (%s), высота сиденья %.1f" % (anim, h)

# ---- part 7 (2026-11-03): more of the library - lying on the bed (the iron bed got a mattress, ref 122: without it
# the springs are 10 units above the floor), leaning on the house's west wall, sitting on the ground in the yard.
# The bed marker is where "Подогнать к кровати" of the kit puts it: the middle of the bed's north side facing away,
# the head to the marker's left (west); the mattress sags to 14.6 above the floor in the middle (16 at the rim).
# The note on the bed moves to the south edge of the mattress (the body lies along the middle).
DEMO7_IDS = {122}
layer.refs[:] = [r for r in layer.refs if r.id not in DEMO7_IDS]
ref(122, "prop", (930, 220, 17.2), 90, model="models/props_c17/furnituremattress001a.mdl", note="Демо: матрас на кровати")
FIT7 = {"ck_bed": ((929.4, 252.0, 4.0), 90.0, 14.6, "rpm_lie_bed"),
        "ck_lean": ((354.0, 160.0, 0.0), 180.0, -1.0, "rpm_lean_wall"),
        "ck_ground": ((-300.0, -620.0, 0.0), 45.0, -1.0, "rpm_sit_ground")}
for r in layer.refs:
    if r.type == "marker" and r.name in FIT7:
        pos, yaw, h, anim = FIT7[r.name]
        r.pos = list(pos)
        r.angles = [0.0, yaw, 0.0]
        r.seat_h = h
        r.anim = anim
        r.note = "Демо: анимация библиотеки (%s)%s" % (anim, ", высота матраса %.1f" % h if h >= 0 else "")
    elif r.id == 42:
        r.pos = [930.0, 205.0, 30.0]

# ---- part 8 (2026-11-03): more of the library - two people chatting with their hands (Kimodo motion) at two markers
# facing each other in the yard, searching on the haunches in front of the junk crate, sleeping rough by the fence
# (refs 123..126; no packages use them - rpm_marker_use <npc> <marker> to see them, or give a routine a package)
DEMO8_IDS = {123, 124, 125, 126}
layer.refs[:] = [r for r in layer.refs if r.id not in DEMO8_IDS]
for rid, nm, pos, yaw, kind, anim, note in (
        (123, "ck_chat_1", (-150, 150, 0), 0, "work", "rpm_talk", "Демо: разговор с жестами (лицом к ck_chat_2)"),
        (124, "ck_chat_2", (-90, 150, 0), 180, "work", "rpm_talk", "Демо: разговор с жестами (лицом к ck_chat_1)"),
        (125, "ck_search", (-961, 300, 0), 180, "work", "rpm_search", "Демо: обыскивает на корточках ящик с хламом"),
        (126, "ck_bedroll", (-560, 300, 0), 0, "sleep", "rpm_lie_ground", "Демо: спит на земле у забора (голова к забору)")):
    ref(rid, "marker", pos, yaw, name=nm, kind=kind, anim=anim, note=note)

# ---- part 9 (2026-11-04): everyone on ck_test speaks (voice_gen: Qwen3-TTS 1.7B on a Colab T4, Russian with the stress
# LoRA + RUAccent; cloned HL2 voices - CP and Combine through the radio with the stock on/off clicks, male01, female01 -
# and two voices made by description: the trader, the worker; the hazmat workers through a respirator). The files lie
# at their canonical places in testmaps (sound/dialogs/...); they are attached here so a re-run keeps them. A bark set
# has one recording per bark, so every voice other than the stock male citizen gets its own copy of the citizens'
# barks (the same texts) - otherwise the trader or the woman would say "Hi." in the voice of male01.
CIT_VARIANTS = [  # set id, voice (note), NPCs that use it instead of the class's ck_citizens
    ("ck_citizens_f", "женский голос", ["ck_wanderer"]),
    ("ck_citizens_t", "голос торговца", ["ck_trader"]),
    ("ck_citizens_w", "голос рабочего", ["ck_worker", "ck_cmb_worker2"]),
    ("ck_citizens_m", "горожанин в респираторе", ["ck_hazmat1"]),
    ("ck_citizens_wm", "рабочий в респираторе", ["ck_hazmat2"]),
]
variant_ids = []
for sid, voice_note, users in CIT_VARIANTS:
    v = sounddata.BarkSet.from_dict(cit.to_dict())
    v.id = sid
    v.classes = []
    v.note = "Демо ck_test: общие фразы горожан, %s (%s)" % (voice_note, ", ".join(users))
    for b in v.barks:
        b.voices = {}
    p.bark_sets[:] = [x for x in p.bark_sets if x.id != sid] + [v]
    variant_ids.append(sid)
    for n in p.npcs:
        if n.targetname in users:
            n.actor.bark_sets = [sid]
fem = p.bark_set("ck_citizens_f")
from dlgedit import voiceattach  # noqa: E402
CK_SPEAKERS = ["ck_guard", "ck_talker", "ck_wanderer", "ck_worker", "ck_trader", "ck_cmb_worker1", "ck_cmb_worker2",
               "ck_hazmat1", "ck_hazmat2", "ck_synth_elite", "ck_synth_elite2", "ck_super_soldier", "ck_super_soldier2",
               "ck_cremator", "ck_concept_elite", "ck_hazmat_cmb"]
print("voices attached:", len(voiceattach.attach_canonical_voices(p, CK_SPEAKERS, [cit.id] + variant_ids)))

errs = [x for x in validate.validate(p, check_files=False) if x.severity == "error"]
for x in errs:
    print("error:", x.npc, x.where, x.message)
print("validate:", "%d error(s)" % len(errs) if errs else "ok")
p.save(PATH)
print("saved", PATH)
if "--export" in sys.argv:
    r = exporter.Exporter(p).run()
    print(r.summary())
    p.save(PATH)
