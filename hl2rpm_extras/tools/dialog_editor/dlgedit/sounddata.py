"""Sound of the project: music tracks (scripts/rpg/music.txt), soundscapes of locations
(scripts/soundscapes_hl2rpm.txt) and NPC barks - their own (scripts/npcs/<npc>.txt) and shared
sets (scripts/rpg/barks.txt). The server side is game/server/hl2rpm_audio.cpp, the client music
game/client/hl2rpm_music.cpp."""
import copy

MUSIC_KINDS = [
    ("explore", "Исследование (фон)"), ("combat", "Бой"), ("location", "Тема локации (зона)"),
    ("stinger", "Короткая вставка (стингер)"),
]
MUSIC_KIND_NAMES = dict(MUSIC_KINDS)
MUSIC_TIMES = [("any", "в любое время"), ("day", "днём (6–21 ч)"), ("night", "ночью (21–6 ч)")]
MUSIC_TIME_NAMES = dict(MUSIC_TIMES)
STINGER_EVENTS = [
    ("", "— только действием «Стингер» —"), ("location", "открыта локация"), ("quest_start", "задание выдано"),
    ("quest_complete", "задание выполнено"), ("quest_fail", "задание провалено"),
]
STINGER_EVENT_NAMES = dict(STINGER_EVENTS)

# bark events of the server (RPMAudio::Bark callers); value = what the line's "value" filters
BARK_EVENTS = [
    ("greet", "Игрок подошёл", ""),
    ("idle", "Просто так (бродит, стоит)", ""),
    ("farewell", "Прощание (после диалога)", ""),
    ("hurt", "Ранен", ""),
    ("combat", "Бой", ""),
    ("weapon", "Игрок с оружием в руках", ""),
    ("ally_death", "Убит союзник", "Кто (targetname; пусто — любой)"),
    ("kill", "Убил кого-то", "Кого (targetname; пусто — любого)"),
    ("theft", "Видел, как игрок крадёт (у него или у его фракции)", ""),
    ("weapon_law", "Запрет оружия (ГО, солдаты): увидел оружие у гражданского",
     "warn — предупреждение, final — последнее, attack — открывает огонь, comply — оружие убрали"),
    ("package", "Начался пакет распорядка", "Пакет (имя; пусто — любой)"),
    ("custom", "По действию «Фраза NPC»", ""),
]
BARK_EVENT_NAMES = {e[0]: e[1] for e in BARK_EVENTS}
BARK_EVENT_VALUE = {e[0]: e[2] for e in BARK_EVENTS}

STOCK_SPEECH = [
    ("", "по умолчанию — только боевые"), ("all", "все фразы HL2 (кроме фраз о Фримене)"),
    ("combat", "только боевые (крики, ранения, бой)"), ("none", "нет — молчит, кроме своих фраз"),
]

SOUNDSCAPE_DSP = [
    (0, "0 — без эха"), (1, "1 — обычно"), (2, "2 — комната"), (6, "6 — большая комната"), (17, "17 — улица"),
    (20, "20 — большой зал"), (23, "23 — канализация/туннель"), (26, "26 — металл"), (35, "35 — подвал"),
]


def _vec2(v, default):
    """"min,max" or a number -> [min, max]."""
    if isinstance(v, (list, tuple)) and len(v) == 2:
        return [float(v[0]), float(v[1])]
    try:
        parts = [float(x) for x in str(v).replace(" ", "").split(",") if x]
    except ValueError:
        return list(default)
    if len(parts) == 1:
        return [parts[0], parts[0]]
    if len(parts) >= 2:
        return parts[:2]
    return list(default)


class MusicTrack:
    def __init__(self, tid="track"):
        self.id = tid
        self.file = ""            # under sound/: "music/hl2_song3.mp3"
        self.volume = 1.0
        self.kind = "explore"
        self.time = "any"
        self.maps = []            # empty - every map
        self.event = ""           # stinger: plays by itself on this event
        self.note = ""

    def to_dict(self):
        d = {"id": self.id, "file": self.file, "kind": self.kind}
        if self.volume != 1.0:
            d["volume"] = self.volume
        if self.time != "any":
            d["time"] = self.time
        if self.maps:
            d["maps"] = list(self.maps)
        if self.event:
            d["event"] = self.event
        if self.note:
            d["note"] = self.note
        return d

    @staticmethod
    def from_dict(d):
        t = MusicTrack(d.get("id", "track"))
        t.file = d.get("file", "")
        t.volume = float(d.get("volume", 1.0))
        t.kind = d.get("kind", "explore")
        t.time = d.get("time", "any")
        t.maps = list(d.get("maps", []))
        t.event = d.get("event", "")
        t.note = d.get("note", "")
        return t

    def label(self):
        name = self.file.rsplit("/", 1)[-1] if self.file else "(нет файла)"
        return "%s — %s" % (self.id, name)


class Soundscape:
    """A soundscape of the stock system: looping sounds + random sounds every few seconds."""

    def __init__(self, name="hl2rpm.new"):
        self.name = name
        self.dsp = 1
        self.loops = []           # {"wave", "volume", "pitch", "soundlevel"}
        self.randoms = []         # {"waves": [...], "time": [min, max], "volume": [min, max], "pitch": [min, max], "soundlevel"}
        self.note = ""

    def to_dict(self):
        d = {"name": self.name, "dsp": self.dsp}
        if self.loops:
            d["loops"] = copy.deepcopy(self.loops)
        if self.randoms:
            d["randoms"] = copy.deepcopy(self.randoms)
        if self.note:
            d["note"] = self.note
        return d

    @staticmethod
    def from_dict(d):
        s = Soundscape(d.get("name", "hl2rpm.new"))
        s.dsp = int(d.get("dsp", 1))
        s.loops = [new_loop(**x) for x in d.get("loops", [])]
        s.randoms = [new_random(**x) for x in d.get("randoms", [])]
        s.note = d.get("note", "")
        return s


def new_loop(wave="", volume=0.6, pitch=100, soundlevel=0):
    return {"wave": wave, "volume": float(volume), "pitch": int(pitch), "soundlevel": int(soundlevel)}


def new_random(waves=None, time=(8, 20), volume=(0.4, 0.7), pitch=(95, 105), soundlevel=0):
    return {"waves": list(waves or []), "time": _vec2(time, (8, 20)), "volume": _vec2(volume, (0.4, 0.7)),
            "pitch": _vec2(pitch, (95, 105)), "soundlevel": int(soundlevel)}


class Bark:
    """One NPC bark: a line said by itself on an event (with conditions), with subtitles."""

    def __init__(self, bid="b1", event="idle"):
        self.id = bid
        self.event = event
        self.value = ""           # event filter (package name, targetname of the victim...)
        self.text = {}            # {language: text}
        self.voices = {}          # {language: model.Voice} - placed like dialog voices (bark_<id>)
        self.sound = ""           # a game sound (path under sound/ or soundscript) when there is no voice
        self.weight = 1.0
        self.cooldown = 30.0
        self.once = False
        self.chance = 100
        self.conditions = []
        self.note = ""

    def to_dict(self):
        d = {"id": self.id, "event": self.event}
        if self.value:
            d["value"] = self.value
        t = {k: v for k, v in self.text.items() if v}
        if t:
            d["text"] = t
        v = {k: x.to_dict() for k, x in self.voices.items() if x and not x.is_empty()}
        if v:
            d["voices"] = v
        if self.sound:
            d["sound"] = self.sound
        if self.weight != 1.0:
            d["weight"] = self.weight
        if self.cooldown != 30.0:
            d["cooldown"] = self.cooldown
        if self.once:
            d["once"] = True
        if self.chance != 100:
            d["chance"] = self.chance
        if self.conditions:
            d["conditions"] = self.conditions
        if self.note:
            d["note"] = self.note
        return d

    @staticmethod
    def from_dict(d):
        from .model import Voice
        b = Bark(d.get("id", "b1"), d.get("event", "idle"))
        b.value = str(d.get("value", ""))
        b.text = dict(d.get("text", {}))
        b.voices = {k: Voice.from_dict(x) for k, x in (d.get("voices") or {}).items()}
        b.sound = d.get("sound", "")
        b.weight = float(d.get("weight", 1.0))
        b.cooldown = float(d.get("cooldown", 30.0))
        b.once = bool(d.get("once", False))
        b.chance = int(d.get("chance", 100))
        b.conditions = list(d.get("conditions", []))
        b.note = d.get("note", "")
        return b

    def clone(self):
        return Bark.from_dict(copy.deepcopy(self.to_dict()))


class BarkSet:
    """Shared barks: NPC of the listed classes (also placed in Hammer) and NPCs that pick the set."""

    def __init__(self, sid="set"):
        self.id = sid
        self.classes = []         # npc_citizen, npc_metropolice...
        self.barks = []
        self.note = ""

    def to_dict(self):
        d = {"id": self.id, "barks": [b.to_dict() for b in self.barks]}
        if self.classes:
            d["classes"] = list(self.classes)
        if self.note:
            d["note"] = self.note
        return d

    @staticmethod
    def from_dict(d):
        s = BarkSet(d.get("id", "set"))
        s.classes = list(d.get("classes", []))
        s.barks = [Bark.from_dict(x) for x in d.get("barks", [])]
        s.note = d.get("note", "")
        return s


def new_bark_id(barks, event):
    used = {b.id for b in barks}
    n = 1
    while "%s%d" % (event, n) in used:
        n += 1
    return "%s%d" % (event, n)


def bark_media_key(bark_id):
    return "bark_%s" % bark_id
