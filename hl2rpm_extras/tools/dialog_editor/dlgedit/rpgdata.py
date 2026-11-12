"""RPG data of the project: items (scripts/rpg/items.txt) and loot lists
(scripts/rpg/lootlists.txt). Records are described by a field table, so the editor
pages, the export and the import all read the same description."""

# field kinds: "text" = {language: str}, "str", "int", "float", "bool"
ITEM_FIELDS = [
    ("id", "str", ""), ("base", "str", ""), ("category", "str", "misc"),
    ("name", "text", None), ("desc", "text", None), ("text", "text", None),
    ("model", "str", ""), ("weight", "float", -1.0), ("value", "int", 0), ("heal", "int", 0),
    ("stackable", "bool", False), ("quest", "bool", False),
    ("damage", "float", 0.0), ("fire_rate", "float", 0.0), ("clip", "int", 0), ("skill", "str", ""), ("skill_req", "int", 0),
    ("note", "str", ""),
]

ITEM_CATEGORIES = [
    ("weapon", "Оружие"), ("ammo", "Патроны"), ("apparel", "Одежда и броня"), ("aid", "Медицина"), ("food", "Еда и питьё"),
    ("note", "Записки и книги"), ("key", "Ключи"), ("currency", "Деньги"), ("quest", "Квестовые"), ("misc", "Прочее"),
]
ITEM_CATEGORY_NAMES = dict(ITEM_CATEGORIES)
ITEM_BASES = [
    ("", "сущность игры (classname: weapon_*, item_*)"), ("generic", "свой предмет (item_rpm)"), ("note", "записка / книга (item_rpm)"),
    ("key", "ключ (item_rpm)"), ("currency", "деньги (item_rpm, стопкой)"),
]
WEAPON_SKILLS = [("", "—"), ("guns", "Огнестрел"), ("energy", "Оружие Альянса"), ("melee", "Ближний бой"), ("explosives", "Взрывчатка")]


class Record:
    FIELDS = []

    def __init__(self, **kw):
        for name, kind, default in self.FIELDS:
            if kind == "text":
                setattr(self, name, {})
            else:
                setattr(self, name, default)
        for k, v in kw.items():
            setattr(self, k, v)

    def to_dict(self):
        d = {}
        for name, kind, default in self.FIELDS:
            v = getattr(self, name)
            if kind == "text":
                v = {k: t for k, t in v.items() if t}
                if v:
                    d[name] = v
            elif v != default:
                d[name] = v
        return d

    @classmethod
    def from_dict(cls, d):
        r = cls()
        for name, kind, default in cls.FIELDS:
            if name not in d:
                continue
            v = d[name]
            if kind == "text":
                v = dict(v) if isinstance(v, dict) else {}
            elif kind == "int":
                v = int(v)
            elif kind == "float":
                v = float(v)
            elif kind == "bool":
                v = bool(v)
            else:
                v = str(v)
            setattr(r, name, v)
        return r

    def clone(self):
        return type(self).from_dict(self.to_dict())


class ItemDef(Record):
    FIELDS = ITEM_FIELDS

    def is_generic(self):
        return bool(self.base)

    def label(self, lang, base_lang):
        return self.name.get(lang) or self.name.get(base_lang) or self.id


class LootList(Record):
    """Leveled list: entries {item, min, max, chance (0..100), level (min player level)}."""
    FIELDS = [("id", "str", ""), ("note", "str", ""), ("all", "bool", True)]

    def __init__(self, **kw):
        self.entries = []
        super().__init__(**kw)

    def to_dict(self):
        d = super().to_dict()
        d["entries"] = [dict(e) for e in self.entries]
        return d

    @classmethod
    def from_dict(cls, d):
        r = super().from_dict(d)
        r.entries = [dict(e) for e in d.get("entries", [])]
        return r


def _t(en, ru):
    return {"english": en, "russian": ru}


# the game's own items with names, weights (the mod's inventory_weights.txt) and the
# player damage of skill.cfg (sk_plr_dmg_*)
STOCK_ITEMS = [
    dict(id="weapon_crowbar", category="weapon", name=_t("Crowbar", "Монтировка"), weight=1.5, value=20, damage=10, fire_rate=2.5, skill="melee",
         desc=_t("A heavy steel crowbar. Opens crates, breaks heads.", "Тяжёлая стальная монтировка. Вскрывает ящики и головы."),
         model="models/weapons/w_crowbar.mdl"),
    dict(id="weapon_stunstick", category="weapon", name=_t("Stun baton", "Электродубинка"), weight=1.5, value=60, damage=10, fire_rate=1.25, skill="melee",
         desc=_t("Civil Protection standard issue.", "Штатная дубинка Гражданской обороны."), model="models/weapons/w_stunbaton.mdl"),
    # melee weapons of the HL2 Melee Pack (game/server/hl2rpm_melee.cpp)
    dict(id="weapon_rpm_axe", category="weapon", name=_t("Axe", "Топор"), weight=2.2, value=30, damage=37, fire_rate=1.0, skill="melee",
         desc=_t("A fire axe. Slow, but it splits more than wood.", "Пожарный топор. Медленный, но рубит не только дрова."), model="models/weapons/hl2rpm/w_axe.mdl"),
    dict(id="weapon_rpm_bottle", category="weapon", name=_t("Bottle", "Бутылка"), weight=0.5, value=2, damage=6, fire_rate=2.0, skill="melee",
         desc=_t("An empty bottle. Breaks on a hard hit.", "Пустая бутылка. Бьётся от сильного удара."), model="models/weapons/hl2rpm/w_bottle.mdl"),
    dict(id="weapon_rpm_brokenbottle", category="weapon", name=_t("Broken bottle", "Розочка"), weight=0.4, value=3, damage=11, fire_rate=2.0, skill="melee",
         desc=_t("The neck of a broken bottle. Cuts.", "Горлышко разбитой бутылки. Режет."), model="models/weapons/hl2rpm/w_brokenbottle.mdl"),
    dict(id="weapon_rpm_hook", category="weapon", name=_t("Hook", "Крюк"), weight=1.8, value=25, damage=42, fire_rate=0.8, skill="melee",
         desc=_t("A heavy meat hook.", "Тяжёлый мясницкий крюк."), model="models/weapons/hl2rpm/w_hook.mdl"),
    dict(id="weapon_rpm_pan", category="weapon", name=_t("Frying pan", "Сковорода"), weight=1.4, value=8, damage=15, fire_rate=1.0, skill="melee",
         desc=_t("A cast-iron frying pan. Rings on every hit.", "Чугунная сковорода. Звенит на каждом ударе."), model="models/weapons/hl2rpm/w_pan.mdl"),
    dict(id="weapon_rpm_pickaxe", category="weapon", name=_t("Pickaxe", "Кирка"), weight=3.0, value=28, damage=42, fire_rate=0.74, skill="melee",
         desc=_t("A miner's pickaxe. Heavy and slow.", "Шахтёрская кирка. Тяжёлая и медленная."), model="models/weapons/hl2rpm/w_pickaxe.mdl"),
    dict(id="weapon_rpm_pipe", category="weapon", name=_t("Pipe", "Труба"), weight=1.6, value=10, damage=23, fire_rate=0.9, skill="melee",
         desc=_t("A length of steel pipe.", "Обрезок стальной трубы."), model="models/weapons/hl2rpm/w_pipe.mdl"),
    dict(id="weapon_rpm_pot", category="weapon", name=_t("Pot", "Кастрюля"), weight=1.0, value=6, damage=12, fire_rate=1.1, skill="melee",
         desc=_t("A dented pot. Better than bare hands.", "Мятая кастрюля. Лучше, чем голые руки."), model="models/weapons/hl2rpm/w_pot.mdl"),
    dict(id="weapon_rpm_shovel", category="weapon", name=_t("Shovel", "Лопата"), weight=2.0, value=18, damage=28, fire_rate=0.8, skill="melee",
         desc=_t("A spade. Long reach.", "Штыковая лопата. Бьёт издалека."), model="models/weapons/hl2rpm/w_shovel.mdl"),
    dict(id="weapon_pistol", category="weapon", name=_t("9mm pistol", "Пистолет 9 мм"), weight=1.1, value=120, damage=8, fire_rate=6, clip=18, skill="guns",
         desc=_t("USP Match. Light and reliable.", "USP Match. Лёгкий и надёжный."), model="models/weapons/w_pistol.mdl"),
    dict(id="weapon_rpm_beretta", category="weapon", name=_t("Beretta 92", "Беретта 92"), weight=0.95, value=180, damage=9, fire_rate=4, clip=15, skill="guns",
         desc=_t("Beretta 92: 9 mm, fifteen rounds. Hits harder than the USP; the slide locks back on the last round.",
                 "Беретта 92: 9 мм, пятнадцать патронов. Бьёт сильнее USP; на последнем патроне затвор встаёт на задержку."),
         model="models/weapons/hl2rpm/w_beretta.mdl"),
    dict(id="weapon_357", category="weapon", name=_t(".357 Magnum", "Револьвер .357"), weight=1.5, value=300, damage=40, fire_rate=1.3, clip=6, skill="guns",
         desc=_t("Colt Python. Slow, loud, decisive.", "Кольт «Питон». Медленный, громкий, убедительный."), model="models/weapons/w_357.mdl"),
    dict(id="weapon_smg1", category="weapon", name=_t("SMG", "Пистолет-пулемёт"), weight=3.0, value=250, damage=4, fire_rate=12, clip=45, skill="guns",
         desc=_t("MP7 with an underbarrel grenade launcher.", "MP7 с подствольным гранатомётом."), model="models/weapons/w_smg1.mdl"),
    dict(id="weapon_ar2", category="weapon", name=_t("Pulse rifle", "Импульсная винтовка"), weight=4.0, value=600, damage=8, fire_rate=10, clip=30, skill="energy",
         desc=_t("Overwatch Standard Issue Pulse Rifle.", "Импульсная винтовка Надзора."), model="models/weapons/w_irifle.mdl"),
    dict(id="weapon_shotgun", category="weapon", name=_t("Shotgun", "Дробовик"), weight=3.5, value=350, damage=56, fire_rate=1.5, clip=6, skill="guns",
         desc=_t("SPAS-12. Seven pellets per shot.", "SPAS-12. Семь дробин за выстрел."), model="models/weapons/w_shotgun.mdl"),
    dict(id="weapon_crossbow", category="weapon", name=_t("Crossbow", "Арбалет"), weight=4.5, value=500, damage=100, fire_rate=0.5, clip=1, skill="guns",
         desc=_t("Fires heated rebar.", "Стреляет раскалённой арматурой."), model="models/weapons/w_crossbow.mdl"),
    dict(id="weapon_rpg", category="weapon", name=_t("Rocket launcher", "Ракетомёт"), weight=6.0, value=900, damage=100, fire_rate=0.4, clip=1, skill="explosives",
         desc=_t("Laser-guided rockets.", "Ракеты с лазерным наведением."), model="models/weapons/w_rocket_launcher.mdl"),
    dict(id="weapon_frag", category="weapon", name=_t("Grenade", "Граната"), weight=0.5, value=50, damage=150, skill="explosives",
         desc=_t("MK3A2 fragmentation grenade.", "Осколочная граната MK3A2."), model="models/weapons/w_grenade.mdl"),
    dict(id="weapon_physcannon", category="weapon", name=_t("Gravity gun", "Гравипушка"), weight=5.0, value=0, quest=True,
         desc=_t("Zero Point Energy Field Manipulator.", "Манипулятор поля нулевой энергии."), model="models/weapons/w_physics.mdl"),
    dict(id="item_healthkit", category="aid", name=_t("Medkit", "Аптечка"), weight=1.0, value=40, heal=25,
         desc=_t("Restores 25 health.", "Восстанавливает 25 здоровья."), model="models/items/healthkit.mdl"),
    dict(id="item_healthvial", category="aid", name=_t("Health vial", "Колба здоровья"), weight=0.2, value=15, heal=10,
         desc=_t("Restores 10 health.", "Восстанавливает 10 здоровья."), model="models/healthvial.mdl"),
    dict(id="item_battery", category="apparel", name=_t("Suit battery", "Батарея костюма"), weight=0.5, value=30,
         desc=_t("Charges an HEV suit by 15.", "Заряжает костюм HEV на 15."), model="models/items/battery.mdl"),
    dict(id="item_suit", category="apparel", name=_t("HEV suit", "Костюм HEV"), weight=5.0, value=2000,
         desc=_t("Hazardous Environment Suit, Mark V.", "Защитный костюм H.E.V., пятая модель."), model="models/hl2rpm/items/suit_hev.mdl"),
    dict(id="item_suit_pcv", category="apparel", name=_t("P.C.V. suit", "Костюм P.C.V."), weight=8.0,
         desc=_t("Powered Combat Vest, a prototype for the military.", "Боевой жилет P.C.V., опытный образец для армии."), model="models/hl2rpm/items/suit_pcv.mdl"),
    dict(id="item_suit_cp", category="apparel", name=_t("Civil Protection uniform", "Форма Гражданской обороны"), weight=4.0,
         desc=_t("The uniform and the mask of a Civil Protection unit.", "Форма и маска юнита Гражданской обороны."), model="models/hl2rpm/items/suit_cp.mdl"),
    dict(id="item_combine_soldier_suit", category="apparel", name=_t("Overwatch soldier armor", "Броня солдата Надзора"), weight=7.0,
         desc=_t("Armor of an Overwatch soldier.", "Броня солдата Надзора."), model="models/hl2rpm/items/suit_soldier.mdl"),
    dict(id="item_suit_prison", category="apparel", name=_t("Nova Prospekt guard armor", "Броня охраны Нова Проспект"), weight=7.0,
         desc=_t("Armor of the Nova Prospekt guard.", "Броня охраны Нова Проспект."), model="models/hl2rpm/items/suit_prison.mdl"),
    dict(id="item_suit_elite", category="apparel", name=_t("Overwatch elite armor", "Броня элиты Надзора"), weight=9.0,
         desc=_t("White armor of the Overwatch elite.", "Белая броня элиты Надзора."), model="models/hl2rpm/items/suit_elite.mdl"),
    dict(id="item_suit_worker", category="apparel", name=_t("Combine worker's coverall", "Роба рабочего Альянса"), weight=3.5, value=120,
         desc=_t("The coverall, the hard hat and the respirator of a Combine worker. A work HUD.", "Роба, каска и респиратор рабочего Альянса. Рабочий интерфейс."),
         model="models/hl2rpm/items/suit_worker.mdl"),
    dict(id="item_suit_hazmat", category="apparel", name=_t("Hazmat suit", "Костюм химзащиты"), weight=6.0, value=450,
         desc=_t("A sealed suit for the toxic zones: a mask, an air tank, a dosimeter on the display.", "Герметичный костюм для заражённых зон: маска, баллон, дозиметр на дисплее."),
         model="models/hl2rpm/items/suit_hazmat.mdl"),
    dict(id="item_ammo_pistol", category="ammo", name=_t("9mm rounds", "Патроны 9 мм"), weight=0.01, value=1, model="models/items/boxsrounds.mdl"),
    dict(id="item_ammo_357", category="ammo", name=_t(".357 rounds", "Патроны .357"), weight=0.02, value=4, model="models/items/357ammo.mdl"),
    dict(id="item_ammo_smg1", category="ammo", name=_t("SMG rounds", "Патроны ПП"), weight=0.01, value=1, model="models/items/boxmrounds.mdl"),
    dict(id="item_ammo_ar2", category="ammo", name=_t("Pulse cartridge", "Импульсный картридж"), weight=0.02, value=3, model="models/items/combine_rifle_cartridge01.mdl"),
    dict(id="item_box_buckshot", category="ammo", name=_t("Buckshot", "Дробь"), weight=0.03, value=3, model="models/items/boxbuckshot.mdl"),
    dict(id="item_ammo_crossbow", category="ammo", name=_t("Crossbow bolts", "Арбалетные болты"), weight=0.1, value=8, model="models/items/crossbowrounds.mdl"),
    dict(id="rpm_credits", base="currency", category="currency", name=_t("Tokens", "Жетоны"), weight=0.0, value=1, stackable=True,
         desc=_t("Universal Union tokens. Accepted everywhere.", "Жетоны Альянса. Принимают везде."), model="models/props_lab/box01a.mdl"),
    dict(id="bobby_pin", base="generic", category="misc", name=_t("Bobby pin", "Шпилька"), weight=0.0, value=3, stackable=True,
         desc=_t("Picks locks: the pin finds the spot, the lock turns there. Breaks if you force it elsewhere.",
                 "Отмычка для замков: шпилька ищет «сладкую точку», замок поворачивается в ней. Ломается, если давить мимо."),
         model="models/hl2rpm/items/bobby_pin.mdl"),
    dict(id="rpm_ration", base="generic", category="food", name=_t("Ration", "Паёк"), weight=0.6, value=10, heal=15,
         desc=_t("A standard Civil ration. Tastes of nothing.", "Стандартный паёк гражданина. Безвкусный."), model="models/weapons/w_package.mdl"),
    dict(id="rpm_water", base="generic", category="food", name=_t("Breen's water", "Вода «Брин»"), weight=0.5, value=5, heal=5,
         desc=_t("Dr. Breen's Private Reserve.", "Личный резерв доктора Брина."), model="models/props_junk/popcan01a.mdl"),
]


def stock_items():
    return [ItemDef.from_dict(d) for d in STOCK_ITEMS]
