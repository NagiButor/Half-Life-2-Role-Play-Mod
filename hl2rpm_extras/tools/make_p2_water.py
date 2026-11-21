"""Port all distinct Portal 2 water & Aperture toxic goo materials into HL2RPM,
adapted for the HL2RPM deferred renderer (flowing water $flowmap + $flow_colortexture
in stdshaders/water.cpp & SDK_water_ps2x.fxc), and build the showcase/test map p2_water_test.

Usage:
    python make_p2_water.py [--no-map] [--no-compile]
"""
import argparse
import os
import struct
import subprocess
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
DLG_DIR = os.path.join(TOOLS_DIR, "dialog_editor")
HAMMER_SCRIPTS = os.path.normpath(os.path.join(TOOLS_DIR, "..", "..", "AI_instructions", "skills", "hl2rpm-hammer", "scripts"))
for p in (TOOLS_DIR, DLG_DIR, HAMMER_SCRIPTS):
    if p not in sys.path:
        sys.path.insert(0, p)

from dlgedit.world import vpk  # noqa: E402
from csgo_weapons import csgo_core as C  # noqa: E402

MOD = r"E:\Steam\steamapps\sourcemods\hl2rpm"
PORTAL2_ROOT = r"E:\Steam\steamapps\common\Portal 2"
MAPSRC_TEST = r"E:\XBLAH's modding tool\content\Mapbase\hl2rpm\mapsrc\test"
COMPILE_PS1 = os.path.normpath(os.path.join(HAMMER_SCRIPTS, "compile_map.ps1"))

FONT_BOLD = r"C:\Windows\Fonts\arialbd.ttf"
FONT_REG = r"C:\Windows\Fonts\arial.ttf"

TEX_REL = "hl2rpm/water/p2_tex"
SIGN_REL = "hl2rpm/water/p2_signs"
WATER_REL = "hl2rpm/water"


# ------------------------------------------------------------------ VPK & VTF 7.5 -> 7.4 (lossless!)
def open_p2_vpks():
    paks = []
    for sub in ("update", "portal2_dlc2", "portal2_dlc1", "portal2"):
        p = os.path.join(PORTAL2_ROOT, sub, "pak01_dir.vpk")
        if os.path.exists(p):
            paks.append(vpk.VPK(p))
    if not paks:
        raise FileNotFoundError("No Portal 2 VPK archives found in " + PORTAL2_ROOT)
    return paks


def read_p2(paks, rel):
    for p in paks:
        d = p.read(rel)
        if d is not None:
            return d
    raise FileNotFoundError("Missing in Portal 2 VPKs: " + rel)


def fix_vtf_75_to_74(data):
    """Lossless VTF 7.5 -> 7.4 downgrade for Source SDK 2013 (identical binary layout)."""
    if data[:4] == b"VTF\0" and struct.unpack_from("<II", data, 4) == (7, 5):
        return data[:8] + struct.pack("<I", 4) + data[12:]
    return data


P2_TEXTURES = (
    ("materials/liquids/water_river_normal_sharp.vtf", "p2_river_normal_sharp.vtf"),
    ("materials/liquids/toxic_rep_flowmapr.vtf", "p2_toxic_flowmap.vtf"),
    ("materials/liquids/c3m2_hflowmap_a.vtf", "p2_swamp_flowmap.vtf"),
    ("materials/liquids/sp_a2_bridge_intro_hflowmap.vtf", "p2_chamber_flowmap.vtf"),
    ("materials/liquids/water_noise.vtf", "p2_water_noise.vtf"),
    ("materials/liquids/water_noise2.vtf", "p2_water_noise2.vtf"),
    ("materials/liquids/toxicslime_color.vtf", "p2_toxicslime_color.vtf"),
    ("materials/nature/water_frame01_normal.vtf", "p2_tool_water.vtf"),
)


# ------------------------------------------------------------------ All 8 Distinct Portal 2 Water Types + 2 Beneath
P2_WATERS = [
    dict(
        name="p2_aperture_goo",
        title="1. P2 APERTURE GOO",
        subtitle="orig: nature/toxicslime002a",
        desc="Dense flowing toxic goo | Vortex flowmap + matte scum | RT Reflect",
        surfaceprop="slime",
        bottom="p2_goo_beneath",
        normalmap=f"{TEX_REL}/p2_river_normal_sharp",
        flowmap=f"{TEX_REL}/p2_toxic_flowmap",
        noise=f"{TEX_REL}/p2_water_noise",
        colortex=f"{TEX_REL}/p2_toxicslime_color",
        flow_worlduvscale=4.0,
        flow_normaluvscale=160.0,
        flow_interval=0.63,
        flow_scroll=0.12,
        flow_bump=1.0,
        flow_timescale=1.0,
        flow_noise_scale=0.00082,
        color_uvscale=260.0,
        color_interval=4.0,
        color_scroll=0.14,
        color_lerpexp=0.98,
        color_displace=0.014,
        color_intensity=1.05,
        color_opacity=0.94,
        color_tint="{255 244 226}",
        refract=False,
        refractamount=0.0,
        refracttint="{151 135 34}",
        reflectamount=0.18,
        reflecttint="{195 210 208}",
        fogcolor="{58 35 20}",
        fogstart=0,
        fogend=100,
        water_absorb="[0.85 1.0 1.45 2.0]",
        water_glow=0.0,
    ),
    dict(
        name="p2_overgrown_slime",
        title="2. P2 OVERGROWN SLIME",
        subtitle="orig: toxicslime_a1_intro5 / a2_laser_over_goo",
        desc="Slow mossy chamber slime | Yellow-green overgrown tint | Broad drift",
        surfaceprop="slime",
        bottom="p2_goo_beneath",
        normalmap=f"{TEX_REL}/p2_river_normal_sharp",
        flowmap=f"{TEX_REL}/p2_chamber_flowmap",
        noise=f"{TEX_REL}/p2_water_noise",
        colortex=f"{TEX_REL}/p2_toxicslime_color",
        flow_worlduvscale=0.55,
        flow_normaluvscale=160.0,
        flow_interval=0.60,
        flow_scroll=0.10,
        flow_bump=1.0,
        flow_timescale=0.85,
        flow_noise_scale=0.0010,
        color_uvscale=340.0,
        color_interval=5.0,
        color_scroll=0.11,
        color_lerpexp=1.15,
        color_displace=0.010,
        color_intensity=1.08,
        color_opacity=0.92,
        color_tint="{198 238 158}",
        refract=False,
        refractamount=0.0,
        refracttint="{151 135 34}",
        reflectamount=0.15,
        reflecttint="{232 240 194}",
        fogcolor="{54 64 24}",
        fogstart=0,
        fogend=100,
        water_absorb="[0.90 0.90 1.55 1.9]",
        water_glow=0.0,
    ),
    dict(
        name="p2_old_aperture_slime",
        title="3. P2 OLD APERTURE SLIME",
        subtitle="orig: toxicslime_a3_03 / a3_bomb_flings",
        desc="Deep A3 shaft sludge | Dark rust-oil fine foam | Wide slow current",
        surfaceprop="slime",
        bottom="p2_goo_beneath",
        normalmap=f"{TEX_REL}/p2_river_normal_sharp",
        flowmap=f"{TEX_REL}/p2_toxic_flowmap",
        noise=f"{TEX_REL}/p2_water_noise",
        colortex=f"{TEX_REL}/p2_toxicslime_color",
        flow_worlduvscale=0.40,
        flow_normaluvscale=160.0,
        flow_interval=0.45,
        flow_scroll=0.12,
        flow_bump=1.0,
        flow_timescale=0.95,
        flow_noise_scale=0.0009,
        color_uvscale=175.0,
        color_interval=3.6,
        color_scroll=0.15,
        color_lerpexp=0.98,
        color_displace=0.018,
        color_intensity=0.84,
        color_opacity=0.96,
        color_tint="{188 152 132}",
        refract=False,
        refractamount=0.0,
        refracttint="{130 110 34}",
        reflectamount=0.22,
        reflecttint="{185 198 198}",
        fogcolor="{38 24 14}",
        fogstart=0,
        fogend=90,
        water_absorb="[0.85 1.05 1.55 2.2]",
        water_glow=0.0,
    ),
    dict(
        name="p2_underground_cave",
        title="4. P2 UNDERGROUND CAVE",
        subtitle="orig: liquids/water_underground_cave",
        desc="Semi-translucent cave sludge | Refract shallow edge + flowing goo",
        surfaceprop="slime",
        bottom="p2_goo_beneath",
        normalmap=f"{TEX_REL}/p2_river_normal_sharp",
        flowmap=f"{TEX_REL}/p2_toxic_flowmap",
        noise=f"{TEX_REL}/p2_water_noise",
        colortex=f"{TEX_REL}/p2_toxicslime_color",
        flow_worlduvscale=1.5,
        flow_normaluvscale=160.0,
        flow_interval=0.45,
        flow_scroll=0.11,
        flow_bump=1.0,
        flow_timescale=1.0,
        flow_noise_scale=0.0010,
        color_uvscale=260.0,
        color_interval=3.8,
        color_scroll=0.18,
        color_lerpexp=0.98,
        color_displace=0.030,
        color_intensity=1.02,
        color_opacity=0.58,
        color_tint="{220 202 178}",
        refract=True,
        refractamount=0.30,
        refracttint="{175 158 110}",
        reflectamount=0.26,
        reflecttint="{190 205 205}",
        fogcolor="{58 35 20}",
        fogstart=0,
        fogend=85,
        water_absorb="[0.85 1.0 1.45 1.45]",
        water_glow=0.0,
    ),
    dict(
        name="p2_swamp_murky",
        title="5. P2 SWAMP MURKY",
        subtitle="orig: liquids/water_swamp_m2",
        desc="Murky amber swamp water | RT Refract + RT Reflect | Gentle flow",
        surfaceprop="water",
        bottom="p2_swamp_beneath",
        normalmap=f"{TEX_REL}/p2_river_normal_sharp",
        flowmap=f"{TEX_REL}/p2_swamp_flowmap",
        noise=f"{TEX_REL}/p2_water_noise",
        colortex=None,
        flow_worlduvscale=0.65,
        flow_normaluvscale=155.0,
        flow_interval=0.28,
        flow_scroll=0.12,
        flow_bump=0.50,
        flow_timescale=0.45,
        flow_noise_scale=0.0005,
        refract=True,
        refractamount=0.32,
        refracttint="{190 176 112}",
        reflectamount=0.40,
        reflecttint="{195 195 190}",
        fogcolor="{88 68 20}",
        fogstart=0,
        fogend=62,
        water_absorb="[0.82 1.0 1.45 1.35]",
        water_glow=0.0,
    ),
    dict(
        name="p2_bts_industrial",
        title="6. P2 BTS INDUSTRIAL",
        subtitle="orig: water_bts_01 / a1_intro3 / a2_laser_intro",
        desc="Flooded behind-the-scenes water | Cool olive-grey | Crisp mirrors",
        surfaceprop="water",
        bottom="p2_swamp_beneath",
        normalmap=f"{TEX_REL}/p2_river_normal_sharp",
        flowmap=f"{TEX_REL}/p2_toxic_flowmap",
        noise=f"{TEX_REL}/p2_water_noise",
        colortex=None,
        flow_worlduvscale=1.1,
        flow_normaluvscale=155.0,
        flow_interval=0.30,
        flow_scroll=0.10,
        flow_bump=0.46,
        flow_timescale=0.40,
        flow_noise_scale=0.0005,
        refract=True,
        refractamount=0.35,
        refracttint="{175 186 168}",
        reflectamount=0.45,
        reflecttint="{198 210 208}",
        fogcolor="{54 70 56}",
        fogstart=0,
        fogend=78,
        water_absorb="[1.15 1.0 1.20 1.15]",
        water_glow=0.0,
    ),
    dict(
        name="p2_glados_chamber",
        title="7. P2 GLADOS CHAMBER",
        subtitle="orig: nature/water_glados_01",
        desc="Shallow flooded chamber water | Strong refract (0.52) | Clear rim",
        surfaceprop="water",
        bottom="p2_swamp_beneath",
        normalmap=f"{TEX_REL}/p2_river_normal_sharp",
        flowmap=f"{TEX_REL}/p2_swamp_flowmap",
        noise=f"{TEX_REL}/p2_water_noise",
        colortex=None,
        flow_worlduvscale=0.50,
        flow_normaluvscale=155.0,
        flow_interval=0.25,
        flow_scroll=0.11,
        flow_bump=0.42,
        flow_timescale=0.40,
        flow_noise_scale=0.0005,
        refract=True,
        refractamount=0.52,
        refracttint="{188 182 130}",
        reflectamount=0.40,
        reflecttint="{198 206 208}",
        fogcolor="{68 78 58}",
        fogstart=0,
        fogend=42,
        water_absorb="[1.05 1.0 1.25 1.10]",
        water_glow=0.0,
    ),
    dict(
        name="p2_bottomless_pit",
        title="8. P2 BOTTOMLESS PIT",
        subtitle="orig: nature/water_bottomless_pit_01",
        desc="Turbulent abyss water | Tight vortices (scale 8) | Noise2 + Bump 1",
        surfaceprop="water",
        bottom="p2_swamp_beneath",
        normalmap=f"{TEX_REL}/p2_river_normal_sharp",
        flowmap=f"{TEX_REL}/p2_toxic_flowmap",
        noise=f"{TEX_REL}/p2_water_noise2",
        colortex=None,
        flow_worlduvscale=8.0,
        flow_normaluvscale=160.0,
        flow_interval=0.22,
        flow_scroll=0.10,
        flow_bump=1.0,
        flow_timescale=0.42,
        flow_noise_scale=0.0005,
        refract=True,
        refractamount=0.28,
        refracttint="{148 158 152}",
        reflectamount=0.44,
        reflecttint="{195 202 208}",
        fogcolor="{36 46 50}",
        fogstart=0,
        fogend=65,
        water_absorb="[1.25 1.05 0.95 1.40]",
        water_glow=0.0,
    ),
]

P2_BENEATH = [
    dict(
        name="p2_goo_beneath",
        orig="nature/toxicslime002a_beneath",
        surfaceprop="slime",
        fogcolor="{36 22 12}",
        fogend=90,
        refracttint="{151 135 34}",
    ),
    dict(
        name="p2_swamp_beneath",
        orig="liquids/water_swamp_m2_beneath",
        surfaceprop="water",
        fogcolor="{78 64 26}",
        fogend=140,
        refracttint="{175 162 96}",
    ),
]


def export_textures_and_vmts():
    paks = open_p2_vpks()
    tex_dir = os.path.join(MOD, "materials", *TEX_REL.split("/"))
    water_dir = os.path.join(MOD, "materials", *WATER_REL.split("/"))
    os.makedirs(tex_dir, exist_ok=True)
    os.makedirs(water_dir, exist_ok=True)

    # 1. Lossless VTF 7.5 -> 7.4 port of all Portal 2 water textures
    for src_rel, dst_name in P2_TEXTURES:
        raw = read_p2(paks, src_rel)
        fixed = fix_vtf_75_to_74(raw)
        dst = os.path.join(tex_dir, dst_name)
        with open(dst, "wb") as f:
            f.write(fixed)
        v0, v1, hsz, w, h = struct.unpack_from("<IIIHH", fixed, 4)
        print(f"VTF: {dst_name:28s} v{v0}.{v1} {w}x{h} ({len(fixed)} B)")

    # 2. Generate 8 above-water Portal 2 VMTs adapted for HL2RPM deferred Water shader.
    # NOTE: Must use "SDK_Water" (not "Water"): on HDR maps (mat_hdr_level 2) Source 2013
    # maps "Water" to Valve's stock stdshader_dx9.dll Water_DX9_HDR instead of our
    # game_shader_dx9.dll SDK_Water_DX9_HDR (which falls back to SDK_Water_DX90 on LDR).
    for w in P2_WATERS:
        lines = [
            '"SDK_Water"',
            "{",
            f'\t// HL2RPM Portal 2 water port ({w["subtitle"]})',
            '\t"%compilewater" 1',
            f'\t"%tooltexture" "{TEX_REL}/p2_tool_water"',
            '\t"$abovewater" 1',
            '\t"$forceexpensive" 1',
            '\t"$envmap" "env_cubemap"',
            f'\t"$normalmap" "{w["normalmap"]}"',
            f'\t"$flowmap" "{w["flowmap"]}"',
            f'\t"$flow_worlduvscale" "{w["flow_worlduvscale"]}"',
            f'\t"$flow_normaluvscale" "{w["flow_normaluvscale"]}"',
            f'\t"$flow_timeintervalinseconds" "{w["flow_interval"]}"',
            f'\t"$flow_uvscrolldistance" "{w["flow_scroll"]}"',
            f'\t"$flow_bumpstrength" "{w["flow_bump"]}"',
            f'\t"$flow_timescale" "{w["flow_timescale"]}"',
            f'\t"$flow_noise_texture" "{w["noise"]}"',
            f'\t"$flow_noise_scale" "{w["flow_noise_scale"]}"',
        ]
        if w.get("colortex"):
            lines += [
                f'\t"$flow_colortexture" "{w["colortex"]}"',
                f'\t"$color_flow_uvscale" "{w["color_uvscale"]}"',
                f'\t"$color_flow_timeintervalinseconds" "{w["color_interval"]}"',
                f'\t"$color_flow_uvscrolldistance" "{w["color_scroll"]}"',
                f'\t"$color_flow_lerpexp" "{w["color_lerpexp"]}"',
                f'\t"$color_flow_displacebynormalstrength" "{w["color_displace"]}"',
                f'\t"$flow_color_intensity" "{w["color_intensity"]}"',
                f'\t"$flow_color_opacity" "{w["color_opacity"]}"',
                f'\t"$flow_color_tint" "{w.get("color_tint", "[1 1 1]")}"',
            ]
        lines += [
            '\t"$reflecttexture" "_rt_WaterReflection"',
            '\t"$reflectentities" 1',
            f'\t"$reflectamount" "{w["reflectamount"]}"',
            f'\t"$reflecttint" "{w["reflecttint"]}"',
        ]
        if w["refract"]:
            lines += [
                '\t"$refracttexture" "_rt_WaterRefraction"',
                f'\t"$refractamount" "{w["refractamount"]}"',
                f'\t"$refracttint" "{w["refracttint"]}"',
            ]
        lines += [
            f'\t"$bottommaterial" "{WATER_REL}/{w["bottom"]}"',
            f'\t"$surfaceprop" "{w["surfaceprop"]}"',
            '\t"$fogenable" 1',
            f'\t"$fogstart" "{w["fogstart"]}"',
            f'\t"$fogend" "{w["fogend"]}"',
            f'\t"$fogcolor" "{w["fogcolor"]}"',
            f'\t"$water_absorb" "{w["water_absorb"]}"',
            f'\t"$water_glow" "{w["water_glow"]}"',
            '\t"$scale" "[1 1]"',
            '\t"Proxies"',
            "\t{",
            '\t\t"WaterLOD"',
            "\t\t{",
            "\t\t}",
            "\t}",
            "}",
            "",
        ]
        path = os.path.join(water_dir, w["name"] + ".vmt")
        with open(path, "w", encoding="ascii", newline="\n") as f:
            f.write("\n".join(lines))
        print("VMT:", path)

    # 3. Generate 2 beneath-water VMTs (also "SDK_Water" for HDR/LDR consistency)
    for b in P2_BENEATH:
        lines = [
            '"SDK_Water"',
            "{",
            f'\t// HL2RPM Portal 2 underwater ({b["orig"]})',
            '\t"%compilewater" 1',
            f'\t"%tooltexture" "{TEX_REL}/p2_tool_water"',
            '\t"$abovewater" 0',
            '\t"$forceexpensive" 1',
            f'\t"$normalmap" "{TEX_REL}/p2_river_normal_sharp"',
            f'\t"$flowmap" "{TEX_REL}/p2_swamp_flowmap"',
            '\t"$flow_worlduvscale" "0.5"',
            '\t"$flow_normaluvscale" "155.0"',
            '\t"$flow_timeintervalinseconds" "0.25"',
            '\t"$flow_uvscrolldistance" "0.09"',
            '\t"$flow_bumpstrength" "0.4"',
            '\t"$flow_timescale" "0.35"',
            f'\t"$flow_noise_texture" "{TEX_REL}/p2_water_noise"',
            '\t"$flow_noise_scale" "0.0005"',
            '\t"$refracttexture" "_rt_WaterRefraction"',
            '\t"$refractamount" "0.25"',
            f'\t"$refracttint" "{b["refracttint"]}"',
            f'\t"$surfaceprop" "{b["surfaceprop"]}"',
            '\t"$fogenable" 1',
            '\t"$fogstart" "0"',
            f'\t"$fogend" "{b["fogend"]}"',
            f'\t"$fogcolor" "{b["fogcolor"]}"',
            '\t"$water_glow" "0"',
            "}",
            "",
        ]
        path = os.path.join(water_dir, b["name"] + ".vmt")
        with open(path, "w", encoding="ascii", newline="\n") as f:
            f.write("\n".join(lines))
        print("VMT:", path)


def generate_pool_signs():
    """Create crisp LightmappedGeneric info plaque textures for each pool on p2_water_test."""
    sign_dir = os.path.join(MOD, "materials", *SIGN_REL.split("/"))
    os.makedirs(sign_dir, exist_ok=True)
    f_title = ImageFont.truetype(FONT_BOLD, 44)
    f_mat = ImageFont.truetype(FONT_BOLD, 28)
    f_sub = ImageFont.truetype(FONT_REG, 22)

    for w in P2_WATERS:
        im = Image.new("RGB", (1024, 256), (26, 30, 36))
        dr = ImageDraw.Draw(im)
        # Aperture-inspired amber/cyan top accent bar (amber for goo, teal-blue for translucent water)
        accent = (224, 152, 46) if w.get("colortex") else (66, 178, 214)
        dr.rectangle((0, 0, 1024, 18), fill=accent)
        dr.rectangle((0, 244, 1024, 256), fill=(48, 54, 62))
        dr.rectangle((14, 28, 1010, 234), outline=(72, 80, 92), width=3)

        dr.text((38, 42), w["title"], fill=(242, 246, 250), font=f_title)
        dr.text((38, 104), f'{WATER_REL}/{w["name"]}', fill=accent, font=f_mat)
        dr.text((38, 148), w["subtitle"], fill=(178, 188, 198), font=f_sub)
        dr.text((38, 184), w["desc"], fill=(208, 214, 220), font=f_sub)

        rgb = np.asarray(im, dtype=np.uint8)
        vtf_path = os.path.join(sign_dir, f"sign_{w['name']}.vtf")
        vmt_path = os.path.join(sign_dir, f"sign_{w['name']}.vmt")
        C.write_albedo_vtf(vtf_path, rgb)
        with open(vmt_path, "w", encoding="ascii", newline="\n") as f:
            f.write(
                '"LightmappedGeneric"\n{\n'
                f'\t"$basetexture" "{SIGN_REL}/sign_{w["name"]}"\n'
                '\t"$surfaceprop" "metal"\n'
                '\t"$selfillum" 1\n'
                '\t"$selfillumtint" "[0.48 0.48 0.48]"\n'
                '}\n'
            )
    print(f"Generated {len(P2_WATERS)} pool info plaques in {sign_dir}")


# ------------------------------------------------------------------ Showcase Map Generator: p2_water_test.vmf
class VMFWriter:
    def __init__(self, skyname="sky_day01_01"):
        self.next_id = 1
        self.world_solids = []
        self.entities = []
        self.skyname = skyname

    def _id(self):
        i = self.next_id
        self.next_id += 1
        return i

    def _side_block(self, plane, mat, uaxis, vaxis, lms=16):
        (p1, p2, p3) = plane
        return (
            "\t\tside\n\t\t{\n"
            f'\t\t\t"id" "{self._id()}"\n'
            f'\t\t\t"plane" "({p1[0]:g} {p1[1]:g} {p1[2]:g}) ({p2[0]:g} {p2[1]:g} {p2[2]:g}) ({p3[0]:g} {p3[1]:g} {p3[2]:g})"\n'
            f'\t\t\t"material" "{mat}"\n'
            f'\t\t\t"uaxis" "{uaxis}"\n'
            f'\t\t\t"vaxis" "{vaxis}"\n'
            '\t\t\t"rotation" "0"\n'
            f'\t\t\t"lightmapscale" "{lms}"\n'
            '\t\t\t"smoothing_groups" "0"\n'
            "\t\t}"
        )

    def make_box(self, x1, y1, z1, x2, y2, z2, mat, top=None, bottom=None, north=None, south=None, east=None, west=None,
                 scale=0.25, custom_uv=None, lms=16):
        """Build a strictly valid axis-aligned Source VMF solid (outward-facing planes)."""
        if x1 > x2: x1, x2 = x2, x1
        if y1 > y2: y1, y2 = y2, y1
        if z1 > z2: z1, z2 = z2, z1
        custom_uv = custom_uv or {}
        s = scale
        faces = [
            # top (+Z)
            (((x1, y2, z2), (x2, y2, z2), (x2, y1, z2)), top or mat, f"[1 0 0 0] {s}", f"[0 -1 0 0] {s}", "top"),
            # bottom (-Z)
            (((x1, y1, z1), (x2, y1, z1), (x2, y2, z1)), bottom or mat, f"[1 0 0 0] {s}", f"[0 -1 0 0] {s}", "bottom"),
            # west (-X)
            (((x1, y2, z2), (x1, y1, z2), (x1, y1, z1)), west or mat, f"[0 1 0 0] {s}", f"[0 0 -1 0] {s}", "west"),
            # east (+X)
            (((x2, y2, z1), (x2, y1, z1), (x2, y1, z2)), east or mat, f"[0 1 0 0] {s}", f"[0 0 -1 0] {s}", "east"),
            # north (+Y)
            (((x2, y2, z2), (x1, y2, z2), (x1, y2, z1)), north or mat, f"[1 0 0 0] {s}", f"[0 0 -1 0] {s}", "north"),
            # south (-Y)
            (((x2, y1, z1), (x1, y1, z1), (x1, y1, z2)), south or mat, f"[1 0 0 0] {s}", f"[0 0 -1 0] {s}", "south"),
        ]
        sid = self._id()
        sblocks = []
        for pl, m, ua, va, key in faces:
            if key in custom_uv:
                ua, va = custom_uv[key]
            sblocks.append(self._side_block(pl, m, ua, va, lms=lms))
        return "\tsolid\n\t{\n" + f'\t\t"id" "{sid}"\n' + "\n".join(sblocks) + "\n\t}"

    def add_world_box(self, *args, **kwargs):
        self.world_solids.append(self.make_box(*args, **kwargs))

    def add_detail_box(self, *args, **kwargs):
        sol = self.make_box(*args, **kwargs)
        self.add_brush_entity("func_detail", [sol])

    def add_brush_entity(self, classname, solids, **kv):
        eid = self._id()
        lines = ["entity", "{", f'\t"id" "{eid}"', f'\t"classname" "{classname}"']
        for k, v in kv.items():
            lines.append(f'\t"{k}" "{v}"')
        lines += solids
        lines.append("}")
        self.entities.append("\n".join(lines))

    def add_entity(self, classname, origin, **kv):
        eid = self._id()
        lines = [
            "entity",
            "{",
            f'\t"id" "{eid}"',
            f'\t"classname" "{classname}"',
            f'\t"origin" "{origin[0]:g} {origin[1]:g} {origin[2]:g}"',
        ]
        for k, v in kv.items():
            lines.append(f'\t"{k}" "{v}"')
        lines.append("}")
        self.entities.append("\n".join(lines))

    def write(self, path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        out = [
            'versioninfo\n{\n\t"editorversion" "400"\n\t"editorbuild" "8864"\n\t"mapversion" "1"\n\t"formatversion" "100"\n\t"prefab" "0"\n}',
            'visgroups\n{\n}',
            'viewsettings\n{\n\t"bSnapToGrid" "1"\n\t"bShowGrid" "1"\n\t"bShowLogicalGrid" "0"\n\t"nGridSpacing" "16"\n\t"bShow3DGrid" "0"\n}',
            "world\n{\n"
            f'\t"id" "{self._id()}"\n'
            '\t"mapversion" "1"\n'
            '\t"classname" "worldspawn"\n'
            '\t"detailmaterial" "detail/detailsprites"\n'
            '\t"detailvbsp" "detail.vbsp"\n'
            '\t"maxpropscreenwidth" "-1"\n'
            f'\t"skyname" "{self.skyname}"\n'
            + "\n".join(self.world_solids)
            + "\n}",
        ]
        out += self.entities
        out.append('cameras\n{\n\t"activecamera" "-1"\n}\ncordon\n{\n\t"mins" "(-1024 -1024 -1024)"\n\t"maxs" "(1024 1024 1024)"\n\t"active" "0"\n}\n')
        with open(path, "w", encoding="ascii", newline="\n") as f:
            f.write("\n".join(out))
        print(f"Wrote VMF: {path} ({len(self.world_solids)} world solids, {len(self.entities)} entities)")


def build_test_map():
    """Build p2_water_test.vmf: 2 rows x 4 pools (all 8 Portal 2 water types), shared waterline Z=0,
    walkable steps into every pool (depth 0..-112), submerged & emergent concrete pillars, overhead
    half-canopy with warm deferred spotlights (sun + shadow + night lamp testing on every single water!),
    and clear Aperture-styled info plaques above each pool."""
    vmf = VMFWriter(skyname="sky_day01_01")

    MAT_DECK = "CONCRETE/CONCRETEFLOOR028A"
    MAT_WALL = "CONCRETE/CONCRETEWALL075A"
    MAT_POOL_FLOOR = "DEV/DEV_MEASUREGENERIC01B"
    MAT_POOL_WALL = "CONCRETE/CONCRETEFLOOR009A"
    MAT_PILLAR = "METAL/METALWALL014A"
    MAT_ROOF = "CONCRETE/CONCRETECEILING003A"
    MAT_NODRAW = "TOOLS/TOOLSNODRAW"
    MAT_SKY = "TOOLS/TOOLSSKYBOX"

    # Layout:
    # Central promenade along X (Y = -160 .. +160, Z = +16)
    # Row North (pools 0..3): Y = +160 .. +672 (len 512), X pitch 512 (pool 384 wide + 128 piers)
    # Row South (pools 4..7): Y = -672 .. -160 (len 512), X pitch 512 (mirrored across central promenade!)
    # Total X: -128 .. 2048 (4 columns: X_pool = [0..384], [512..896], [1024..1408], [1536..1920])
    # Total Y: -832 .. +832
    X_MIN, X_MAX = -192, 2112
    Y_MIN, Y_MAX = -864, 864
    Z_FLOOR = -160   # solid basement slab beneath all pools
    Z_WATER = 0      # shared waterline Z=0 across ALL 8 pools (exact planar reflection match!)
    Z_DECK = 16      # walkways 16 HU above waterline
    Z_WALL_TOP = 320
    Z_SKY = 896

    # 1. Sealed outer world shell (skybox ceiling + perimeter walls + deep basement floor)
    vmf.add_world_box(X_MIN - 64, Y_MIN - 64, Z_FLOOR - 64, X_MAX + 64, Y_MAX + 64, Z_FLOOR, MAT_DECK)
    vmf.add_world_box(X_MIN - 64, Y_MIN - 64, Z_SKY, X_MAX + 64, Y_MAX + 64, Z_SKY + 64, MAT_SKY)
    # Outer perimeter lower concrete walls (Z_FLOOR .. Z_WALL_TOP) + upper skybox walls (Z_WALL_TOP .. Z_SKY)
    for (x1, y1, x2, y2) in (
        (X_MIN - 64, Y_MIN - 64, X_MIN, Y_MAX + 64),
        (X_MAX, Y_MIN - 64, X_MAX + 64, Y_MAX + 64),
        (X_MIN, Y_MIN - 64, X_MAX, Y_MIN),
        (X_MIN, Y_MAX, X_MAX, Y_MAX + 64),
    ):
        vmf.add_world_box(x1, y1, Z_FLOOR, x2, y2, Z_WALL_TOP, MAT_WALL)
        vmf.add_world_box(x1, y1, Z_WALL_TOP, x2, y2, Z_SKY, MAT_SKY)

    # 2. Central promenade deck (Y = -160 .. +160, Z = Z_FLOOR .. Z_DECK) + outer walkways along Y_MIN/Y_MAX
    vmf.add_world_box(X_MIN, -160, Z_FLOOR, X_MAX, 160, Z_DECK, MAT_DECK)
    vmf.add_world_box(X_MIN, Y_MIN, Z_FLOOR, X_MAX, -672, Z_DECK, MAT_DECK)
    vmf.add_world_box(X_MIN, 672, Z_FLOOR, X_MAX, Y_MAX, Z_DECK, MAT_DECK)
    # End decks left & right of pools
    vmf.add_world_box(X_MIN, -672, Z_FLOOR, 0, -160, Z_DECK, MAT_DECK)
    vmf.add_world_box(X_MIN, 160, Z_FLOOR, 0, 672, Z_DECK, MAT_DECK)
    vmf.add_world_box(1920, -672, Z_FLOOR, X_MAX, -160, Z_DECK, MAT_DECK)
    vmf.add_world_box(1920, 160, Z_FLOOR, X_MAX, 672, Z_DECK, MAT_DECK)

    # Dividing piers between the 4 columns (128 HU wide between pools)
    for col in range(3):
        px1 = col * 512 + 384
        px2 = px1 + 128
        vmf.add_world_box(px1, 160, Z_FLOOR, px2, 672, Z_DECK, MAT_DECK)
        vmf.add_world_box(px1, -672, Z_FLOOR, px2, -160, Z_DECK, MAT_DECK)

    # 3. Half-canopy shade gallery over the back half of each row (lets user inspect sun + shadow + lamp on EVERY water!)
    # North canopy: Y = 464 .. 864, Z = 224 .. 256 (supported by columns on the piers)
    # South canopy: Y = -864 .. -464, Z = 224 .. 256
    vmf.add_world_box(X_MIN, 464, 224, X_MAX, Y_MAX, 256, MAT_ROOF)
    vmf.add_world_box(X_MIN, Y_MIN, 224, X_MAX, -464, 256, MAT_ROOF)
    for cx in (-64, 448, 960, 1472, 1984):
        vmf.add_detail_box(cx - 16, 480, Z_DECK, cx + 16, 512, 224, MAT_PILLAR)
        vmf.add_detail_box(cx - 16, -512, Z_DECK, cx + 16, -480, 224, MAT_PILLAR)

    # 4. Build all 8 pools (0..3 North row facing South; 4..7 South row facing North)
    for idx, w in enumerate(P2_WATERS):
        is_north = idx < 4
        col = idx if is_north else (idx - 4)
        x1 = col * 512
        x2 = x1 + 384
        xc = (x1 + x2) // 2
        if is_north:
            y_front, y_back = 160, 672
            y_dir = 1
            sign_y1, sign_y2 = 688, 704
            sign_face = "south"
        else:
            y_front, y_back = -160, -672
            y_dir = -1
            sign_y1, sign_y2 = -704, -688
            sign_face = "north"

        ya, yb = min(y_front, y_back), max(y_front, y_back)

        # Pool basin floor at Z = -112 (bright grid texture so refraction & depth gradient are super clear!)
        vmf.add_world_box(x1, ya, Z_FLOOR, x2, yb, -112, MAT_POOL_FLOOR)

        # Gradual walkable steps descending from Z_DECK (+16) down to Z=-112 along the front end
        # (8 steps of 16 HU drop x 24 HU tread = 192 HU slope: lets player see exact 0..112 HU water depth & wade/dive!)
        for st in range(7):
            z_top = Z_DECK - (st + 1) * 16   # 0, -16, -32, -48, -64, -80, -96
            d0 = st * 24
            d1 = (st + 1) * 24
            sy0 = y_front + y_dir * d0
            sy1 = y_front + y_dir * d1
            sya, syb = min(sy0, sy1), max(sy0, sy1)
            vmf.add_detail_box(x1, sya, -112, x2, syb, z_top, MAT_POOL_WALL, top=MAT_POOL_FLOOR)

        # Two contrasting vertical reference pillars inside each pool (one in sunlit middle, one under shaded canopy)
        # Rising from basin floor (-112) to +96 above water -> crystal-clear refraction & real-time reflection test!
        for px, py in ((x1 + 96, y_front + y_dir * 256), (x2 - 96, y_front + y_dir * 420)):
            vmf.add_detail_box(px - 20, py - 20, -112, px + 20, py + 20, 96, MAT_PILLAR)
        # On refracting pools (4..8), place a bright submerged marker block at Z=-40 to test underwater refraction & depth fog.
        # On opaque goo pools (1..3), skip deep submerged block (avoid LightSampler shadow ghost) and keep emergent metal crate/barrel!
        if w["refract"]:
            vmf.add_detail_box(xc - 32, y_front + y_dir * 270 - 24, -112, xc + 32, y_front + y_dir * 270 + 24, -40, "DEV/DEV_MEASUREWALL01A")
        else:
            vmf.add_detail_box(xc - 24, y_front + y_dir * 320 - 24, -112, xc + 24, y_front + y_dir * 320 + 24, 14, MAT_PILLAR)

        # Water volume brush: STRICTLY top face = hl2rpm/water/<name>, 5 hidden faces = TOOLS/TOOLSNODRAW!
        water_mat = f"{WATER_REL}/{w['name']}"
        vmf.add_world_box(x1, ya, -112, x2, yb, Z_WATER, MAT_NODRAW, top=water_mat, scale=0.25)

        # Crisp Aperture-style info plaque mounted at the back edge of the pool facing the promenade (256x64 HU)
        # Map 1024x256 texture 1:1 onto 256x64 HU face -> texture scale = 0.25!
        sx1, sx2 = xc - 128, xc + 128
        sz1, sz2 = 96, 160
        sign_mat = f"{SIGN_REL}/sign_{w['name']}"
        if sign_face == "south":
            # Looking +Y at south face: u goes +X from sx1 (u=0 at sx1), v goes -Z from sz2 (v=0 at sz2)
            # u_tex = (x * 1 + shift_u) / (1024 * 0.25) -> shift_u = -sx1 / 0.25
            uv = {"south": (f"[1 0 0 {-sx1 * 4:g}] 0.25", f"[0 0 -1 {sz2 * 4:g}] 0.25")}
            vmf.add_detail_box(sx1, sign_y1, sz1, sx2, sign_y2, sz2, MAT_PILLAR, south=sign_mat, custom_uv=uv)
        else:
            # Looking -Y at north face: u goes -X from sx2 (u=0 at sx2, u=1 at sx1), v goes -Z from sz2
            uv = {"north": (f"[-1 0 0 {sx2 * 4:g}] 0.25", f"[0 0 -1 {sz2 * 4:g}] 0.25")}
            vmf.add_detail_box(sx1, sign_y1, sz1, sx2, sign_y2, sz2, MAT_PILLAR, north=sign_mat, custom_uv=uv)
        # Sign support legs down to back walkway
        vmf.add_detail_box(sx1 + 16, sign_y1, Z_DECK, sx1 + 28, sign_y2, sz1, MAT_PILLAR)
        vmf.add_detail_box(sx2 - 28, sign_y1, Z_DECK, sx2 - 16, sign_y2, sz1, MAT_PILLAR)

        # Warm spotlight under the shaded canopy right above each pool's rear half + ambient light
        ly = y_front + y_dir * 420
        vmf.add_entity("light_spot", (xc, ly, 212),
                       _light="255 215 160 760", _cone="74", _inner_cone="44",
                       pitch="-90", angles="-90 0 0", _quadratic_attn="1")
        # Dedicated cubemap over each pool center
        vmf.add_entity("env_cubemap", (xc, y_front + y_dir * 256, 64), cubemapsize="0")

        # Named info_target camera bookmark for easy playtesting
        vmf.add_entity("info_target", (xc, y_front - y_dir * 48, 84), targetname=f"view_{w['name']}")

    # 5. Global lighting, water LOD control, player spawn & test weapons on promenade
    vmf.add_entity("light_environment", (960, 0, 640),
                   _light="255 246 226 650", _ambient="140 165 195 160",
                   angles="0 135 0", pitch="-42", SunSpreadAngle="2")
    vmf.add_entity("water_lod_control", (960, 0, 128), cheapwaterstartdistance="2400", cheapwaterenddistance="3600")
    vmf.add_entity("info_player_start", (-64, 0, Z_DECK + 8), angles="0 0 0")
    vmf.add_entity("env_cubemap", (960, 0, 96), cubemapsize="0")

    # Add a couple of props standing half-in water on the central pools for extra eye candy
    for px, py in ((256, 400), (768, 400), (1280, -400), (1792, -400)):
        vmf.add_entity("prop_static", (px, py, -32),
                       model="models/props_c17/oildrum001.mdl", angles="0 30 0", solid="6")

    vmf_path = os.path.join(MAPSRC_TEST, "p2_water_test.vmf")
    vmf.write(vmf_path)
    return vmf_path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-map", action="store_true")
    ap.add_argument("--no-compile", action="store_true")
    args = ap.parse_args()

    export_textures_and_vmts()
    generate_pool_signs()

    if not args.no_map:
        build_test_map()
        if not args.no_compile:
            cmd = [
                "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                "-File", COMPILE_PS1, "-Map", "p2_water_test", "-Profile", "normal",
            ]
            print("Compiling p2_water_test:", " ".join(cmd))
            rc = subprocess.call(cmd)
            if rc != 0:
                raise SystemExit(rc)


if __name__ == "__main__":
    main()
