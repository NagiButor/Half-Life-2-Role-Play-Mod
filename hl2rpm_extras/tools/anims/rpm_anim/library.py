"""The list of sequences of the library (per skeleton) and their actbusy entries.

Every entry: (name, maker, Seq options). With make_anims.py --only a,b the other sequences are
taken from the previous build (build.Cached)."""
import numpy as np

from . import build as B
from .sets import eat_chair, fists, kimodo_clips, lean_wall, lie_bed, lie_ground, sit_chair, sit_couch, sit_ground, stand_idle

STOCK = {
    "male": ("models/humans/male_ss.mdl", "models/humans/male_shared_original.mdl"),
    "female": ("models/humans/female_ss.mdl", "models/humans/female_shared_original.mdl"),
    "police": ("models/police_ss.mdl", "models/police_animations_original.mdl"),
    "combine": ("models/combine_soldier_anims_original.mdl", "models/combine_soldier_anims_original.mdl"),
}


def _busy(prefix):
    return {"busy_sequence": prefix + "_idle", "entry_sequence": prefix + "_enter", "exit_sequence": prefix + "_exit",
            "min_time": "0.0", "max_time": "0.0", "interrupts": "BA_INT_NONE"}


# actbusy entries (name, {..}) of every set - written into scripts/actbusy.txt of the mod
ACTBUSY = [
    ("rpm_sit_chair", _busy("rpm_sit_chair")),
    ("rpm_sit_couch", _busy("rpm_sit_couch")),
    ("rpm_sit_ground", _busy("rpm_sit_ground")),
    ("rpm_lean_wall", _busy("rpm_lean_wall")),
    ("rpm_lie_bed", _busy("rpm_lie_bed")),
    ("rpm_lie_ground", _busy("rpm_lie_ground")),
    # made of Kimodo motions (sets/kimodo_clips.py): talking with the hands at a marker, searching on the haunches
    ("rpm_talk", {"busy_sequence": "rpm_talk_idle", "min_time": "0.0", "max_time": "0.0", "interrupts": "BA_INT_NONE"}),
    ("rpm_search", _busy("rpm_search")),
    # eating on a chair: the chair's own sitting down / getting up around the eating idle
    ("rpm_eat_chair", dict(_busy("rpm_sit_chair"), busy_sequence="rpm_eat_chair_idle")),
]


def seat_params(rig):
    """the numbers of the sitting animations for the Creation Kit (male skeleton)"""
    sit_chair.configure(rig, "male")
    base = {"thigh": sit_chair.THIGH, "shin": sit_chair.SHIN, "ankle_z": sit_chair.ANKLE_Z}
    rows = []
    for name, mod, over, ff in (("rpm_sit_chair", sit_chair, sit_chair.PELVIS_OVER_SEAT, 2.5),
                                ("rpm_sit_couch", sit_couch, sit_chair.PELVIS_OVER_SEAT + sit_couch.OVER_SEAT_EXTRA, sit_couch.FEET_FORWARD)):
        d = dict(base)
        d.update({"pelvis_back": mod.PELVIS_BACK, "over_seat": over, "feet_forward": ff,
                  "h_min": mod.SEAT_HEIGHTS[0], "h_max": mod.SEAT_HEIGHTS[-1]})
        rows.append((name, d))
    rows.append(("rpm_eat_chair", dict(rows[0][1])))      # sits exactly like the chair
    # not furniture: where the body ends relative to the marker (ground - the pelvis behind it on the floor,
    # lean - the wall behind the marker)
    rows.append(("rpm_sit_ground", {"pelvis_back": sit_ground.PELVIS_BACK, "ground": 1}))
    rows.append(("rpm_lean_wall", {"wall_dist": lean_wall.WALL_DIST, "wall": 1}))
    rows.append(("rpm_lie_ground", {"pelvis_back": sit_ground.PELVIS_BACK + lie_ground.LIE_BACK, "ground": 1}))
    # a bed: the marker at the middle of a long side, facing away; the mattress edge `edge` behind it, the body's
    # line `lie_x` behind it, the head to the marker's left
    rows.append(("rpm_lie_bed", {"bed": 1, "edge": -lie_bed.EDGE, "lie_x": -lie_bed.LIE_X, "lie_y": lie_bed.LIE_Y,
                                 "lie_center": round(lie_bed.lie_center(rig, "male"), 2),
                                 "h_min": lie_bed.BED_HEIGHTS[0], "h_max": lie_bed.BED_HEIGHTS[-1]}))
    return rows


def loop_of(clip, seconds):
    """repeat a clip (or hold its first frame) to the given length"""
    n = int(round(seconds * clip.fps)) + 1
    reps = int(np.ceil(n / max(1, clip.frames - 1)))
    c = clip.copy()
    idx = np.concatenate([np.arange(clip.frames - 1)] * reps + [[0]])[:n]
    c.pos = clip.pos[idx]
    c.quat = clip.quat[idx]
    c.loop = True
    return c


def _blended(fn, heights):
    def make(rig, skel, name):
        clips = [fn(rig, skel, h) for h in heights]
        for c in clips:
            c.name = name
        return clips
    return make, {"blend": ("rpm_seat_h", heights[0], heights[-1])}


def _single(fn):
    def make(rig, skel, name):
        c = fn(rig, skel)
        c.name = name
        return c
    return make, {}


def entries(skel):
    """[(sequence name, maker(rig, skel, name) -> clip or clips, Seq options)]"""
    out = []
    humans = skel in ("male", "female", "police", "combine")
    if humans:
        for prefix, mod in (("rpm_sit_chair", sit_chair), ("rpm_sit_couch", sit_couch), ("rpm_lie_bed", lie_bed)):
            H = mod.SEAT_HEIGHTS
            for part, fn, opt in (("enter", mod.enter, {"fadein": 0.25, "fadeout": 0.1}),
                                  ("idle", mod.idle, {"loop": True, "fadein": 0.1, "fadeout": 0.1}),
                                  ("exit", mod.exit_, {"fadein": 0.1, "fadeout": 0.25})):
                make, o = _blended(fn, H)
                o.update(opt)
                out.append(("%s_%s" % (prefix, part), make, o))
        make, o = _blended(eat_chair.idle, eat_chair.SEAT_HEIGHTS)
        o.update({"loop": True, "fadein": 0.1, "fadeout": 0.1})
        out.append(("rpm_eat_chair_idle", make, o))
    if humans:
        # sitting on the ground and leaning on a wall (markers without furniture)
        for prefix, mod in (("rpm_sit_ground", sit_ground), ("rpm_lean_wall", lean_wall), ("rpm_lie_ground", lie_ground)):
            for part, fn, opt in (("enter", mod.enter, {"fadein": 0.25, "fadeout": 0.1}),
                                  ("idle", mod.idle, {"loop": True, "fadein": 0.1, "fadeout": 0.1}),
                                  ("exit", mod.exit_, {"fadein": 0.1, "fadeout": 0.25})):
                if mod is lean_wall:
                    # the back that touches the wall is the target skeleton's (a soldier's backpack)
                    def fn(rig, s, _f=fn, _t=skel):
                        return _f(rig, s, depth=_t)
                make, o = _single(fn)
                o.update(opt)
                out.append(("%s_%s" % (prefix, part), make, o))
    if humans:
        for name, fn, opt in (("rpm_talk_idle", kimodo_clips.talk_idle, {"loop": True, "fadein": 0.3, "fadeout": 0.3}),
                              ("rpm_search_enter", kimodo_clips.search_enter, {"fadein": 0.25, "fadeout": 0.1}),
                              ("rpm_search_idle", kimodo_clips.search_idle, {"loop": True, "fadein": 0.1, "fadeout": 0.1}),
                              ("rpm_search_exit", kimodo_clips.search_exit, {"fadein": 0.1, "fadeout": 0.25})):
            make, o = _single(fn)
            o.update(opt)
            out.append((name, make, o))
    if skel in ("male", "female"):
        # a standing idle made of a Kimodo motion, for NPCs and the player
        for name, act in (("rpm_idle_bored", "ACT_IDLE"), ("rpm_pidle_bored", "ACT_HL2MP_IDLE")):
            make, o = _single(kimodo_clips.idle_bored)
            o.update({"loop": True, "activity": act, "weight": 1, "fadein": 0.4, "fadeout": 0.3})
            out.append((name, make, o))
        # standing idles of unarmed citizens: ACT_IDLE variants next to the stock idle_subtle
        for name, fn in stand_idle.ALL:
            make, o = _single(fn)
            o.update({"loop": True, "activity": "ACT_IDLE", "weight": 1, "fadein": 0.4, "fadeout": 0.3})
            out.append((name, make, o))
            # the same for the player's body in third person (Mapbase's player animation state, unarmed)
            make2, o2 = _single(fn)
            o2.update({"loop": True, "activity": "ACT_HL2MP_IDLE", "weight": 1, "fadein": 0.4, "fadeout": 0.3})
            out.append((name.replace("rpm_idle_", "rpm_pidle_"), make2, o2))
    if humans:
        # fists (weapon_rpm_fists): the guard and the punches
        make, o = _single(fists.stance)
        o.update({"loop": True, "activity": "ACT_RPM_FIST_STANCE", "weight": 1, "fadein": 0.2, "fadeout": 0.2})
        out.append(("rpm_fist_stance", make, o))
        for name, fn, w in (("rpm_fist_jab", fists.jab, 3), ("rpm_fist_cross", fists.cross, 2),
                            ("rpm_fist_hook", fists.hook, 2), ("rpm_fist_uppercut", fists.uppercut, 1)):
            make, o = _single(fn)
            o.update({"activity": "ACT_RPM_FIST_PUNCH", "weight": w, "fadein": 0.08, "fadeout": 0.15})
            out.append((name, make, o))
        for name, fn, act in (("rpm_fist_flinch", fists.flinch, "ACT_RPM_FIST_FLINCH"), ("rpm_fist_shove", fists.shove, "ACT_RPM_FIST_SHOVE")):
            make, o = _single(fn)
            o.update({"activity": act, "weight": 1, "fadein": 0.08, "fadeout": 0.15})
            out.append((name, make, o))
    return out


# skeletons whose clips are made on the citizen's (the same bones and reference pose) and copied over
BORROW = {"police": "male", "combine": "male"}


def build(rig, skel, only=None):
    from . import rig as R
    src_skel = BORROW.get(skel, skel)
    src_rig = rig if src_skel == skel else R.Rig.from_model(B.SKELETONS[src_skel]["rig"])
    sit_chair.configure(src_rig, src_skel)
    seqs = []
    for name, make, opt in entries(skel):
        if only is not None and name not in only:
            seqs.append(B.Cached(name))
            continue
        clips = make(src_rig, src_skel, name)
        if src_rig is not rig:
            clips = [R.remap(c, rig) for c in clips] if isinstance(clips, list) else R.remap(clips, rig)
        seqs.append(B.Seq(name, clips, **opt))
    return seqs
