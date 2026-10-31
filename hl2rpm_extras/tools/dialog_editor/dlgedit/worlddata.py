"""World content of the HL2RPM Creation Kit: what an NPC is and does (Actor: class,
model, weapon, faction, AI packages), factions, and what is placed on each map
(MapLayer: references - NPCs, markers, items, containers, props, patrol paths -
and AI nodes). Game files:

    scripts/npcs/<targetname>.txt   - an actor with its packages
    scripts/world/<map>.txt         - the map layer, spawned by the game on a new map
    scripts/world/<map>_nav.txt     - AI nodes ("x y z" per line)
    scripts/world/factions.txt      - faction relations
"""
import copy
import uuid

# --- AI packages -------------------------------------------------------------------------
PACKAGE_TYPES = [
    ("sleep", "Сон", "Ложится на кровать: свою (маркер с владельцем), иначе свободную в радиусе; без кровати — на землю"),
    ("eat", "Еда", "Садится за стол (маркер «еда», иначе стул) и сидит до конца пакета"),
    ("sandbox", "Свободное время", "Бродит в радиусе, садится на стулья, облокачивается о стены, сидит на земле"),
    ("patrol", "Патруль", "Идёт по маршруту, ждёт в точках"),
    ("wander", "Бродить", "Гуляет по случайным местам в радиусе"),
    ("travel", "Идти и остаться", "Идёт в место и остаётся там"),
    ("guard", "Охрана", "Стоит на посту лицом в заданную сторону, возвращается, если сдвинули"),
    ("use_marker", "Маркер", "Занимает маркер (по имени или любой выбранного вида) до конца пакета"),
    ("wait", "Стоять", "Стоит на месте (можно с анимацией)"),
    ("follow", "Следовать", "Идёт за игроком или объектом"),
    ("talk", "Заговорить", "Подходит к игроку и начинает свой диалог (один раз)"),
]
PACKAGE_NAMES = {k: n for k, n, _ in PACKAGE_TYPES}
PACKAGE_HELP = {k: h for k, _, h in PACKAGE_TYPES}
PACKAGE_COLORS = {"sleep": "#5b6fd6", "eat": "#d68a3a", "sandbox": "#4fae6a", "patrol": "#c9534f", "wander": "#7cb35a",
                  "travel": "#4aa3b8", "guard": "#b0473f", "use_marker": "#b98bd6", "wait": "#8a96a6",
                  "follow": "#d6c34a", "talk": "#e07ab8"}

LOCATIONS = [("self", "Своё место (где поставлен)"), ("ref", "Маркер или объект"), ("point", "Точка на карте"),
             ("player", "Возле игрока"), ("current", "Где стоит сейчас")]
LOCATION_NAMES = dict(LOCATIONS)

# --- markers (furniture / idle markers of GECK) -------------------------------------------
MARKER_KINDS = [("sleep", "Кровать", "#5b6fd6"), ("sit", "Стул / диван", "#b98bd6"), ("eat", "Стол (еда)", "#d68a3a"),
                ("lean", "Стена (облокотиться)", "#4aa3b8"), ("ground", "Сидеть на земле", "#7cb35a"),
                ("work", "Работа", "#d6c34a"), ("guard", "Пост", "#c9534f"), ("idle", "Точка ожидания", "#8a96a6")]
MARKER_NAMES = {k: n for k, n, _ in MARKER_KINDS}
MARKER_COLORS = {k: c for k, _, c in MARKER_KINDS}

# actbusy entries (scripts/actbusy.txt of the game) with a name a person understands
ANIMS = [
    ("laycouch1", "Лежать (кровать, диван)", "sleep"),
    ("d1_t03_Sit_Bed", "Сидеть на кровати", "sleep"),
    ("ACT_BUSY_SIT_CHAIR", "Сидеть на стуле", "sit"),
    ("Sit_Chair02", "Сидеть на стуле 2", "sit"),
    ("Sit_Chair03", "Сидеть на стуле 3", "sit"),
    ("sitcouch1", "Сидеть на диване", "sit"),
    ("sitcouchfeet1", "Диван, ноги на столе", "sit"),
    ("sitcouchknees1", "Диван, колени к груди", "sit"),
    ("d1_t03_Sit_couch", "Диван (устало)", "sit"),
    ("d1_t01_BreakRoom_Sit01_Idle", "Сидеть (комната отдыха)", "sit"),
    ("d1_t02_Plaza_Sit01_Idle", "Сидеть на скамейке", "sit"),
    ("d1_t02_Plaza_Sit02", "Сидеть на скамейке 2", "sit"),
    ("sitchairtable1", "Сидеть за столом", "eat"),
    ("drinker_sit_idle", "Сидеть с выпивкой", "eat"),
    ("d1_t01_BreakRoom_WatchClock_Sit", "Сидеть, смотреть на часы", "eat"),
    ("ACT_BUSY_LEAN_BACK", "Облокотиться спиной", "lean"),
    ("ACT_BUSY_LEAN_LEFT", "Облокотиться боком", "lean"),
    ("d1_town05_Leon_Lean_Table_Idle", "Опереться о стол", "lean"),
    ("railing_idle", "Опереться о перила", "lean"),
    ("ACT_BUSY_SIT_GROUND", "Сидеть на земле", "ground"),
    ("D1_C03_Matt_sitground", "Сидеть на земле 2", "ground"),
    ("D1_C01a_Mary_kneel", "На коленях", "ground"),
    ("d1_t02_Playground_Cit1_Arms_Crossed", "Стоять, скрестив руки", "work"),
    ("d1_t02_Playground_Cit2_Pockets", "Стоять, руки в карманах", "work"),
    ("d1_t03_LookOutWindow", "Смотреть в окно", "work"),
    ("d1_t03_Tenements_Look_Out_Window_Idle", "Смотреть в окно 2", "work"),
    ("LineIdle01", "Стоять в очереди", "work"),
    ("binoculars_idle", "Бинокль", "work"),
    ("core_control_console_type", "Печатать за пультом", "work"),
    ("idle_alert_01", "Настороже", "guard"),
    ("idle_alert_02", "Настороже 2", "guard"),
    ("Cover_crouch", "В укрытии, присев", "guard"),
    ("Crouch_idle_smg1_A", "Присев с оружием", "guard"),
    ("idle_relaxed_01", "Стоять расслабленно", "idle"),
    ("idle_relaxed_02", "Стоять расслабленно 2", "idle"),
    ("Zombie_Slump_A", "Зомби: сидит у стены", "ground"),
]
ANIM_NAMES = {a: n for a, n, _ in ANIMS}
DEFAULT_MARKER_ANIM = {"sleep": "laycouch1", "sit": "ACT_BUSY_SIT_CHAIR", "eat": "sitchairtable1",
                       "lean": "ACT_BUSY_LEAN_BACK", "ground": "ACT_BUSY_SIT_GROUND", "work": "", "guard": "", "idle": ""}

# --- NPC classes -----------------------------------------------------------------------------
NPC_CLASSES = [
    ("npc_citizen", "Горожанин / повстанец", "models/humans/group01/male_07.mdl", True),
    ("npc_metropolice", "Гражданская оборона", "models/police.mdl", True),
    ("npc_combine_s", "Солдат Альянса", "models/combine_soldier.mdl", True),
    ("npc_vortigaunt", "Вортигонт", "models/vortigaunt.mdl", True),
    ("npc_alyx", "Аликс", "models/alyx.mdl", True),
    ("npc_barney", "Барни", "models/barney.mdl", True),
    ("npc_monk", "Отец Григорий", "models/monk.mdl", True),
    ("npc_eli", "Илай", "models/eli.mdl", False),
    ("npc_kleiner", "Кляйнер", "models/kleiner.mdl", False),
    ("npc_mossman", "Мосман", "models/mossman.mdl", False),
    ("npc_breen", "Брин", "models/breen.mdl", False),
    ("npc_fisherman", "Рыбак", "models/lostcoast/fisherman/fisherman.mdl", False),
    ("npc_zombie", "Зомби", "models/zombie/classic.mdl", True),
    ("npc_fastzombie", "Быстрый зомби", "models/zombie/fast.mdl", True),
    ("npc_poisonzombie", "Ядовитый зомби", "models/zombie/poison.mdl", True),
    ("npc_zombine", "Зомбайн", "models/zombie/zombie_soldier.mdl", True),
    ("npc_antlion", "Муравьиный лев", "models/antlion.mdl", False),
    ("npc_antlionguard", "Страж муравьиных львов", "models/antlion_guard.mdl", False),
    ("npc_headcrab", "Хедкраб", "models/headcrabclassic.mdl", False),
    ("npc_dog", "Пёс", "models/dog.mdl", False),
]
NPC_CLASS_NAMES = {c: n for c, n, _, _ in NPC_CLASSES}
NPC_DEFAULT_MODEL = {c: m for c, _, m, _ in NPC_CLASSES}
NPC_HAS_ROUTINE = {c: r for c, _, _, r in NPC_CLASSES}
CITIZEN_TYPES = [("", "Как задано моделью"), ("1", "Запуганный (рабочая одежда)"), ("2", "Беженец"), ("3", "Повстанец"),
                 ("4", "Своя модель")]
RELATIONS = [("", "По фракции / по классу"), ("friend", "Друг"), ("neutral", "Нейтрален"), ("enemy", "Враг")]
RELATION_NAMES = dict(RELATIONS)
WEAPONS = ["", "weapon_crowbar", "weapon_stunstick", "weapon_pistol", "weapon_357", "weapon_smg1", "weapon_ar2",
           "weapon_shotgun", "weapon_crossbow", "weapon_rpg", "weapon_annabelle", "weapon_alyxgun"]

REF_TYPES = [("npc", "NPC"), ("marker", "Маркер"), ("item", "Предмет"), ("container", "Контейнер"),
             ("prop", "Проп"), ("path", "Маршрут"), ("entity", "Сущность")]
REF_TYPE_NAMES = dict(REF_TYPES)

CONTAINER_MODELS = [
    ("models/props_junk/wood_crate001a.mdl", "Деревянный ящик"),
    ("models/props_junk/wood_crate002a.mdl", "Большой ящик"),
    ("models/props_c17/lockers001a.mdl", "Шкафчики"),
    ("models/props_c17/furnituredresser001a.mdl", "Комод"),
    ("models/props_c17/furniturefridge001a.mdl", "Холодильник"),
    ("models/props_c17/furniturecupboard001a.mdl", "Шкаф"),
    ("models/props_wasteland/controlroom_filecabinet001a.mdl", "Картотека"),
    ("models/items/ammocrate_smg1.mdl", "Ящик патронов"),
    ("models/props_junk/trashdumpster01a.mdl", "Мусорный бак"),
    ("models/props_c17/oildrum001.mdl", "Бочка"),
]
PROP_MODELS = [
    ("models/props_c17/furniturebed001a.mdl", "Кровать"),
    ("models/props_c17/furniturecouch001a.mdl", "Диван"),
    ("models/props_c17/furniturecouch002a.mdl", "Диван 2"),
    ("models/props_c17/furniturechair001a.mdl", "Стул"),
    ("models/props_c17/chair02a.mdl", "Стул 2"),
    ("models/props_c17/furnituretable001a.mdl", "Стол круглый"),
    ("models/props_c17/furnituretable002a.mdl", "Стол"),
    ("models/props_c17/furnituretable003a.mdl", "Стол 3"),
    ("models/props_interiors/furniture_desk01a.mdl", "Письменный стол"),
    ("models/props_c17/bench01a.mdl", "Скамейка"),
    ("models/props_c17/furnituremattress001a.mdl", "Матрас"),
    ("models/props_wasteland/cafeteria_table001a.mdl", "Стол столовой"),
    ("models/props_wasteland/cafeteria_bench001a.mdl", "Скамья столовой"),
    ("models/props_junk/trashbin01a.mdl", "Урна"),
    ("models/props_c17/lamppost03a_off.mdl", "Фонарь"),
]


def _vec(v, n=3):
    v = list(v or [])
    return [float(x) for x in (v + [0.0] * n)[:n]]


class Package:
    """One AI package (GECK): what the NPC does in a time window."""

    def __init__(self, ptype="wander"):
        self.uid = uuid.uuid4().hex[:8]
        self.name = PACKAGE_NAMES.get(ptype, ptype)
        self.type = ptype
        self.start = 8.0
        self.duration = 24.0 if ptype in ("wander", "sandbox", "guard", "wait", "talk", "follow") else 8.0
        self.location = "self"
        self.ref = ""                 # marker / entity targetname (location "ref", use_marker)
        self.point = [0.0, 0.0, 0.0]
        self.point_map = ""
        self.yaw = None
        self.radius = 0
        self.run = False
        self.anim = ""
        self.markers = []             # sandbox / use_marker kinds
        self.path = ""
        self.path_mode = "loop"
        self.target = ""
        self.once = ptype == "talk"
        self.min_use = 30
        self.max_use = 90
        self.req_state = None         # [quest, state]
        self.req_stage = None         # [quest, stage]
        self.note = ""

    def always(self):
        return self.duration >= 24

    def to_dict(self):
        d = {"uid": self.uid, "name": self.name, "type": self.type, "start": self.start, "duration": self.duration,
             "location": self.location}
        for k in ("ref", "point_map", "anim", "path", "target", "note"):
            if getattr(self, k):
                d[k] = getattr(self, k)
        if self.location == "point" or any(self.point):
            d["point"] = [round(x, 2) for x in self.point]
        if self.yaw is not None:
            d["yaw"] = round(self.yaw, 1)
        if self.radius:
            d["radius"] = self.radius
        if self.run:
            d["run"] = True
        if self.markers:
            d["markers"] = list(self.markers)
        if self.path_mode != "loop":
            d["path_mode"] = self.path_mode
        if self.once:
            d["once"] = True
        if (self.min_use, self.max_use) != (30, 90):
            d["use_time"] = [self.min_use, self.max_use]
        if self.req_state:
            d["req_state"] = list(self.req_state)
        if self.req_stage:
            d["req_stage"] = list(self.req_stage)
        return d

    @staticmethod
    def from_dict(d):
        p = Package(d.get("type", "wander"))
        p.uid = d.get("uid") or p.uid
        p.name = d.get("name", p.name)
        p.start = float(d.get("start", 8.0))
        p.duration = float(d.get("duration", p.duration))
        p.location = d.get("location", "self")
        p.ref = d.get("ref", "")
        p.point = _vec(d.get("point"))
        p.point_map = d.get("point_map", "")
        y = d.get("yaw")
        p.yaw = float(y) if y is not None else None
        p.radius = int(d.get("radius", 0))
        p.run = bool(d.get("run", False))
        p.anim = d.get("anim", "")
        p.markers = list(d.get("markers", []))
        p.path = d.get("path", "")
        p.path_mode = d.get("path_mode", "loop")
        p.target = d.get("target", "")
        p.once = bool(d.get("once", False))
        ut = d.get("use_time")
        if ut:
            p.min_use, p.max_use = int(ut[0]), int(ut[1])
        rs = d.get("req_state")
        p.req_state = [rs[0], rs[1]] if rs else None
        rg = d.get("req_stage")
        p.req_stage = [rg[0], int(rg[1])] if rg else None
        p.note = d.get("note", "")
        return p

    def active_at(self, hour):
        if self.always():
            return True
        return (hour - self.start) % 24.0 < self.duration


class Actor:
    """What an NPC is (GECK actor base): class, look, weapon, faction, AI data, packages."""

    def __init__(self):
        self.classname = "npc_citizen"
        self.model = ""
        self.skin = 0
        self.citizen_type = ""
        self.weapon = ""
        self.squad = ""
        self.faction = ""
        self.relation = ""
        self.health = 0
        self.essential = False
        self.commandable = False
        self.hate_on_dmg = True
        self.spoil_on_dmg = False
        self.loot_items = []          # [[classname, amount], ...]
        self.keyvalues = {}           # anything else for the spawn
        self.packages = []

    def model_or_default(self):
        return self.model or NPC_DEFAULT_MODEL.get(self.classname, "models/humans/group01/male_07.mdl")

    def is_default(self):
        return (self.classname == "npc_citizen" and not self.model and not self.weapon and not self.faction
                and not self.relation and not self.packages and not self.essential and not self.health
                and not self.loot_items and not self.keyvalues and not self.squad and not self.commandable
                and not self.citizen_type and self.hate_on_dmg and not self.spoil_on_dmg and not self.skin)

    def to_dict(self):
        d = {"classname": self.classname}
        for k in ("model", "citizen_type", "weapon", "squad", "faction", "relation"):
            if getattr(self, k):
                d[k] = getattr(self, k)
        for k in ("skin", "health"):
            if getattr(self, k):
                d[k] = getattr(self, k)
        for k in ("essential", "commandable", "spoil_on_dmg"):
            if getattr(self, k):
                d[k] = True
        if not self.hate_on_dmg:
            d["hate_on_dmg"] = False
        if self.loot_items:
            d["loot_items"] = [[a, int(b)] for a, b in self.loot_items]
        if self.keyvalues:
            d["keyvalues"] = dict(self.keyvalues)
        if self.packages:
            d["packages"] = [p.to_dict() for p in self.packages]
        return d

    @staticmethod
    def from_dict(d):
        a = Actor()
        if not d:
            return a
        a.classname = d.get("classname", "npc_citizen")
        for k in ("model", "citizen_type", "weapon", "squad", "faction", "relation"):
            setattr(a, k, d.get(k, ""))
        a.skin = int(d.get("skin", 0))
        a.health = int(d.get("health", 0))
        a.essential = bool(d.get("essential", False))
        a.commandable = bool(d.get("commandable", False))
        a.hate_on_dmg = bool(d.get("hate_on_dmg", True))
        a.spoil_on_dmg = bool(d.get("spoil_on_dmg", False))
        a.loot_items = [[x[0], int(x[1])] for x in d.get("loot_items", [])]
        a.keyvalues = dict(d.get("keyvalues", {}))
        a.packages = [Package.from_dict(x) for x in d.get("packages", [])]
        return a

    def package_at(self, hour, quests_ok=None):
        """The package the game runs at that hour (first match, like GECK); conditions are
        assumed true unless quests_ok(package) says otherwise."""
        for p in self.packages:
            if p.active_at(hour) and (quests_ok is None or quests_ok(p)):
                return p
        return None


class Faction:
    def __init__(self, fid="faction"):
        self.id = fid
        self.name = {}
        self.player = ""              # "" / friend / neutral / enemy
        self.relations = {}           # other id -> friend / neutral / enemy
        self.note = ""

    def to_dict(self):
        d = {"id": self.id, "name": self.name, "player": self.player, "relations": dict(self.relations)}
        if self.note:
            d["note"] = self.note
        return d

    @staticmethod
    def from_dict(d):
        f = Faction(d.get("id", "faction"))
        f.name = dict(d.get("name", {}))
        f.player = d.get("player", "")
        f.relations = dict(d.get("relations", {}))
        f.note = d.get("note", "")
        return f


class PathPoint:
    def __init__(self, pos=(0, 0, 0)):
        self.pos = _vec(pos)
        self.wait = 0.0
        self.yaw = None
        self.anim = ""

    def to_dict(self):
        d = {"pos": [round(x, 2) for x in self.pos]}
        if self.wait:
            d["wait"] = self.wait
        if self.yaw is not None:
            d["yaw"] = round(self.yaw, 1)
        if self.anim:
            d["anim"] = self.anim
        return d

    @staticmethod
    def from_dict(d):
        p = PathPoint(d.get("pos"))
        p.wait = float(d.get("wait", 0.0))
        y = d.get("yaw")
        p.yaw = float(y) if y is not None else None
        p.anim = d.get("anim", "")
        return p


class Ref:
    """Something the kit placed on a map (GECK reference)."""

    def __init__(self, rid, rtype, pos=(0, 0, 0)):
        self.id = rid
        self.type = rtype
        self.pos = _vec(pos)
        self.angles = [0.0, 0.0, 0.0]
        self.name = ""               # targetname (markers, paths: required; others optional)
        self.npc = ""                # type npc: actor targetname
        self.kind = "idle"           # marker kind
        self.anim = ""               # marker animation
        self.owner = ""              # marker owner (NPC targetname)
        self.classname = ""          # item / entity
        self.model = ""              # container / prop
        self.skin = 0
        self.physics = False         # prop
        self.loot_items = []         # container
        self.display_name = {}       # container
        self.points = []             # path
        self.keyvalues = {}
        self.note = ""

    @property
    def yaw(self):
        return self.angles[1]

    def to_dict(self):
        d = {"id": self.id, "type": self.type, "pos": [round(x, 2) for x in self.pos]}
        if any(self.angles):
            d["angles"] = [round(x, 2) for x in self.angles]
        for k in ("name", "npc", "anim", "owner", "classname", "model", "note"):
            if getattr(self, k):
                d[k] = getattr(self, k)
        if self.type == "marker":
            d["kind"] = self.kind
        if self.skin:
            d["skin"] = self.skin
        if self.physics:
            d["physics"] = True
        if self.loot_items:
            d["loot_items"] = [[a, int(b)] for a, b in self.loot_items]
        if self.display_name:
            d["display_name"] = dict(self.display_name)
        if self.points:
            d["points"] = [p.to_dict() for p in self.points]
        if self.keyvalues:
            d["keyvalues"] = dict(self.keyvalues)
        return d

    @staticmethod
    def from_dict(d):
        r = Ref(int(d.get("id", 0)), d.get("type", "marker"), d.get("pos"))
        r.angles = _vec(d.get("angles"))
        for k in ("name", "npc", "anim", "owner", "classname", "model", "note"):
            setattr(r, k, d.get(k, ""))
        r.kind = d.get("kind", "idle")
        r.skin = int(d.get("skin", 0))
        r.physics = bool(d.get("physics", False))
        r.loot_items = [[x[0], int(x[1])] for x in d.get("loot_items", [])]
        r.display_name = dict(d.get("display_name", {}))
        r.points = [PathPoint.from_dict(x) for x in d.get("points", [])]
        r.keyvalues = dict(d.get("keyvalues", {}))
        return r

    def clone(self, new_id):
        r = Ref.from_dict(copy.deepcopy(self.to_dict()))
        r.id = new_id
        return r

    def label(self):
        if self.type == "npc":
            return self.npc or "NPC"
        if self.type == "marker":
            return self.name or MARKER_NAMES.get(self.kind, self.kind)
        if self.type == "path":
            return self.name or "маршрут"
        if self.type in ("item", "entity"):
            return self.name or self.classname
        if self.type in ("container", "prop"):
            return self.name or (self.model.rsplit("/", 1)[-1][:-4] if self.model else self.type)
        return self.type


class MapLayer:
    def __init__(self, name):
        self.name = name
        self.refs = []
        self.nav = []                 # [[x, y, z], ...] AI nodes
        self.nav_spacing = 128
        self.camera = None            # editor: last camera [x, y, z, pitch, yaw]

    def ref(self, rid):
        for r in self.refs:
            if r.id == rid:
                return r
        return None

    def new_id(self):
        return max([r.id for r in self.refs] + [0]) + 1

    def marker_names(self):
        return sorted(r.name for r in self.refs if r.type == "marker" and r.name)

    def path_names(self):
        return sorted(r.name for r in self.refs if r.type == "path" and r.name)

    def to_dict(self):
        d = {"name": self.name, "refs": [r.to_dict() for r in self.refs]}
        if self.nav:
            d["nav"] = [[round(c, 1) for c in p] for p in self.nav]
            d["nav_spacing"] = self.nav_spacing
        if self.camera:
            d["camera"] = [round(x, 2) for x in self.camera]
        return d

    @staticmethod
    def from_dict(d):
        m = MapLayer(d.get("name", "map"))
        m.refs = [Ref.from_dict(x) for x in d.get("refs", [])]
        m.nav = [list(map(float, p)) for p in d.get("nav", [])]
        m.nav_spacing = int(d.get("nav_spacing", 128))
        m.camera = d.get("camera")
        return m
