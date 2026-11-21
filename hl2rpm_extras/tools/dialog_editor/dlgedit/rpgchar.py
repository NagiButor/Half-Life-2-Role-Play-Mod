"""The character of the RPG ("RPG" tab): rules, attributes and skills, perks, traits
(scripts/rpg/rpg.txt) and the new game (scripts/rpg/newgame.txt). The game side is
game/shared/hl2rpm_rpgdefs.cpp and game/server/hl2rpm_char.cpp; the defaults below
are the built-in ones of the game (like Fallout: New Vegas, with an HL2 flavour)."""
import copy

from . import kv
from . import logic


def _t(en, ru):
    return {"english": en, "russian": ru}


STATS = [
    ("str", _t("Strength", "Сила"), _t("Raw physical power: melee damage and how much you can carry.",
                                         "Физическая мощь: урон в ближнем бою и сколько можно унести.")),
    ("per", _t("Perception", "Восприятие"), _t("Senses and awareness: energy weapons, explosives, lockpicking.",
                                                "Чутьё и внимательность: оружие Альянса, взрывчатка, взлом.")),
    ("end", _t("Endurance", "Выносливость"), _t("Stamina and toughness: health and survival.",
                                                 "Стойкость и здоровье: запас здоровья и выживание.")),
    ("cha", _t("Charisma", "Обаяние"), _t("Charm and presence: speech and barter.",
                                           "Умение располагать к себе: красноречие и бартер.")),
    ("int", _t("Intelligence", "Интеллект"), _t("Knowledge and wit: science, repair, medicine, skill points per level.",
                                                  "Знания и сообразительность: наука, ремонт, медицина, очки навыков за уровень.")),
    ("agi", _t("Agility", "Ловкость"), _t("Coordination and speed: guns and sneak.", "Координация и скорость: огнестрел и скрытность.")),
    ("lck", _t("Luck", "Удача"), _t("Fortune: critical hits and a little of every skill.", "Везение: критические попадания и понемногу каждый навык.")),
]
STAT_IDS = [s[0] for s in STATS]

SKILLS = [
    ("guns", "agi", _t("Guns", "Огнестрел"), _t("Pistols, SMGs, shotguns, rifles.", "Пистолеты, пистолеты-пулемёты, дробовики, винтовки.")),
    ("energy", "per", _t("Combine Weapons", "Оружие Альянса"), _t("Pulse rifles and other Combine technology.", "Импульсные винтовки и прочее оружие Альянса.")),
    ("melee", "str", _t("Melee", "Ближний бой"), _t("Crowbars, batons, knives and fists.", "Монтировки, дубинки, ножи и кулаки.")),
    ("explosives", "per", _t("Explosives", "Взрывчатка"), _t("Grenades, rockets, mines.", "Гранаты, ракеты, мины.")),
    ("lockpick", "per", _t("Lockpick", "Взлом"), _t("Opening locked doors and containers.", "Вскрытие запертых дверей и контейнеров.")),
    ("science", "int", _t("Science", "Наука"), _t("Hacking terminals and Combine machines.", "Взлом терминалов и машин Альянса.")),
    ("repair", "int", _t("Repair", "Ремонт"), _t("Fixing gear and machines.", "Починка снаряжения и механизмов.")),
    ("medicine", "int", _t("Medicine", "Медицина"), _t("How much medkits and food heal.", "Сколько лечат аптечки и еда.")),
    ("sneak", "agi", _t("Sneak", "Скрытность"), _t("Staying unseen, stealing.", "Оставаться незамеченным, воровать.")),
    ("speech", "cha", _t("Speech", "Красноречие"), _t("Persuading people in dialogs.", "Убеждать людей в разговоре.")),
    ("barter", "cha", _t("Barter", "Бартер"), _t("Better prices when trading.", "Выгодные цены в торговле.")),
    ("survival", "end", _t("Survival", "Выживание"), _t("Living off the land of the occupied City.", "Выживать в оккупированном Городе.")),
]
SKILL_IDS = [s[0] for s in SKILLS]

# (key, default, label, group, decimals)
RULES = [
    ("stat_total", 40, "Очков характеристик при создании", "Создание персонажа", 0),
    ("stat_start", 5, "Начальное значение характеристики", "Создание персонажа", 0),
    ("stat_min", 1, "Минимум характеристики", "Создание персонажа", 0),
    ("stat_max", 10, "Максимум характеристики", "Создание персонажа", 0),
    ("tags", 3, "Отмеченных навыков", "Создание персонажа", 0),
    ("tag_bonus", 15, "Бонус отмеченного навыка", "Создание персонажа", 0),
    ("traits_max", 2, "Черт не больше", "Создание персонажа", 0),
    ("skill_base", 2, "Навык: основа", "Навыки", 0),
    ("skill_per_stat", 2, "Навык: × своя характеристика", "Навыки", 1),
    ("skill_luck_div", 2, "Навык: + Удача / N (вверх)", "Навыки", 0),
    ("skill_max", 100, "Навык: предел", "Навыки", 0),
    ("level_max", 30, "Максимальный уровень", "Уровни", 0),
    ("xp_scale", 1, "Множитель опыта до уровня (25·(3L+2)(L−1) × N)", "Уровни", 2),
    ("points_base", 10, "Очков навыков за уровень", "Уровни", 0),
    ("points_per_int", 0.5, "+ Интеллект × N", "Уровни", 2),
    ("perk_every", 2, "Перк каждые N уровней", "Уровни", 0),
    ("hp_base", 50, "Здоровье: основа", "Здоровье и вес", 0),
    ("hp_per_end", 10, "Здоровье: × Выносливость", "Здоровье и вес", 0),
    ("hp_per_level", 3, "Здоровье: за уровень", "Здоровье и вес", 0),
    ("carry_base", 25, "Вес, кг: основа", "Здоровье и вес", 0),
    ("carry_per_str", 3, "Вес, кг: × Сила", "Здоровье и вес", 1),
    ("dmg_min", 0.75, "Урон при навыке 0 (×)", "Бой", 2),
    ("dmg_max", 1.25, "Урон при навыке 100 (×)", "Бой", 2),
    ("skill_req_penalty", 0.25, "Штраф урона ниже требования оружия", "Бой", 2),
    ("spread_min", 1.5, "Разброс при навыке 0 (×)", "Бой", 2),
    ("spread_max", 0.6, "Разброс при навыке 100 (×)", "Бой", 2),
    ("crit_per_luck", 1, "Шанс крита, % за Удачу", "Бой", 1),
    ("crit_mult", 1.5, "Урон крита (×)", "Бой", 2),
    ("heal_per_medicine", 0.005, "Лечение: + за очко Медицины", "Прочие навыки", 3),
    ("barter_per_point", 0.002, "Цены: доля за очко Бартера", "Прочие навыки", 3),
    ("xp_kill", 10, "Опыт за врага без своего опыта", "Опыт и репутация", 0),
    ("xp_lockpick", 10, "Опыт за взлом (+ сложность / 10)", "Опыт и репутация", 0),
    ("xp_check", 10, "Опыт за пройденную проверку в диалоге", "Опыт и репутация", 0),
    ("rep_kill", 25, "Репутация: − за убийство члена фракции", "Опыт и репутация", 0),
    ("rep_hostile", -50, "Репутация: фракция враждебна от", "Опыт и репутация", 0),
    ("rep_theft", 5, "Репутация: − за замеченную кражу", "Опыт и репутация", 0),
    ("npc_yield", 0, "NPC уступают дорогу игроку (0 — нет, 1 — да, как в HL2)", "Поведение NPC", 0),
    ("npc_pickup_weapons", 0, "NPC подбирают оружие с пола (0 — нет, 1 — да, 2 — только в отряде игрока)", "Поведение NPC", 0),
    ("npc_talk_gestures", 1, "NPC жестикулируют, когда говорят реплики и фразы (0 — нет, 1 — да)", "Поведение NPC", 0),
    ("weapon_law", 1, "Запрет оружия: ГО и солдаты (0 — не следят, 1 — предупреждают, 2 — стреляют сразу)", "Поведение NPC", 0),
    ("weapon_law_grace", 5, "Запрет оружия: секунд, чтобы убрать оружие", "Поведение NPC", 1),
    ("weapon_law_dist", 1000, "Запрет оружия: дальность, ед.", "Поведение NPC", 0),
    ("weapon_law_melee", 0, "Запрет оружия: и ближнего боя (0 — нет, 1 — да)", "Поведение NPC", 0),
    ("weapon_law_disguise", 1, "Запрет оружия: в форме ГО / солдата игроку можно (0 — нет, 1 — да)", "Поведение NPC", 0),
]
RULE_DEFAULTS = {r[0]: r[1] for r in RULES}

EFFECT_TYPES = [
    ("stat", "Характеристика +", "stat"), ("skill", "Навык +", "skill"), ("skills", "Все навыки +", ""),
    ("hp", "Здоровье +", ""), ("carry", "Вес, кг +", ""), ("damage", "Урон оружия навыка, % +", "wskill"),
    ("crit", "Шанс крита, % +", ""), ("heal", "Лечение, % +", ""), ("xp", "Опыт, % +", ""),
    ("price", "Цены выгоднее, % +", ""), ("points", "Очков навыков за уровень +", ""),
    ("dr", "Сопротивление урону, % +", ""),
]
EFFECT_NAMES = {e[0]: e[1] for e in EFFECT_TYPES}
EFFECT_ID_KIND = {e[0]: e[2] for e in EFFECT_TYPES}
WEAPON_SKILLS = ["guns", "energy", "melee", "explosives"]


def describe_effect(e):
    t = e.get("type", "")
    v = e.get("value", 0)
    try:
        v = float(v)
    except (TypeError, ValueError):
        v = 0
    vs = ("%+g" % v)
    name = EFFECT_NAMES.get(t, t)
    eid = e.get("id", "")
    if t in ("stat", "skill"):
        return "%s %s" % (logic.SKILL_NAMES.get(eid, eid or "?"), vs)
    if t == "damage":
        return "урон %s %s%%" % (logic.SKILL_NAMES.get(eid, "любого оружия") if eid else "любого оружия", vs)
    return "%s %s" % (name.split(",")[0].replace(" +", ""), vs + ("%" if "%" in name else ""))


class Perk:
    def __init__(self, pid="perk"):
        self.id = pid
        self.name = {}
        self.desc = {}
        self.level = 2
        self.ranks = 1
        self.req = {}            # stat / skill id -> minimum
        self.effects = []        # {"type", "id", "value"}
        self.note = ""

    def to_dict(self):
        d = {"id": self.id, "level": self.level}
        for k in ("name", "desc"):
            t = {a: b for a, b in getattr(self, k).items() if b}
            if t:
                d[k] = t
        if self.ranks != 1:
            d["ranks"] = self.ranks
        if self.req:
            d["req"] = dict(self.req)
        if self.effects:
            d["effects"] = copy.deepcopy(self.effects)
        if self.note:
            d["note"] = self.note
        return d

    @classmethod
    def from_dict(cls, d):
        p = cls(d.get("id", "perk"))
        p.name = dict(d.get("name", {}))
        p.desc = dict(d.get("desc", {}))
        p.level = int(d.get("level", 2 if cls is Perk else 1))
        p.ranks = int(d.get("ranks", 1))
        p.req = {k: int(v) for k, v in (d.get("req") or {}).items()}
        p.effects = [dict(x) for x in d.get("effects", [])]
        p.note = d.get("note", "")
        return p

    def label(self, lang, base):
        return self.name.get(lang) or self.name.get(base) or self.id


class Trait(Perk):
    """A trait: chosen at character creation, an advantage with a drawback (effects only)."""

    def __init__(self, tid="trait"):
        super().__init__(tid)
        self.level = 1

    def to_dict(self):
        d = super().to_dict()
        d.pop("level", None)
        d.pop("ranks", None)
        d.pop("req", None)
        return d


def _e(t, v, i=""):
    d = {"type": t, "value": v}
    if i:
        d["id"] = i
    return d


STOCK_PERKS = [
    ("swift_learner", 2, 3, {"int": 4}, [_e("xp", 10)], _t("Swift Learner", "Быстрообучаемый"),
     _t("+10% experience for each rank.", "+10% опыта за каждый ранг.")),
    ("toughness", 2, 2, {"end": 5}, [_e("dr", 5)], _t("Toughness", "Закалка"),
     _t("+5% damage resistance for each rank.", "+5% сопротивления урону за каждый ранг.")),
    ("gunslinger", 2, 1, {"agi": 6}, [_e("damage", 10, "guns")], _t("Gunslinger", "Стрелок"),
     _t("+10% damage with guns.", "+10% урона из огнестрельного оружия.")),
    ("crowbar_master", 2, 1, {"str": 5}, [_e("damage", 20, "melee")], _t("Crowbar Master", "Мастер монтировки"),
     _t("+20% melee damage.", "+20% урона в ближнем бою.")),
    ("silver_tongue", 2, 1, {"cha": 5}, [_e("skill", 10, "speech")], _t("Silver Tongue", "Подвешенный язык"),
     _t("+10 Speech.", "+10 к Красноречию.")),
    ("tinkerer", 2, 1, {"int": 5}, [_e("skill", 10, "repair")], _t("Tinkerer", "Умелец"),
     _t("+10 Repair.", "+10 к Ремонту.")),
    ("scavenger", 2, 1, {"per": 5}, [_e("skill", 10, "survival"), _e("carry", 5)], _t("Scavenger", "Мародёр"),
     _t("+10 Survival, +5 kg carry weight.", "+10 к Выживанию, +5 кг к весу.")),
    ("strong_back", 4, 1, {"str": 5, "end": 5}, [_e("carry", 10)], _t("Strong Back", "Крепкая спина"),
     _t("+10 kg carry weight.", "+10 кг к переносимому весу.")),
    ("educated", 4, 1, {"int": 4}, [_e("points", 2)], _t("Educated", "Образованный"),
     _t("+2 skill points every level.", "+2 очка навыков на каждом уровне.")),
    ("field_medic", 4, 1, {"medicine": 40}, [_e("heal", 25)], _t("Field Medic", "Полевой медик"),
     _t("Medkits and food heal 25% more.", "Аптечки и еда лечат на 25% больше.")),
    ("black_marketeer", 4, 1, {"barter": 40}, [_e("price", 10)], _t("Black Marketeer", "Чёрный рынок"),
     _t("Buy 10% cheaper, sell 10% dearer.", "Покупать на 10% дешевле, продавать на 10% дороже.")),
    ("locksmith", 4, 1, {"lockpick": 30}, [_e("skill", 15, "lockpick")], _t("Locksmith", "Взломщик"),
     _t("+15 Lockpick.", "+15 к Взлому.")),
    ("combine_hacker", 4, 1, {"science": 40}, [_e("skill", 15, "science")], _t("Combine Hacker", "Хакер Альянса"),
     _t("+15 Science.", "+15 к Науке.")),
    ("overwatch_tech", 4, 1, {"per": 6}, [_e("damage", 15, "energy")], _t("Overwatch Tech", "Техника Надзора"),
     _t("+15% damage with Combine weapons.", "+15% урона из оружия Альянса.")),
    ("shadow", 6, 1, {"agi": 6}, [_e("skill", 15, "sneak")], _t("Shadow", "Тень"),
     _t("+15 Sneak.", "+15 к Скрытности.")),
    ("demolition_expert", 6, 2, {"explosives": 50}, [_e("damage", 20, "explosives")], _t("Demolition Expert", "Подрывник"),
     _t("+20% explosives damage for each rank.", "+20% урона взрывчаткой за каждый ранг.")),
    ("lucky", 6, 1, {"lck": 6}, [_e("crit", 5)], _t("Lucky", "Везунчик"),
     _t("+5% critical chance.", "+5% к шансу критического попадания.")),
    ("resistance_veteran", 8, 1, {"end": 6}, [_e("dr", 10), _e("hp", 10)], _t("Resistance Veteran", "Ветеран Сопротивления"),
     _t("+10% damage resistance, +10 health.", "+10% сопротивления урону, +10 здоровья.")),
    ("finesse", 10, 1, {}, [_e("crit", 5)], _t("Finesse", "Точность"),
     _t("+5% critical chance.", "+5% к шансу критического попадания.")),
    ("life_giver", 12, 1, {"end": 6}, [_e("hp", 30)], _t("Life Giver", "Живучесть"),
     _t("+30 health.", "+30 здоровья.")),
]

STOCK_TRAITS = [
    ("small_frame", [_e("stat", 1, "agi"), _e("hp", -15)], _t("Small Frame", "Хрупкое телосложение"),
     _t("+1 Agility, but -15 health.", "+1 к Ловкости, но −15 здоровья.")),
    ("heavy_handed", [_e("damage", 20, "melee"), _e("damage", -5, "guns")], _t("Heavy Handed", "Тяжёлая рука"),
     _t("+20% melee damage, -5% damage with guns.", "+20% урона в ближнем бою, −5% урона из огнестрела.")),
    ("former_scientist", [_e("skill", 10, "science"), _e("skill", 5, "medicine"), _e("stat", -1, "str")],
     _t("Former Scientist", "Бывший учёный"), _t("+10 Science, +5 Medicine, but -1 Strength.", "+10 к Науке, +5 к Медицине, но −1 к Силе.")),
    ("smuggler", [_e("price", 10), _e("skill", -5, "speech")], _t("Smuggler", "Контрабандист"),
     _t("Prices 10% better, but -5 Speech.", "Цены на 10% выгоднее, но −5 к Красноречию.")),
    ("built_tough", [_e("hp", 20), _e("xp", -10)], _t("Built Tough", "Крепыш"),
     _t("+20 health, but 10% less experience.", "+20 здоровья, но на 10% меньше опыта.")),
    ("fast_learner", [_e("xp", 10), _e("points", -2)], _t("Fast Learner", "Способный"),
     _t("+10% experience, but 2 skill points less every level.", "+10% опыта, но на 2 очка навыков меньше за уровень.")),
    ("loner", [_e("skill", 10, "survival"), _e("skill", 5, "sneak"), _e("skill", -10, "speech")], _t("Loner", "Одиночка"),
     _t("+10 Survival, +5 Sneak, but -10 Speech.", "+10 к Выживанию, +5 к Скрытности, но −10 к Красноречию.")),
    ("combine_defector", [_e("skill", 15, "energy"), _e("skill", -5, "guns")], _t("Combine Defector", "Перебежчик"),
     _t("+15 Combine Weapons, but -5 Guns.", "+15 к Оружию Альянса, но −5 к Огнестрелу.")),
]


def stock_perks():
    out = []
    for pid, level, ranks, req, effects, name, desc in STOCK_PERKS:
        p = Perk(pid)
        p.level, p.ranks, p.req, p.effects, p.name, p.desc = level, ranks, dict(req), copy.deepcopy(effects), dict(name), dict(desc)
        out.append(p)
    return out


def stock_traits():
    out = []
    for tid, effects, name, desc in STOCK_TRAITS:
        t = Trait(tid)
        t.effects, t.name, t.desc = copy.deepcopy(effects), dict(name), dict(desc)
        out.append(t)
    return out


def default_text(cid, key):
    """Built-in name / description of an attribute or skill."""
    for sid, name, desc in STATS:
        if sid == cid:
            return dict(name if key == "name" else desc)
    for sid, _stat, name, desc in SKILLS:
        if sid == cid:
            return dict(name if key == "name" else desc)
    return {}


def skill_stat(project, sid):
    over = (project.rpg_texts.get(sid) or {}).get("stat")
    if over:
        return over
    for s, stat, _n, _d in SKILLS:
        if s == sid:
            return stat
    return ""


def char_text(project, cid, key):
    t = default_text(cid, key)
    t.update({k: v for k, v in ((project.rpg_texts.get(cid) or {}).get(key) or {}).items() if v})
    return t


def rule(project, key):
    return project.rpg_rules.get(key, RULE_DEFAULTS.get(key, 0))


def uses_rpg(project):
    return bool(project.perks or project.traits or project.rpg_rules or project.rpg_texts)


# --- game files -------------------------------------------------------------------------------
def _num(v):
    if isinstance(v, float):
        return ("%.4f" % v).rstrip("0").rstrip(".") or "0"
    return str(v)


def _texts(blk, key, texts, base):
    from .world_export import _texts as wt
    wt(blk, key, texts, base)


def _def_kv(parent, d, base, trait=False):
    b = parent.add(d.id, None, d.note)
    _texts(b, "name", d.name, base)
    _texts(b, "desc", d.desc, base)
    if not trait:
        b.add("level", str(int(d.level)))
        if d.ranks != 1:
            b.add("ranks", str(int(d.ranks)))
        if d.req:
            r = b.add("req")
            for k, v in sorted(d.req.items()):
                r.add(k, str(int(v)))
            r.inline = True
    if d.effects:
        e = b.add("effects")
        for x in d.effects:
            c = e.add("e")
            c.add("type", x.get("type", ""))
            if x.get("id"):
                c.add("id", x["id"])
            c.add("value", _num(x.get("value", 0)))
            c.inline = True
    return b


def rpg_kv(project):
    base = project.base_language
    root = kv.KV("RPG")
    rb = root.add("rules")
    for key, default, _l, _g, _dec in RULES:
        rb.add(key, _num(rule(project, key)))
    sb = root.add("stats")
    for sid in STAT_IDS:
        b = sb.add(sid)
        _texts(b, "name", char_text(project, sid, "name"), base)
        _texts(b, "desc", char_text(project, sid, "desc"), base)
    kb = root.add("skills")
    for sid in SKILL_IDS:
        b = kb.add(sid)
        b.add("stat", skill_stat(project, sid))
        _texts(b, "name", char_text(project, sid, "name"), base)
        _texts(b, "desc", char_text(project, sid, "desc"), base)
    pb = root.add("perks")
    for p in project.perks:
        if p.id:
            _def_kv(pb, p, base)
    tb = root.add("traits")
    for t in project.traits:
        if t.id:
            _def_kv(tb, t, base, trait=True)
    return root


def newgame_kv(project):
    """scripts/rpg/newgame.txt from project.newgame (start map, point, character creation, start actions)."""
    ng = project.newgame
    root = kv.KV("NewGame")
    if ng.get("map"):
        root.add("map", ng["map"])
    sp = ng.get("spawn")
    if sp:
        root.add("spawn", " ".join(_num(round(float(c), 1)) for c in sp))
        root.add("yaw", _num(round(float(ng.get("yaw", 0)), 1)))
    root.add("create", "1" if ng.get("create", True) else "0")
    if ng.get("name"):
        root.add("name", ng["name"])
    actions = []
    if ng.get("time") is not None and ng.get("time") != "":
        actions.append({"type": "time", "value": float(ng["time"])})
    if ng.get("weather"):
        actions.append({"type": "weather", "value": ng["weather"], "seconds": 0})
    for item, n in ng.get("items", []):
        if item:
            actions.append({"type": "give", "item": item, "count": int(n)})
    if int(ng.get("money", 0) or 0):
        actions.append({"type": "money", "value": int(ng["money"])})
    if ng.get("quest"):
        actions.append({"type": "quest_start", "quest": ng["quest"]})
    actions += list(ng.get("actions", []))
    logic.write_actions(root, "actions", actions, project)
    return root


def import_rpg_char(project, root):
    """rpg.txt (KV root "RPG") back into the project."""
    from .world_import import _loc_texts
    base = project.base_language
    top = root.block("RPG") or (root.blocks()[0] if root.blocks() else None)
    if top is None:
        return
    rb = top.block("rules")
    project.rpg_rules = {}
    for c in (rb.values() if rb else []):
        try:
            v = float(c.value)
        except ValueError:
            continue
        if c.name in RULE_DEFAULTS and v != float(RULE_DEFAULTS[c.name]):
            project.rpg_rules[c.name] = int(v) if v == int(v) and isinstance(RULE_DEFAULTS[c.name], int) else v
    project.rpg_texts = {}
    for sect in ("stats", "skills"):
        sb = top.block(sect)
        for b in (sb.blocks() if sb else []):
            over = {}
            for key in ("name", "desc"):
                t = _loc_texts(b, key, base)
                d = default_text(b.name, key)
                diff = {lang: v for lang, v in t.items() if v and v != d.get(lang)}
                if diff:
                    over[key] = diff
            if sect == "skills" and b.get("stat") and b.get("stat") != skill_stat(project, b.name):
                over["stat"] = b.get("stat")
            if over:
                project.rpg_texts[b.name] = over

    def defs(block, cls):
        out = []
        for b in (block.blocks() if block else []):
            d = cls(b.name)
            d.name = _loc_texts(b, "name", base)
            d.desc = _loc_texts(b, "desc", base)
            d.note = b.comment or ""
            if cls is Perk:
                d.level = int(float(b.get("level", "1") or 1))
                d.ranks = int(float(b.get("ranks", "1") or 1))
                r = b.block("req")
                d.req = {c.name: int(float(c.value)) for c in (r.values() if r else [])}
            e = b.block("effects")
            for x in (e.blocks() if e else []):
                eff = {"type": x.get("type", ""), "value": float(x.get("value", "0") or 0)}
                if eff["value"] == int(eff["value"]):
                    eff["value"] = int(eff["value"])
                if x.get("id"):
                    eff["id"] = x.get("id")
                d.effects.append(eff)
            out.append(d)
        return out
    project.perks = defs(top.block("perks"), Perk)
    project.traits = defs(top.block("traits"), Trait)


def import_newgame(project, root):
    top = root.block("NewGame") or (root.blocks()[0] if root.blocks() else None)
    if top is None:
        return
    ng = {"create": top.get("create", "1") not in ("0", "")}
    if top.get("map"):
        ng["map"] = top.get("map")
    if top.get("spawn"):
        try:
            ng["spawn"] = [float(x) for x in top.get("spawn").split()[:3]]
            ng["yaw"] = float(top.get("yaw", "0") or 0)
        except ValueError:
            pass
    if top.get("name"):
        ng["name"] = top.get("name")
    items, rest = [], []
    for a in logic.read_actions(top.block("actions"), project.languages):
        t = a.get("type")
        if t == "time" and "time" not in ng:
            ng["time"] = float(a.get("value", 8))
        elif t == "weather" and "weather" not in ng:
            ng["weather"] = a.get("value", "")
        elif t == "give":
            items.append([a.get("item", ""), int(a.get("count", 1) or 1)])
        elif t == "money" and "money" not in ng:
            ng["money"] = int(a.get("value", 0) or 0)
        elif t == "quest_start" and "quest" not in ng:
            ng["quest"] = a.get("quest", "")
        else:
            rest.append(a)
    ng["items"] = items
    ng["actions"] = rest
    project.newgame = ng
