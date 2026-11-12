"""Conditions, actions and events of the Creation Kit - the same language the game
speaks (game/server/hl2rpm_logic.cpp): dialog options and lines, quest stages and
objectives, AI packages, presence of map objects, the events of the "Logic" tab.

A condition / an action is a dict: {"type": ..., <params>...}; conditions also have
"not" and "or" (joined with the next one), actions "delay" (seconds). A text
parameter ("message") is a dict language -> text like every text of the project.
In the game files a list is a block of children named "c" / "a".
"""
from . import kv

OPS = [">=", "==", "<=", ">", "<", "!="]
OP_NAMES = {">=": "≥", "==": "=", "<=": "≤", ">": ">", "<": "<", "!=": "≠"}
QSTATES = [("active", "активно"), ("completed", "выполнено"), ("failed", "провалено"), ("inactive", "не взято")]
VAR_OPS = [("set", "="), ("add", "+"), ("sub", "−")]
# machines a terminal works (game/server/hl2rpm_logic.cpp RunMachine)
DOOR_OPS = [("open", "открыть (и отпереть)"), ("close", "закрыть"), ("lock", "закрыть и запереть"), ("unlock", "отпереть"),
            ("toggle", "открыть / закрыть")]
FIELD_OPS = [("off", "выключить"), ("on", "включить"), ("toggle", "переключить")]
TURRET_OPS = [("off", "отключить"), ("on", "включить"), ("friendly", "перепрограммировать (стреляют по Альянсу)"),
              ("hostile", "снова враждебны игроку")]

SKILLS = [("guns", "Огнестрел"), ("energy", "Оружие Альянса"), ("melee", "Ближний бой"), ("explosives", "Взрывчатка"),
          ("lockpick", "Взлом"), ("science", "Наука"), ("repair", "Ремонт"), ("medicine", "Медицина"),
          ("sneak", "Скрытность"), ("speech", "Красноречие"), ("barter", "Бартер"), ("survival", "Выживание")]
STATS = [("str", "Сила"), ("per", "Восприятие"), ("end", "Выносливость"), ("cha", "Обаяние"),
         ("int", "Интеллект"), ("agi", "Ловкость"), ("lck", "Удача")]
SKILL_NAMES = dict(SKILLS + STATS + [("level", "Уровень")])
WEATHERS = ["clear", "fair", "partlycloudy", "cumulus", "stratocumulus", "highclouds", "overcast", "fog", "misty",
            "drizzle", "rain", "thunderstorm", "snow", "ash"]

# parameter kinds: quest, qstate, op, int, hour, float, flag, var, item, skill, stat, npc (targetname, "" = this NPC
# where it makes sense), target (NPC / object / zone name), zone, weather, map, event, obj (objective of "quest"),
# text (languages), str, varop, faction, sound
# (key, kind, label, default)
COND_TYPES = [
    ("quest_state", "Задание: состояние", [("quest", "quest", "Задание", ""), ("value", "qstate", "Состояние", "active")]),
    ("quest_stage", "Задание: этап", [("quest", "quest", "Задание", ""), ("op", "op", "", ">="), ("value", "int", "Этап", 1)]),
    ("objective", "Задание: цель выполнена", [("quest", "quest", "Задание", ""), ("obj", "obj", "Цель", "")]),
    ("flag", "Флаг поставлен", [("name", "flag", "Флаг", "")]),
    ("var", "Переменная", [("name", "var", "Переменная", ""), ("op", "op", "", ">="), ("value", "int", "Значение", 1)]),
    ("item", "Предмет у игрока", [("item", "item", "Предмет", ""), ("op", "op", "", ">="), ("value", "int", "Сколько", 1)]),
    ("skill", "Навык игрока", [("name", "skill", "Навык", "speech"), ("op", "op", "", ">="), ("value", "int", "Значение", 50)]),
    ("stat", "Характеристика игрока", [("name", "stat", "Характеристика", "cha"), ("op", "op", "", ">="), ("value", "int", "Значение", 5)]),
    ("level", "Уровень игрока", [("op", "op", "", ">="), ("value", "int", "Уровень", 2)]),
    ("rep", "Репутация", [("faction", "faction", "Фракция (пусто — общая)", ""), ("op", "op", "", ">="), ("value", "int", "Значение", 0)]),
    ("perk", "У игрока перк", [("name", "perk", "Перк", "")]),
    ("created", "Персонаж создан", []),
    ("time", "Время суток", [("from", "hour", "С", 8.0), ("to", "hour", "До", 20.0)]),
    ("day", "День игры", [("op", "op", "", ">="), ("value", "int", "День", 1)]),
    ("weather", "Погода", [("value", "weather", "Погода (через ;)", "rain")]),
    ("map", "Карта", [("value", "map", "Карта", "")]),
    ("zone", "Игрок в зоне", [("name", "zone", "Зона", "")]),
    ("near", "Игрок рядом с", [("target", "target", "Что", ""), ("value", "int", "Ближе, ед.", 256)]),
    ("alive", "NPC жив", [("target", "npc", "NPC", "")]),
    ("hostile", "NPC враждебен игроку", [("target", "npc", "NPC", "")]),
    ("talked", "Игрок уже говорил с", [("target", "npc", "NPC", "")]),
    ("health", "Здоровье игрока, %", [("op", "op", "", "<="), ("value", "int", "%", 50)]),
    ("suit", "На игроке костюм", []),
    ("event", "Событие уже было", [("name", "event", "Событие", "")]),
    ("chance", "Случайно, с шансом %", [("value", "int", "%", 50)]),
]

ACTION_TYPES = [
    ("give", "Дать предмет", [("item", "item", "Предмет", ""), ("count", "int", "Сколько", 1)]),
    ("take", "Забрать предмет", [("item", "item", "Предмет", ""), ("count", "int", "Сколько", 1)]),
    ("money", "Деньги ±", [("value", "int", "Жетонов (− забрать)", 10)]),
    ("xp", "Опыт", [("value", "int", "Очков", 25)]),
    ("flag", "Флаг", [("name", "flag", "Флаг", ""), ("value", "bool", "Поставить (снять — без галки)", 1)]),
    ("var", "Переменная", [("name", "var", "Переменная", ""), ("op", "varop", "", "set"), ("value", "int", "Значение", 1)]),
    ("quest_start", "Задание: выдать", [("quest", "quest", "Задание", "")]),
    ("quest_stage", "Задание: этап", [("quest", "quest", "Задание", ""), ("value", "int", "Этап", 1)]),
    ("objective", "Задание: отметить цель", [("quest", "quest", "Задание", ""), ("obj", "obj", "Цель", "")]),
    ("quest_complete", "Задание: выполнить", [("quest", "quest", "Задание", "")]),
    ("quest_fail", "Задание: провалить", [("quest", "quest", "Задание", "")]),
    ("rep", "Репутация ±", [("faction", "faction", "Фракция (пусто — общая)", ""), ("value", "int", "На сколько", 5)]),
    ("perk", "Дать перк", [("name", "perk", "Перк", "")]),
    ("char_create", "Окно создания персонажа", []),
    ("hostile", "NPC нападает", [("target", "npc", "NPC (пусто — этот)", "")]),
    ("faction_hostile", "Фракция нападает", [("faction", "faction", "Фракция (пусто — этого NPC)", "")]),
    ("message", "Сообщение на экран", [("text", "text", "Текст", None)]),
    ("sound", "Звук", [("value", "sound", "Звук (путь .wav или имя)", "")]),
    ("music", "Сменить музыку", [("value", "music", "Треки (пусто — обычная музыка)", "")]),
    ("stinger", "Музыкальная вставка (стингер)", [("value", "stinger", "Трек", "")]),
    ("bark", "Фраза NPC", [("target", "npc", "NPC (пусто — этот)", ""), ("value", "barkevent", "Какая фраза (событие)", "custom")]),
    ("teleport", "Перенести игрока", [("target", "target", "К чему", "")]),
    ("dialog", "Начать диалог", [("target", "npc", "NPC", "")]),
    ("trade", "Открыть торговлю", [("target", "npc", "NPC (пусто — этот)", "")]),
    ("kill", "Убить NPC", [("target", "npc", "NPC (пусто — этот)", "")]),
    ("remove", "Убрать объект", [("target", "target", "Что", "")]),
    ("fade", "Затемнение экрана", [("value", "float", "Секунд", 1.0), ("hold", "float", "Темно, сек", 0.5)]),
    ("time", "Поставить время", [("value", "hour", "Час", 8.0)]),
    ("weather", "Сменить погоду", [("value", "weather", "Погода", "rain"), ("seconds", "int", "За секунд", 20)]),
    ("skill", "Навык ±", [("name", "skill", "Навык", "speech"), ("value", "int", "На сколько", 5)]),
    ("stat", "Характеристика ±", [("name", "stat", "Характеристика", "cha"), ("value", "int", "На сколько", 1)]),
    ("fire", "Ввод сущности (Hammer)", [("target", "target", "targetname", ""), ("input", "str", "Ввод", "Open"),
                                        ("value", "str", "Параметр", "")]),
    ("command", "Консольная команда", [("value", "str", "Команда", "")]),
    ("door", "Дверь", [("target", "target", "Дверь (имя, можно * и ;)", ""), ("op", "doorop", "Что сделать", "open")]),
    ("field", "Силовое поле", [("target", "target", "Поле (имя: func_brush, проп)", ""), ("op", "fieldop", "Что сделать", "off")]),
    ("turret", "Турели и камеры", [("target", "target", "Какие (пусто — все рядом)", ""), ("op", "turretop", "Что сделать", "off"),
                                   ("radius", "int", "Радиус от терминала, ед. (0 — 1500)", 0)]),
]

EVENT_TYPES = [
    # id, label, target kind ("" - none), value label ("" - none)
    ("map_enter", "Игрок пришёл на карту", "", ""),
    ("zone_enter", "Игрок вошёл в зону", "zone", ""),
    ("zone_leave", "Игрок вышел из зоны", "zone", ""),
    ("kill", "Убит NPC", "npc", ""),
    ("use", "Игрок использовал (E)", "target", ""),
    ("talk", "Игрок заговорил с", "npc", ""),
    ("hour", "Наступил час", "", "Час"),
    ("quest_start", "Задание выдано", "quest", ""),
    ("quest_stage", "Начался этап задания", "quest", "Этап"),
    ("quest_complete", "Задание выполнено", "quest", ""),
    ("quest_fail", "Задание провалено", "quest", ""),
    ("when", "Когда условия станут верны", "", ""),
]

OBJ_TYPES = [
    ("kill", "Убить", "target"),
    ("collect", "Собрать предмет", "item"),
    ("talk", "Поговорить с", "npc"),
    ("go", "Дойти до", "target"),
    ("use", "Использовать (E)", "target"),
    ("cond", "Когда выполнены условия", ""),
    ("custom", "Вручную (действием «отметить цель»)", ""),
]

COND_BY_ID = {t[0]: t for t in COND_TYPES}
ACTION_BY_ID = {t[0]: t for t in ACTION_TYPES}
EVENT_BY_ID = {t[0]: t for t in EVENT_TYPES}
OBJ_BY_ID = {t[0]: t for t in OBJ_TYPES}
# actions that wait until the dialog window closes in the game
AFTER_DIALOG = {"hostile", "faction_hostile", "teleport", "dialog", "trade", "kill"}


def new_cond(t="flag"):
    d = {"type": t}
    for key, kind, _label, default in COND_BY_ID[t][2]:
        d[key] = {} if kind == "text" else default
    return d


def new_action(t="give"):
    d = {"type": t}
    for key, kind, _label, default in ACTION_BY_ID[t][2]:
        d[key] = {} if kind == "text" else default
    return d


def params(item, actions=False):
    t = (ACTION_BY_ID if actions else COND_BY_ID).get(item.get("type"))
    return t[2] if t else []


# --- readable text -----------------------------------------------------------------------
def _qname(project, qid, lang):
    q = project.quest(qid) if project and qid else None
    if q is None:
        return "«%s»" % (qid or "?")
    t = q.title.get(lang) or q.title.get(project.base_language) or q.id
    return "«%s»" % t


def _obj_name(project, qid, oid, lang):
    q = project.quest(qid) if project and qid else None
    for s in (q.stages if q else []):
        for o in s.objectives:
            if o.id == oid:
                return "«%s»" % (o.text.get(lang) or o.text.get(project.base_language) or oid)
    return "«%s»" % (oid or "?")


def _item_name(project, iid, lang):
    it = project.item(iid) if project and iid else None
    if it is not None:
        try:
            return it.label(lang, project.base_language)
        except Exception:
            pass
    return iid or "?"


def _num(v):
    try:
        f = float(v)
        return "%d" % f if f == int(f) else "%g" % f
    except (TypeError, ValueError):
        return str(v)


def _hour(v):
    try:
        f = float(v) % 24.0
    except (TypeError, ValueError):
        return str(v)
    return "%d:%02d" % (int(f), int(round((f - int(f)) * 60)) % 60)


def describe_cond(c, project=None, lang="russian"):
    t = c.get("type", "")
    op = OP_NAMES.get(c.get("op", ">="), c.get("op", ""))
    v = _num(c.get("value", ""))
    if t == "quest_state":
        st = dict(QSTATES).get(c.get("value", "active"), c.get("value"))
        s = "задание %s %s" % (_qname(project, c.get("quest"), lang), st)
    elif t == "quest_stage":
        s = "задание %s активно, этап %s %s" % (_qname(project, c.get("quest"), lang), op, v)
    elif t == "objective":
        s = "цель %s выполнена" % _obj_name(project, c.get("quest"), c.get("obj"), lang)
    elif t == "flag":
        s = "флаг %s" % (c.get("name") or "?")
    elif t == "var":
        s = "%s %s %s" % (c.get("name") or "?", op, v)
    elif t == "item":
        s = "у игрока %s %s %s шт." % (_item_name(project, c.get("item"), lang), op, v)
    elif t in ("skill", "stat"):
        s = "%s %s %s" % (SKILL_NAMES.get(c.get("name"), c.get("name")), op, v)
    elif t == "level":
        s = "уровень %s %s" % (op, v)
    elif t == "rep":
        s = ("репутация %s %s %s" % (c.get("faction"), op, v)) if c.get("faction") else ("репутация %s %s" % (op, v))
    elif t == "perk":
        p = project.perk(c.get("name")) if project and hasattr(project, "perk") else None
        s = "перк «%s»" % (p.label(lang, project.base_language) if p else (c.get("name") or "?"))
    elif t == "created":
        s = "персонаж создан"
    elif t == "time":
        s = "время с %s до %s" % (_hour(c.get("from", 0)), _hour(c.get("to", 24)))
    elif t == "day":
        s = "день %s %s" % (op, v)
    elif t == "weather":
        s = "погода: %s" % (c.get("value") or "?")
    elif t == "map":
        s = "карта %s" % (c.get("value") or "?")
    elif t == "zone":
        s = "игрок в зоне %s" % (c.get("name") or "?")
    elif t == "near":
        s = "игрок ближе %s ед. к %s" % (v, c.get("target") or "?")
    elif t == "alive":
        s = "%s жив" % (c.get("target") or "этот NPC")
    elif t == "hostile":
        s = "%s враждебен" % (c.get("target") or "этот NPC")
    elif t == "talked":
        s = "уже говорил с %s" % (c.get("target") or "?")
    elif t == "health":
        s = "здоровье %s %s%%" % (op, v)
    elif t == "suit":
        s = "на игроке костюм"
    elif t == "event":
        s = "событие %s уже было" % (c.get("name") or "?")
    elif t == "chance":
        s = "шанс %s%%" % v
    else:
        s = t
    if c.get("not"):
        s = "НЕ " + s
    return s


def describe_action(a, project=None, lang="russian"):
    t = a.get("type", "")
    v = _num(a.get("value", ""))
    if t == "give":
        s = "дать %s × %s" % (_item_name(project, a.get("item"), lang), _num(a.get("count", 1)))
    elif t == "take":
        s = "забрать %s × %s" % (_item_name(project, a.get("item"), lang), _num(a.get("count", 1)))
    elif t == "money":
        s = "жетоны %+d" % int(float(a.get("value", 0) or 0))
    elif t == "xp":
        s = "опыт +%s" % v
    elif t == "flag":
        s = ("поставить флаг %s" if int(a.get("value", 1) or 0) else "снять флаг %s") % (a.get("name") or "?")
    elif t == "var":
        s = "%s %s %s" % (a.get("name") or "?", dict(VAR_OPS).get(a.get("op", "set"), "="), v)
    elif t == "quest_start":
        s = "выдать задание %s" % _qname(project, a.get("quest"), lang)
    elif t == "quest_stage":
        s = "задание %s → этап %s" % (_qname(project, a.get("quest"), lang), v)
    elif t == "objective":
        s = "отметить цель %s" % _obj_name(project, a.get("quest"), a.get("obj"), lang)
    elif t == "quest_complete":
        s = "выполнить задание %s" % _qname(project, a.get("quest"), lang)
    elif t == "quest_fail":
        s = "провалить задание %s" % _qname(project, a.get("quest"), lang)
    elif t == "rep":
        s = ("репутация %s %+d" % (a.get("faction"), int(float(a.get("value", 0) or 0)))) if a.get("faction") \
            else "репутация %+d" % int(float(a.get("value", 0) or 0))
    elif t == "perk":
        p = project.perk(a.get("name")) if project and hasattr(project, "perk") else None
        s = "дать перк «%s»" % (p.label(lang, project.base_language) if p else (a.get("name") or "?"))
    elif t == "char_create":
        s = "открыть создание персонажа"
    elif t == "hostile":
        s = "%s нападает" % (a.get("target") or "NPC")
    elif t == "faction_hostile":
        s = "фракция %s нападает" % (a.get("faction") or "этого NPC")
    elif t == "message":
        txt = a.get("text") or {}
        s = "сообщение «%s»" % ((txt.get(lang) if isinstance(txt, dict) else txt) or (next(iter(txt.values()), "") if isinstance(txt, dict) else ""))
    elif t == "sound":
        s = "звук %s" % (a.get("value") or "?")
    elif t == "music":
        s = ("музыка: %s" % a.get("value")) if a.get("value") else "обычная музыка"
    elif t == "stinger":
        s = "стингер %s" % (a.get("value") or "?")
    elif t == "bark":
        from .sounddata import BARK_EVENT_NAMES
        ev = a.get("value") or "custom"
        s = "%s: фраза «%s»" % (a.get("target") or "NPC", BARK_EVENT_NAMES.get(ev, ev).lower())
    elif t == "teleport":
        s = "перенести игрока к %s" % (a.get("target") or "?")
    elif t == "dialog":
        s = "диалог с %s" % (a.get("target") or "?")
    elif t == "trade":
        s = "торговля с %s" % (a.get("target") or "этим NPC")
    elif t == "kill":
        s = "убить %s" % (a.get("target") or "этого NPC")
    elif t == "remove":
        s = "убрать %s" % (a.get("target") or "?")
    elif t == "fade":
        s = "затемнение %s с" % v
    elif t == "time":
        s = "время %s" % _hour(a.get("value", 0))
    elif t == "weather":
        s = "погода → %s" % (a.get("value") or "?")
    elif t in ("skill", "stat"):
        s = "%s %+d" % (SKILL_NAMES.get(a.get("name"), a.get("name")), int(float(a.get("value", 0) or 0)))
    elif t == "fire":
        s = "%s → %s %s" % (a.get("target") or "?", a.get("input") or "?", a.get("value") or "")
    elif t == "command":
        s = "команда «%s»" % (a.get("value") or "")
    elif t == "door":
        s = "дверь %s: %s" % (a.get("target") or "?", dict(DOOR_OPS).get(a.get("op", "open"), a.get("op")))
    elif t == "field":
        s = "поле %s: %s" % (a.get("target") or "?", dict(FIELD_OPS).get(a.get("op", "off"), a.get("op")))
    elif t == "turret":
        who = a.get("target") or "турели и камеры рядом"
        s = "%s: %s" % (who, dict(TURRET_OPS).get(a.get("op", "off"), a.get("op")))
    else:
        s = t
    d = float(a.get("delay", 0) or 0)
    if d > 0:
        s += " (через %s с)" % _num(d)
    return s


def describe_conds(conds, project=None, lang="russian"):
    if not conds:
        return "всегда"
    out = ""
    for i, c in enumerate(conds):
        if i:
            out += " или " if conds[i - 1].get("or") else " и "
        out += describe_cond(c, project, lang)
    return out


# --- game files ---------------------------------------------------------------------------
def _val(v):
    if isinstance(v, bool):
        return "1" if v else "0"
    if isinstance(v, float):
        return "%g" % v
    return str(v)


def _add_texts(blk, key, texts, project):
    if not isinstance(texts, dict):
        blk.add(key, str(texts))
        return
    base = project.base_language if project else "english"
    fb = texts.get(base) or next((t for t in texts.values() if t), "")
    if fb:
        blk.add(key, fb)
    for lang in (project.languages if project else []):
        if lang != base and texts.get(lang):
            blk.add(key + "_" + lang, texts[lang])


def write_conds(parent, key, conds, project=None):
    """conds -> block <key> { "c" {...} ... } (nothing if empty)."""
    if not conds:
        return None
    b = parent.add(key)
    for c in conds:
        x = b.add("c")
        x.inline = True
        x.add("type", c.get("type", ""))
        for pk, kind, _l, _d in params(c):
            v = c.get(pk)
            if v is None or v == "":
                continue
            if kind == "text":
                _add_texts(x, pk, v, project)
            else:
                x.add(pk, _val(v))
        if c.get("not"):
            x.add("not", "1")
        if c.get("or"):
            x.add("or", "1")
    return b


def write_actions(parent, key, actions, project=None):
    if not actions:
        return None
    b = parent.add(key)
    for a in actions:
        x = b.add("a")
        x.inline = True
        x.add("type", a.get("type", ""))
        for pk, kind, _l, _d in params(a, True):
            v = a.get(pk)
            if v is None or v == "" or v == {}:
                continue
            if kind == "text":
                _add_texts(x, pk, v, project)
            else:
                x.add(pk, _val(v))
        if float(a.get("delay", 0) or 0) > 0:
            x.add("delay", _val(float(a["delay"])))
    return b


def _read_texts(blk, key, langs):
    out = {}
    base_val = blk.get(key)
    for c in blk.values():
        n = c.name.lower()
        if n == key:
            continue
        if n.startswith(key + "_"):
            out[n[len(key) + 1:]] = c.value
    if base_val is not None:
        out.setdefault(langs[0] if langs else "english", base_val)
    return out


def _coerce(kind, v):
    if v is None:
        return v
    if kind == "int":
        try:
            return int(float(v))
        except ValueError:
            return 0
    if kind in ("hour", "float"):
        try:
            return float(v)
        except ValueError:
            return 0.0
    if kind == "bool":
        return 0 if str(v) in ("0", "") else 1
    return v


def read_conds(blk, langs=("english",)):
    out = []
    if blk is None:
        return out
    for x in blk.blocks():
        c = {"type": x.get("type", "")}
        for pk, kind, _l, default in params(c):
            if kind == "text":
                c[pk] = _read_texts(x, pk, list(langs))
            elif x.get(pk) is not None:
                c[pk] = _coerce(kind, x.get(pk))
            else:
                c[pk] = default
        if x.get("not") not in (None, "0", ""):
            c["not"] = True
        if x.get("or") not in (None, "0", ""):
            c["or"] = True
        out.append(c)
    return out


def read_actions(blk, langs=("english",)):
    out = []
    if blk is None:
        return out
    for x in blk.blocks():
        a = {"type": x.get("type", "")}
        for pk, kind, _l, default in params(a, True):
            if kind == "text":
                a[pk] = _read_texts(x, pk, list(langs))
            elif x.get(pk) is not None:
                a[pk] = _coerce(kind, x.get(pk))
            else:
                a[pk] = default
        if x.get("delay"):
            a["delay"] = float(x.get("delay"))
        out.append(a)
    return out


# --- the simulator: checking conditions against hand-set values -----------------------------
class SimState:
    """What the dialog simulator (F5) assumes about the game."""

    def __init__(self):
        self.quests = {}         # id -> [state, stage]
        self.flags = set()
        self.vars = {}
        self.items = {}
        self.skills = {}
        self.stats = {}
        self.level = 1
        self.rep = 0
        self.hour = 12.0
        self.day = 0
        self.weather = "clear"
        self.map = ""
        self.objectives = set()  # "quest.obj"
        self.dead = set()
        self.hostile = set()
        self.talked = set()
        self.events = set()
        self.zone = ""
        self.health = 100
        self.suit = False
        self.checks = set()      # "node.option" tried once
        self.xp = 0
        self.log = []

    def stat(self, name):
        return self.stats.get(name, 5)


def _cmp(op, a, b):
    try:
        a, b = float(a), float(b)
    except (TypeError, ValueError):
        return False
    return {">=": a >= b, "==": a == b, "<=": a <= b, ">": a > b, "<": a < b, "!=": a != b}.get(op or ">=", a >= b)


def eval_cond(c, s, self_npc=""):
    t = c.get("type")
    op = c.get("op", ">=")
    v = c.get("value", 0)
    if t == "quest_state":
        r = s.quests.get(c.get("quest"), ["inactive", 0])[0] == c.get("value", "active")
    elif t == "quest_stage":
        st = s.quests.get(c.get("quest"), ["inactive", 0])
        r = st[0] == "active" and _cmp(op, st[1], v)
    elif t == "objective":
        r = "%s.%s" % (c.get("quest"), c.get("obj")) in s.objectives
    elif t == "flag":
        r = c.get("name") in s.flags
    elif t == "var":
        r = _cmp(op, s.vars.get(c.get("name"), 0), v)
    elif t == "item":
        r = _cmp(op, s.items.get(c.get("item"), 0), v)
    elif t == "skill":
        r = _cmp(op, s.skills.get(c.get("name"), 0), v)
    elif t == "stat":
        r = _cmp(op, s.stat(c.get("name")), v)
    elif t == "level":
        r = _cmp(op, s.level, v)
    elif t == "rep":
        r = _cmp(op, s.rep, v)
    elif t == "perk":
        r = c.get("name") in getattr(s, "perks", set())
    elif t == "created":
        r = True
    elif t == "time":
        f, to = float(c.get("from", 0)), float(c.get("to", 24))
        r = (f <= s.hour < to) if f <= to else (s.hour >= f or s.hour < to)
    elif t == "day":
        r = _cmp(op, s.day, v)
    elif t == "weather":
        r = s.weather in [w.strip() for w in str(c.get("value", "")).split(";")]
    elif t == "map":
        r = not s.map or s.map == c.get("value")
    elif t == "zone":
        r = s.zone == c.get("name")
    elif t == "near":
        r = True
    elif t == "alive":
        r = (c.get("target") or self_npc) not in s.dead
    elif t == "hostile":
        r = (c.get("target") or self_npc) in s.hostile
    elif t == "talked":
        r = c.get("target") in s.talked
    elif t == "health":
        r = _cmp(op, s.health, v)
    elif t == "suit":
        r = s.suit
    elif t == "event":
        r = c.get("name") in s.events
    elif t == "chance":
        r = True
    else:
        r = True
    return (not r) if c.get("not") else r


def eval_conds(conds, s, self_npc=""):
    group = False
    pending = False
    for c in conds or []:
        group = group or eval_cond(c, s, self_npc)
        pending = True
        if c.get("or"):
            continue
        if not group:
            return False
        group = pending = False
    return (not pending) or group


def check_passes(check, s):
    """A skill / attribute check of a dialog option: (passes, label)."""
    if not check:
        return True, ""
    sid = check.get("skill") or check.get("stat") or ""
    need = int(check.get("value", 0) or 0)
    have = s.stat(sid) if check.get("stat") else (s.level if sid == "level" else s.skills.get(sid, 0))
    name = SKILL_NAMES.get(sid, sid)
    if have >= need:
        return True, "[%s %d] " % (name, need)
    return False, "[%s %d/%d] " % (name, have, need)


def run_actions(actions, s, project=None, self_npc=""):
    """Apply what can be simulated; returns readable lines of everything."""
    lines = []
    for a in actions or []:
        t = a.get("type")
        lines.append(describe_action(a, project))
        if t == "give":
            s.items[a.get("item")] = s.items.get(a.get("item"), 0) + int(a.get("count", 1) or 1)
        elif t == "take":
            s.items[a.get("item")] = max(0, s.items.get(a.get("item"), 0) - int(a.get("count", 1) or 1))
        elif t == "money":
            s.items["rpm_credits"] = max(0, s.items.get("rpm_credits", 0) + int(a.get("value", 0) or 0))
        elif t == "xp":
            s.xp += int(a.get("value", 0) or 0)
        elif t == "flag":
            (s.flags.add if int(a.get("value", 1) or 0) else s.flags.discard)(a.get("name"))
        elif t == "var":
            cur = s.vars.get(a.get("name"), 0)
            val = int(a.get("value", 0) or 0)
            s.vars[a.get("name")] = {"set": val, "add": cur + val, "sub": cur - val}.get(a.get("op", "set"), val)
        elif t == "quest_start":
            if s.quests.get(a.get("quest"), ["inactive"])[0] not in ("active", "completed"):
                s.quests[a.get("quest")] = ["active", 0]
        elif t == "quest_stage":
            st = s.quests.get(a.get("quest"))
            if st and st[0] == "active":
                st[1] = int(a.get("value", 0) or 0)
        elif t == "objective":
            s.objectives.add("%s.%s" % (a.get("quest"), a.get("obj")))
        elif t == "quest_complete":
            st = s.quests.get(a.get("quest"))
            if st and st[0] == "active":
                st[0] = "completed"
        elif t == "quest_fail":
            st = s.quests.setdefault(a.get("quest"), ["inactive", 0])
            st[0] = "failed"
        elif t == "rep":
            s.rep += int(a.get("value", 0) or 0)
        elif t == "hostile":
            s.hostile.add(a.get("target") or self_npc)
        elif t == "kill":
            s.dead.add(a.get("target") or self_npc)
        elif t in ("skill", "stat"):
            d = s.skills if t == "skill" else s.stats
            d[a.get("name")] = (d.get(a.get("name"), 0 if t == "skill" else 5)) + int(a.get("value", 0) or 0)
        elif t == "time":
            s.hour = float(a.get("value", 0) or 0)
    return lines


# --- what a list refers to (validation, "where is it used") ---------------------------------
def refs(items, actions=False):
    """{kind: set(names)} - quest, flag, var, item, npc, zone, event, faction."""
    out = {}
    for x in items or []:
        for pk, kind, _l, _d in params(x, actions):
            v = x.get(pk)
            if kind in ("quest", "flag", "var", "item", "npc", "zone", "event", "faction", "target") and v:
                out.setdefault(kind, set()).add(str(v))
        if x.get("type") in ("objective",) and x.get("quest") and x.get("obj"):
            out.setdefault("obj", set()).add("%s.%s" % (x["quest"], x["obj"]))
    return out


def iter_logic(project):
    """(where, kind, list) over every condition / action list of the project - kind "cond" / "action"."""
    for npc in project.npcs:
        for n in npc.nodes:
            yield ("%s #%d" % (npc.targetname, n.id), "action", getattr(n, "actions", []))
            for i, o in enumerate(n.options):
                w = "%s #%d вариант %d" % (npc.targetname, n.id, i + 1)
                yield (w, "cond", getattr(o, "conditions", []))
                yield (w, "action", getattr(o, "actions", []))
                yield (w, "action", getattr(o, "fail_actions", []))
        for pi, pk in enumerate(npc.actor.packages):
            yield ("%s пакет %d" % (npc.targetname, pi + 1), "cond", getattr(pk, "conditions", []))
        for b in getattr(npc.actor, "barks", []):
            yield ("%s фраза %s" % (npc.targetname, b.id), "cond", b.conditions)
    for bs in getattr(project, "bark_sets", []):
        for b in bs.barks:
            yield ("набор фраз %s: %s" % (bs.id, b.id), "cond", b.conditions)
    for q in project.quests:
        yield ("задание %s" % q.id, "cond", q.fail_conditions)
        yield ("задание %s" % q.id, "action", q.on_complete)
        yield ("задание %s" % q.id, "action", q.on_fail)
        for si, s in enumerate(q.stages):
            yield ("задание %s этап %d" % (q.id, si), "action", s.actions)
            for o in s.objectives:
                yield ("задание %s цель %s" % (q.id, o.id), "cond", o.conditions)
    for name, layer in project.maps.items():
        for r in layer.refs:
            yield ("%s: %s" % (name, r.label() if hasattr(r, "label") else r.id), "cond", getattr(r, "conditions", []))
    for e in project.events:
        yield ("событие %s" % e.id, "cond", e.conditions)
        yield ("событие %s" % e.id, "action", e.actions)
    ng = getattr(project, "newgame", None)
    if ng:
        yield ("новая игра", "action", ng.setdefault("actions", []))
    for t in getattr(project, "terminals", []):
        for e in t.all_entries():
            w = "терминал %s, запись %s" % (t.id, e.key)
            yield (w, "cond", e.conditions)
            yield (w, "action", e.actions)


def known_names(project):
    """Names already used in the project (completion of flags / variables / events)."""
    flags, vars_, events = set(), set(), set()
    for g in project.globals:
        (flags if g.kind == "flag" else vars_).add(g.name)
    for _w, kind, lst in iter_logic(project):
        for x in lst or []:
            if x.get("type") == "flag" and x.get("name"):
                flags.add(x["name"])
            elif x.get("type") == "var" and x.get("name"):
                vars_.add(x["name"])
            elif x.get("type") == "event" and x.get("name"):
                events.add(x["name"])
    events |= {e.id for e in project.events}
    return sorted(flags), sorted(vars_), sorted(events)


def write_kv_dumps(root, header=""):
    return kv.dumps(root, header=header)
