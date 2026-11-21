"""Build the HL2RPM HK USP Match (replacing stock HL2 weapon_pistol: v_pistol.mdl + w_pistol.mdl):
- Source mesh & animations: CS:GO legacy v_pist_223 (USP-S without silencer, MDL v49 + .ani)
- ValveBiped c_arms retargeting (works with all 8 HL2RPM suit hand models via uses_hands=1)
- Full HK USP Match conversion geometry (aligned to exact CS:GO USP centerline XC = -0.027):
    1. Extended 6.02" Stainless Match Barrel (11-deg target crown, deep bore, HK O-ring groove)
    2. HK USP Match Barrel Weight / Compensator (brushed satin stainless two-tone, under-rail
       clamping saddle with hex bolts, chamfered Match nose clearing the reciprocating slide,
       twin side compensator slots, muzzle & guide-rod recesses, matte anti-glare top sight rib)
    3. Match Fire Control Group (Спортивный УСМ):
       - Match Competition Trigger on Trigger bone (curved lightened blade + stainless overtravel stop screw)
       - Match Skeletonized Hammer on Hammer bone (true 3D hollowed oval cutout + serrated thumb spur)
    4. Adjustable HK Match Target Sights on Slide bone (reciprocates with slide):
       - Elevating target rear sight with true open square notch, top elevation micrometer screw,
         right-side windage micrometer screw, serrated anti-glare rear face & twin aiming dots
       - Tall undercut Match front target blade with bright contrast aiming dot
- Slide engraving updated from 'USP TACTICAL / CAL .223' -> 'USP MATCH / 9mm x 19'
- Deferred PBR textures (VTF v7.2 DXT1 albedo + DXT5 DX normal map with PBR gloss in alpha)
- CS:GO unsilenced USP sounds (mono 3D gunfire + synchronized handling foley) + scripts/weapon_pistol.txt
"""
import math
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

from . import csgo_core as C

SRC_DIR = os.path.join(C.SRC_ROOT, "usp_match")
MAT_DIR_REL = "models/weapons/hl2rpm/usp_match"
SND_DIR_REL = "weapons/hl2rpm_usp"

FONT_BOLD = r"C:\Windows\Fonts\arialbd.ttf"
FONT_REG = r"C:\Windows\Fonts\arial.ttf"

# Exact measured centerline of CS:GO v_pist_223 in v_weapon.223_parent local space
XC = -0.027
BORE_Y = 2.995


# ------------------------------------------------------------------ USP Match Hardware Geometry
def build_usp_match_parts(mb):
    """Build all 5 HK USP Match hardware upgrades in unscaled 223_parent local space."""
    ST = "usp_match_steel"    # Brushed satin stainless steel (barrel weight, extended barrel, hammer, screws)
    SG = "usp_match_sights"   # Matte blued/anodized competition steel (adjustable sights, trigger, dark recesses)

    # UV regions in 1024x1024 usp_match_steel atlas:
    UV_BRUSH = (0.04, 0.04, 0.46, 0.46)     # fine longitudinal brushed satin stainless (compensator body)
    UV_BARREL = (0.54, 0.04, 0.96, 0.46)    # bright machined stainless (extended Match barrel & crown)
    UV_SLOT = (0.06, 0.56, 0.44, 0.94)      # darkened machined recess / compensator interior / grooves
    UV_SCREW = (0.56, 0.56, 0.94, 0.94)     # bright machined hex bolts, micrometer screws & hammer spur

    # UV regions in 512x512 usp_match_sights atlas:
    UV_MATTE = (0.06, 0.06, 0.46, 0.46)     # matte blued competition steel (sight body, trigger body, top rib)
    UV_SERR = (0.54, 0.06, 0.94, 0.46)      # fine anti-glare horizontal serrations (rear sight face / top rib)
    UV_DOT = (0.08, 0.58, 0.42, 0.92)       # bright crisp white/tritium target aiming dot
    UV_BORE = (0.58, 0.58, 0.92, 0.92)      # pitch-black bore / recessed dark slot

    # ==================================================================
    # 1. EXTENDED 6.02" STAINLESS MATCH BARREL (bone: usp_parent)
    # ==================================================================
    # Main extended barrel protruding cleanly from inside slide (Z=6.75) through compensator to Z=9.56!
    mb.tube_z(
        ST, SG, "usp", XC, BORE_Y,
        z0=6.72, z1=9.56, r_out=0.295, r_in=0.175, segs=24,
        uv_outer=UV_BARREL, uv_crown=UV_SCREW, uv_inner=UV_BORE,
        crown_inset=0.045, inner_depth=2.0,
    )
    # Iconic HK USP Match dark rubber O-ring lockup band near barrel end (subtle detail visible at muzzle)
    mb.cylinder_axis(SG, "usp", (XC, BORE_Y, 9.28), (XC, BORE_Y, 9.35), 0.300, 0.300, 20, UV_BORE, False, False)

    # ==================================================================
    # 2. HK USP MATCH BARREL WEIGHT / COMPENSATOR (bone: usp_parent)
    # ==================================================================
    # A) Rear under-rail clamping saddle (Z = 4.30 .. 7.38, hugs dust cover strictly below reciprocating slide Y<2.08)
    def saddle_ring(z, xw=0.64, y_bot=1.34, y_top=2.075, ch=0.16):
        return [
            (XC - xw + ch, y_bot, z),
            (XC + xw - ch, y_bot, z),
            (XC + xw, y_bot + ch, z),
            (XC + xw, y_top, z),
            (XC - xw, y_top, z),
            (XC - xw, y_bot + ch, z),
        ]

    mb.loft_z(ST, "usp", [
        saddle_ring(4.28, 0.54, 1.44, 1.98, 0.12),   # clean rear chamfer against trigger guard
        saddle_ring(4.46, 0.64, 1.34, 2.075, 0.16),
        saddle_ring(7.38, 0.64, 1.34, 2.075, 0.16),
    ], UV_BRUSH, cap_start=True, cap_end=False)

    # Dark machined side rail clamping grooves + prominent stainless hex locking screws (Z=5.1..6.7)
    for sgn in (-1.0, 1.0):
        x_in = XC + sgn * 0.635
        x_out = XC + sgn * 0.662
        xa, xb = (x_in, x_out) if sgn > 0 else (x_out, x_in)
        mb.box(SG, "usp", xa, xb, 1.56, 1.88, 4.72, 7.18, UV_BORE)
        # Machined stainless hex rail clamping screw right in the side rail channel!
        mb.cylinder_axis(ST, "usp", (XC + sgn * 0.62, 1.72, 5.75), (XC + sgn * 0.685, 1.72, 5.75), 0.115, 0.105, 6, UV_SCREW)
        # Front locator pin
        mb.cylinder_axis(ST, "usp", (XC + sgn * 0.62, 1.72, 6.72), (XC + sgn * 0.675, 1.72, 6.72), 0.07, 0.065, 12, UV_SCREW)

    # B) Forward Match Barrel Weight / Compensator Block (Z = 7.39 .. 9.42, clears slide at Z=7.358!)
    # Matches the signature HK USP beveled upper cross-section (flat bottom, vertical sides, 45-deg upper chamfers, flat top)
    def comp_ring(z, xw=0.635, y_bot=1.34, y_mid=2.84, y_top=3.54, x_top=0.415, ch_bot=0.16):
        return [
            (XC - xw + ch_bot, y_bot, z),
            (XC + xw - ch_bot, y_bot, z),
            (XC + xw, y_bot + ch_bot, z),
            (XC + xw, y_mid, z),
            (XC + x_top, y_top, z),
            (XC - x_top, y_top, z),
            (XC - xw, y_mid, z),
            (XC - xw, y_bot + ch_bot, z),
        ]

    # Forward-raked HK USP Match silhouette (chin slightly beveled back at bottom, crisp muzzle nose chamfer!)
    mb.loft_z(ST, "usp", [
        comp_ring(7.39, 0.635, 1.34, 2.84, 3.54, 0.415, 0.16),
        comp_ring(9.04, 0.635, 1.34, 2.84, 3.54, 0.415, 0.16),
        comp_ring(9.38, 0.620, 1.88, 2.88, 3.51, 0.400, 0.14),   # bold angled Match chin!
        comp_ring(9.44, 0.540, 2.02, 2.88, 3.43, 0.350, 0.12),   # crisp front muzzle chamfer!
    ], UV_BRUSH, cap_start=True, cap_end=True)

    # Dark circular recessed muzzle surround ring around the protruding extended barrel (Z=9.43..9.46)
    mb.cylinder_axis(SG, "usp", (XC, BORE_Y, 9.34), (XC, BORE_Y, 9.465), 0.355, 0.355, 22, UV_BORE)
    # Recoil spring guide-rod recessed boss below barrel on front face (classic HK USP Match front detail)
    mb.cylinder_axis(SG, "usp", (XC, 2.18, 9.26), (XC, 2.18, 9.445), 0.17, 0.17, 16, UV_BORE)
    mb.cylinder_axis(ST, "usp", (XC, 2.18, 9.30), (XC, 2.18, 9.465), 0.11, 0.10, 14, UV_SCREW)

    # Signature HK USP Match twin machined side compensator/weight-reduction slots (on both left & right sides!)
    for sgn in (-1.0, 1.0):
        x0 = XC + sgn * 0.625
        x1 = XC + sgn * 0.652
        xa, xb = (x0, x1) if sgn > 0 else (x1, x0)
        # Long sculpted horizontal-angled weight recess channel with dark interior + bright rim bevel
        slot1 = [(7.62, 2.06), (8.26, 2.06), (8.36, 2.74), (7.72, 2.74)]
        slot2 = [(8.46, 2.06), (9.02, 2.06), (9.12, 2.74), (8.56, 2.74)]
        mb.extrude_x(SG, "usp", slot1, xa, xb, UV_BORE)
        mb.extrude_x(SG, "usp", slot2, xa, xb, UV_BORE)
        # Subtle lower fluted weight-relief stripe along the side
        mb.box(ST, "usp", xa, xb, 1.62, 1.84, 7.52, 8.95, UV_SLOT)

    # Top matte black serrated anti-glare sight rib along the top flat of the Match weight
    mb.box(SG, "usp", XC - 0.24, XC + 0.24, 3.53, 3.585, 7.42, 9.24, UV_SERR)
    # Underside weight-cut rail slot
    mb.box(SG, "usp", XC - 0.24, XC + 0.24, 1.31, 1.36, 4.65, 8.90, UV_BORE)

    # ==================================================================
    # 3. СПОРТИВНЫЙ УСМ — MATCH COMPETITION TRIGGER (bone: usp_trigger)
    # ==================================================================
    # Pivot origin is [0, 1.69, 2.93]; trigger guard cavity spans Y=0.25..1.80, Z=2.30..3.50
    # Smooth ergonomic curved Match trigger blade (swept back comfortably in the guard)
    trig_poly = [
        (2.84, 1.78),
        (3.04, 1.78),
        (2.94, 1.35),
        (2.74, 0.96),
        (2.70, 0.62),
        (2.84, 0.36),
        (2.72, 0.30),
        (2.50, 0.56),
        (2.50, 0.96),
        (2.68, 1.40),
    ]
    mb.extrude_x(SG, "usp_trigger", trig_poly, XC - 0.145, XC + 0.145, UV_MATTE)
    # Machined side weight-reduction accent strips on the Match trigger shoe
    for sgn in (-1.0, 1.0):
        xa, xb = (XC + 0.14, XC + 0.158) if sgn > 0 else (XC - 0.158, XC - 0.14)
        mb.box(ST, "usp_trigger", xa, xb, 0.68, 1.32, 2.58, 2.74, UV_SLOT)
    # ICONIC HK USP MATCH OVERTRAVEL STOP SCREW protruding through the lower trigger face!
    # Machined stainless steel adjustment screw running front-to-back through trigger blade
    mb.cylinder_axis(ST, "usp_trigger", (XC, 0.58, 2.40), (XC, 0.58, 2.86), 0.068, 0.068, 14, UV_SCREW)
    # Slightly wider knurled/hex stop collar on front face of trigger
    mb.cylinder_axis(ST, "usp_trigger", (XC, 0.58, 2.76), (XC, 0.58, 2.88), 0.088, 0.082, 12, UV_SCREW)

    # ==================================================================
    # 4. СПОРТИВНЫЙ УСМ — MATCH SKELETONIZED HAMMER (bone: usp_hammer)
    # ==================================================================
    # Pivot origin is [0, 2.33, -1.06]. Authentic elongated spurred HK Match hammer with a
    # TRUE 3D HOLLOWED OVAL CUTOUT right through the hammer body (clearly visible in FPS!)
    # Dark blued gunmetal hammer contrasts crisply against the classic HL2 brushed silver slide!
    N_HAM = 16
    ham_out, ham_in = [], []
    cz_h, cy_h = -1.36, 2.82
    for i in range(N_HAM):
        a = 2.0 * math.pi * i / N_HAM
        ca, sa = math.cos(a), math.sin(a)
        # Elongated diagonal oval leaning back-up toward the hammer spur
        oz = cz_h + 0.34 * ca - 0.14 * sa
        oy = cy_h + 0.14 * ca + 0.44 * sa
        iz = cz_h + 0.17 * ca - 0.07 * sa
        iy = cy_h + 0.07 * ca + 0.23 * sa
        ham_out.append((oz, oy))
        ham_in.append((iz, iy))
    mb.extrude_x_ring(ST, "usp_hammer", ham_out, ham_in, XC - 0.155, XC + 0.155, UV_BRUSH)

    # Lower hammer pivot boss seated into the frame (Z=-1.26..-0.92, Y=2.16..2.52)
    mb.box(ST, "usp_hammer", XC - 0.155, XC + 0.155, 2.18, 2.54, -1.24, -0.94, UV_BRUSH)

    # Extended sleek Match competition thumb spur sweeping back above the beavertail!
    spur_poly = [
        (-1.42, 3.16),
        (-1.16, 3.22),
        (-1.26, 3.38),
        (-1.72, 3.46),
        (-1.90, 3.38),
        (-1.86, 3.24),
        (-1.62, 3.16),
    ]
    mb.extrude_x(ST, "usp_hammer", spur_poly, XC - 0.165, XC + 0.165, UV_BRUSH)
    # Crisp serrated thumb checkering bars along the top of the hammer spur
    for zs in (-1.34, -1.46, -1.58, -1.70, -1.80):
        mb.box(SG, "usp_hammer", XC - 0.172, XC + 0.172, 3.41, 3.475, zs - 0.03, zs + 0.03, UV_SERR)

    # ==================================================================
    # 5. РЕГУЛИРУЕМЫЕ ПРИЦЕЛЬНЫЕ ПРИСПОСОБЛЕНИЯ (bone: usp_slide)
    # ==================================================================
    # Reciprocates cleanly with the USP Slide on every shot!
    # A) Adjustable HK LPA / Match Target Rear Sight (Z = -1.46 .. -0.14, Y = 3.56 .. 4.04)
    # Dovetail base seated firmly into rear slide top
    mb.box(SG, "usp_slide", XC - 0.42, XC + 0.42, 3.55, 3.74, -1.10, -0.14, UV_MATTE)
    # Hinged elevating rear target sight housing body (overhangs slightly rearward like real LPA/HK Match sight!)
    # Lower bridge below the target notch (Y = 3.72 .. 3.82)
    mb.box(SG, "usp_slide", XC - 0.44, XC + 0.44, 3.72, 3.83, -1.46, -0.18, UV_MATTE)
    # Left target sight ear (+X: XC+0.095 .. XC+0.44, Y=3.82..4.04) — creates a TRUE open square notch!
    mb.box(SG, "usp_slide", XC + 0.095, XC + 0.44, 3.82, 4.04, -1.46, -0.92, UV_MATTE)
    # Right target sight ear (-X: XC-0.44 .. XC-0.095, Y=3.82..4.04)
    mb.box(SG, "usp_slide", XC - 0.44, XC - 0.095, 3.82, 4.04, -1.46, -0.92, UV_MATTE)
    # Forward hinged elevation leaf body (Z = -0.94 .. -0.20, Y = 3.74 .. 3.96, slopes gently down forward)
    def leaf_ring(z, xw, y0, y1):
        return [
            (XC - xw, y0, z), (XC + xw, y0, z), (XC + xw - 0.04, y1, z), (XC - xw + 0.04, y1, z)
        ]
    mb.loft_z(SG, "usp_slide", [
        leaf_ring(-0.94, 0.42, 3.74, 3.98),
        leaf_ring(-0.42, 0.40, 3.74, 3.96),
        leaf_ring(-0.18, 0.36, 3.70, 3.84),
    ], UV_MATTE)

    # TOP ELEVATION MICROMETER SCREW (slotted stainless steel dial clearly visible in first-person idle!)
    mb.cylinder_axis(ST, "usp_slide", (XC, 3.94, -0.56), (XC, 4.045, -0.56), 0.125, 0.125, 16, UV_SCREW)
    mb.box(SG, "usp_slide", XC - 0.10, XC + 0.10, 4.035, 4.058, -0.585, -0.535, UV_BORE)  # screw slot!
    # Elevation hinge cross-pin at front of sight
    mb.cylinder_axis(ST, "usp_slide", (XC - 0.43, 3.76, -0.32), (XC + 0.43, 3.76, -0.32), 0.042, 0.042, 10, UV_SCREW)

    # RIGHT-SIDE WINDAGE MICROMETER SCREW (slotted stainless steel dial protruding on -X side!)
    mb.cylinder_axis(ST, "usp_slide", (XC - 0.41, 3.88, -1.16), (XC - 0.515, 3.88, -1.16), 0.095, 0.090, 14, UV_SCREW)
    mb.box(SG, "usp_slide", XC - 0.525, XC - 0.505, 3.865, 3.895, -1.23, -1.09, UV_BORE)  # windage slot!

    # Anti-glare serrated rear face + twin bright high-contrast target reference dots facing shooter!
    mb.quad(SG, "usp_slide", (XC + 0.43, 3.82, -1.466), (XC + 0.10, 3.82, -1.466),
            (XC + 0.10, 4.03, -1.466), (XC + 0.43, 4.03, -1.466), UV_SERR, (0, 0, -1))
    mb.quad(SG, "usp_slide", (XC - 0.10, 3.82, -1.466), (XC - 0.43, 3.82, -1.466),
            (XC - 0.43, 4.03, -1.466), (XC - 0.10, 4.03, -1.466), UV_SERR, (0, 0, -1))
    # Bright crisp target dots on left & right rear ears
    for dx in (-0.23, 0.23):
        mb.cylinder_axis(SG, "usp_slide", (XC + dx, 3.925, -1.45), (XC + dx, 3.925, -1.474), 0.045, 0.045, 12, UV_DOT)

    # B) Tall Match Front Target Sight (Z = +6.42 .. +7.26, Y = 3.56 .. 4.06 — matched level with rear sight!)
    # Dovetail base block seated on front slide top
    mb.box(SG, "usp_slide", XC - 0.34, XC + 0.34, 3.55, 3.68, 6.46, 7.22, UV_MATTE)
    # Tall crisp undercut competition front target blade
    front_poly = [
        (6.46, 3.67),
        (7.24, 3.67),
        (7.14, 3.88),
        (6.72, 4.055),
        (6.48, 4.055),
        (6.54, 3.84),   # crisp anti-glare undercut rear face!
    ]
    mb.extrude_x(SG, "usp_slide", front_poly, XC - 0.075, XC + 0.075, UV_MATTE)
    # Bright crisp contrast front aiming dot on shooter-facing face!
    mb.cylinder_axis(SG, "usp_slide", (XC, 3.94, 6.53), (XC, 3.94, 6.485), 0.042, 0.042, 12, UV_DOT)


# ------------------------------------------------------------------ USP Match Deferred PBR Textures
def make_usp_materials(v):
    out_dir = os.path.join(C.MOD, "materials", *MAT_DIR_REL.split("/"))
    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(SRC_DIR, exist_ok=True)

    # 1. CS:GO pist_223 base body texture (2048x2048) + exponent (512 -> 2048)
    raw_diff = v.read("materials/models/weapons/v_models/pist_223/pist_223.vtf")
    raw_exp = v.read("materials/models/weapons/v_models/pist_223/pist_223_exponent.vtf")
    im_diff = C.vtf_to_rgba(raw_diff)
    im_exp = C.vtf_to_rgba(raw_exp).resize(im_diff.size, Image.BILINEAR)

    diff = np.asarray(im_diff, dtype=np.float32)
    rgb = diff[..., :3] / 255.0
    csgo_alpha = diff[..., 3] / 255.0
    exp_arr = np.asarray(im_exp, dtype=np.float32) / 255.0
    exp_r = exp_arr[..., 0]  # shininess (bright red = blued metal slide, dark = polymer grip!)

    # Cleanly replace "USP TACTICAL" (x=990..1048, y=575..845) and "CAL .223" (x=990..1045, y=1020..1110)
    # with authentic "USP MATCH" and "9mm x 19" laser-etched slide markings!
    def heal_box(arr, x0, x1, y0, y1):
        # Sample clean adjacent slide columns to the left & right of the engraving
        left_col = arr[y0:y1, x0 - 10 : x0 - 2].mean(axis=1, keepdims=True)
        right_col = arr[y0:y1, x1 + 2 : x1 + 10].mean(axis=1, keepdims=True)
        t = np.linspace(0.0, 1.0, x1 - x0, dtype=np.float32)
        if arr.ndim == 3:
            t = t[None, :, None]
        else:
            t = t[None, :]
        arr[y0:y1, x0:x1] = left_col * (1.0 - t) + right_col * t

    for bx0, bx1, by0, by1 in ((992, 1046, 578, 845), (992, 1044, 1020, 1108)):
        heal_box(rgb, bx0, bx1, by0, by1)
        heal_box(csgo_alpha, bx0, bx1, by0, by1)
        heal_box(exp_r, bx0, bx1, by0, by1)

    # Render crisp vertical "USP MATCH" and "9mm x 19" engraving matching original slide orientation (rotated -90 deg)
    def make_vertical_engrave(text, font_sz, w_box, h_box):
        tmp = Image.new("L", (h_box, w_box), 0)
        dr = ImageDraw.Draw(tmp)
        fnt = ImageFont.truetype(FONT_BOLD, font_sz)
        dr.text((h_box // 2, w_box // 2), text, font=fnt, fill=255, anchor="mm")
        rot = tmp.rotate(-90, expand=True)
        return np.asarray(rot, dtype=np.float32) / 255.0

    eng1 = make_vertical_engrave("USP MATCH", 28, 46, 240)
    eng2 = make_vertical_engrave("9mm x 19", 15, 34, 92)

    # 1A. Classic HL2 Polymer Frame & Grip (usp_body): deep matte charcoal polymer + dark hardware
    # Desaturate CS:GO's brownish grime slightly for clean HK matte black-charcoal polymer
    luma_body = 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]
    rgb_clean = rgb * 0.55 + luma_body[..., None] * 0.45
    rgb_body_pbr = np.power(np.clip(rgb_clean, 0.0, 1.0), 0.86) * 1.06 + 0.014
    rgb_body_pbr = np.clip(rgb_body_pbr, 0.0, 1.0)

    gloss_body = 0.20 + 0.34 * np.clip(exp_r * 1.2, 0.0, 1.0) + 0.28 * np.sqrt(np.clip(csgo_alpha, 0.0, 1.0))
    gloss_body = np.clip(gloss_body, 0.15, 0.80)

    blur_lb = np.asarray(Image.fromarray((np.clip(luma_body, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2.5)), dtype=np.float32) / 255.0
    height_body = np.clip(0.5 + (luma_body - blur_lb) * 1.7 + csgo_alpha * 0.08, 0.0, 1.0)

    rgb_u8 = np.clip(np.rint(rgb_body_pbr * 255.0), 0, 255).astype(np.uint8)
    nrm_u8 = C.height_to_dx_normal(height_body, strength=2.3)
    gloss_u8 = np.clip(np.rint(gloss_body * 255.0), 0, 255).astype(np.uint8)

    Image.fromarray(rgb_u8).save(os.path.join(SRC_DIR, "usp_body_base.png"))
    C.write_albedo_vtf(os.path.join(out_dir, "usp_body.vtf"), rgb_u8)
    C.write_normal_gloss_vtf(os.path.join(out_dir, "usp_body_n.vtf"), nrm_u8, gloss_u8)
    C.write_pbr_vmt(
        os.path.join(out_dir, "usp_body.vmt"),
        f"{MAT_DIR_REL}/usp_body",
        f"{MAT_DIR_REL}/usp_body_n",
        metalness=0.18,
        specular=1.0,
        phong_exp=20,
        phong_boost=1.0,
    )

    # 1B. Classic HL2 Brushed Silver Stainless Slide (usp_slide — dedicated high-metalness PBR material!)
    # Matches original HL2 v_pistol_sheet.vtf: iconic brushed satin silver-stainless slide with dark
    # rear serrations, crisp dark laser-etched "USP MATCH / 9mm x 19" markings, and bright worn edges!
    rng = np.random.default_rng(4509)
    H2, W2 = rgb.shape[:2]
    # Longitudinal brushed stainless grain running along slide axis (vertical Y in pist_223 UV!)
    st_1d = rng.normal(0.0, 1.0, (96, W2)).astype(np.float32)
    slide_brush = np.asarray(
        Image.fromarray(np.clip(st_1d * 26 + 128, 0, 255).astype(np.uint8)).resize((W2, H2), Image.BILINEAR),
        dtype=np.float32,
    ) / 255.0 - 0.5
    slide_fine = rng.normal(0.0, 0.09, (H2, W2)).astype(np.float32)
    s_grain = slide_brush * 0.78 + slide_fine * 0.22

    # Local-contrast metallic remap: lift ALL flat slide faces (left flat ~0.10, right/bevel ~0.13)
    # uniformly into classic HL2 brushed satin stainless silver (~0.70..0.82), while keeping deep
    # rear serration grooves and crevices crisply darkened just like stock HL2 v_pistol_sheet.vtf!
    luma_s = luma_body
    blur_wide = np.asarray(
        Image.fromarray((np.clip(luma_s, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(18.0)),
        dtype=np.float32,
    ) / 255.0
    detail_s = luma_s - blur_wide
    crevice = np.clip(
        np.maximum((-detail_s - 0.014) / 0.034, (0.052 - luma_s) / 0.032),
        0.0,
        1.0,
    )
    silver_val = (
        0.74
        + np.clip((luma_s - 0.115) * 0.50, -0.06, 0.08)
        + np.clip(detail_s * 2.4, -0.20, 0.14)
        + csgo_alpha * 0.10
        + s_grain * 0.075
    )
    val = silver_val * (1.0 - crevice * 0.72) + 0.14 * (crevice * 0.72)
    val = np.clip(val, 0.08, 0.92)
    # Clean neutral brushed stainless steel tint matching HL2 v_pistol_sheet
    sl_rgb = np.stack([val * 0.99, val * 0.995, val * 1.01], axis=-1)

    sl_gloss = (
        (0.66 + 0.10 * np.clip(exp_r, 0.0, 1.0) + s_grain * 0.14) * (1.0 - crevice)
        + 0.22 * crevice
        + csgo_alpha * 0.18
    )
    sl_gloss = np.clip(sl_gloss, 0.18, 0.92)

    sl_height = np.clip(height_body + s_grain * 0.09 * (1.0 - crevice), 0.0, 1.0)

    # Apply crisp DARK etched "USP MATCH" and "9mm x 19" markings directly onto the silver stainless slide!
    def apply_dark_engrave(mask, cx, cy):
        mh, mw = mask.shape
        x0, y0 = cx - mw // 2, cy - mh // 2
        col_eng = np.array([0.13, 0.14, 0.16], dtype=np.float32)
        a = np.clip(mask * 0.88, 0.0, 1.0)
        sl_rgb[y0 : y0 + mh, x0 : x0 + mw] = (
            sl_rgb[y0 : y0 + mh, x0 : x0 + mw] * (1.0 - a[..., None]) + col_eng[None, None, :] * a[..., None]
        )
        sl_gloss[y0 : y0 + mh, x0 : x0 + mw] = sl_gloss[y0 : y0 + mh, x0 : x0 + mw] * (1.0 - a * 0.65) + 0.22 * (a * 0.65)
        sl_height[y0 : y0 + mh, x0 : x0 + mw] = np.clip(sl_height[y0 : y0 + mh, x0 : x0 + mw] - a * 0.16, 0.0, 1.0)

    apply_dark_engrave(eng1, 1019, 710)
    apply_dark_engrave(eng2, 1018, 1064)

    sl_rgb_u8 = np.clip(np.rint(sl_rgb * 255.0), 0, 255).astype(np.uint8)
    sl_nrm_u8 = C.height_to_dx_normal(sl_height, strength=2.4)
    sl_gloss_u8 = np.clip(np.rint(sl_gloss * 255.0), 0, 255).astype(np.uint8)

    Image.fromarray(sl_rgb_u8).save(os.path.join(SRC_DIR, "usp_slide_base.png"))
    C.write_albedo_vtf(os.path.join(out_dir, "usp_slide.vtf"), sl_rgb_u8)
    C.write_normal_gloss_vtf(os.path.join(out_dir, "usp_slide_n.vtf"), sl_nrm_u8, sl_gloss_u8)
    C.write_pbr_vmt(
        os.path.join(out_dir, "usp_slide.vmt"),
        f"{MAT_DIR_REL}/usp_slide",
        f"{MAT_DIR_REL}/usp_slide_n",
        metalness=0.78,
        specular=1.12,
        phong_exp=40,
        phong_boost=2.3,
    )

    # 2. Match Hardware Steel Atlas (1024x1024) — HL2-authentic palette:
    #    - Dark gunmetal / blued machined steel barrel weight compensator & Match hammer (matches HL2 v_pistol!)
    #    - Bright polished stainless steel extended 6.02" Match barrel & precision hex/micrometer screws!
    S = 1024
    # Directional fine horizontal machined steel brushing
    streak_1d = rng.normal(0.0, 1.0, (S, 64)).astype(np.float32)
    brushed = np.asarray(Image.fromarray(np.clip((streak_1d * 28 + 128), 0, 255).astype(np.uint8)).resize((S, S), Image.BILINEAR), dtype=np.float32) / 255.0 - 0.5
    fine = rng.normal(0.0, 0.12, (S, S)).astype(np.float32)
    btex = brushed * 0.75 + fine * 0.25

    st_rgb = np.zeros((S, S, 3), dtype=np.float32)
    st_gloss = np.full((S, S), 0.64, dtype=np.float32)
    st_h = np.full((S, S), 0.5, dtype=np.float32)

    # Top-Left (0..512, 0..512): Neutral Dark Gunmetal / Blued Steel (iconic HL2 dark Match muzzle weight & hammer!)
    st_rgb[:512, :512] = np.array([56.0, 58.0, 61.0])[None, None, :] + btex[:512, :512, None] * 20.0
    st_gloss[:512, :512] = np.clip(0.56 + btex[:512, :512] * 0.20, 0.36, 0.78)
    st_h[:512, :512] = np.clip(0.50 + btex[:512, :512] * 0.24, 0.0, 1.0)

    # Top-Right (512..1024, 0..512): Bright Machined Stainless Steel (Extended 6.02" Match barrel & crown!)
    st_rgb[:512, 512:] = np.array([204.0, 208.0, 214.0])[None, None, :] + btex[:512, 512:, None] * 22.0
    st_gloss[:512, 512:] = np.clip(0.80 + btex[:512, 512:] * 0.16, 0.58, 0.94)
    st_h[:512, 512:] = np.clip(0.50 + btex[:512, 512:] * 0.18, 0.0, 1.0)

    # Bottom-Left (0..512, 512..1024): Deeper darkened gunmetal flutes / recesses
    st_rgb[512:, :512] = np.array([32.0, 34.0, 37.0])[None, None, :] + btex[512:, :512, None] * 12.0
    st_gloss[512:, :512] = np.clip(0.42 + btex[512:, :512] * 0.14, 0.26, 0.62)
    st_h[512:, :512] = np.clip(0.45 + btex[512:, :512] * 0.20, 0.0, 1.0)

    # Bottom-Right (512..1024, 512..1024): Bright machined stainless steel (hex bolts, guide rod, overtravel & micrometer screws)
    st_rgb[512:, 512:] = np.array([196.0, 200.0, 206.0])[None, None, :] + btex[512:, 512:, None] * 16.0
    st_gloss[512:, 512:] = np.clip(0.82 + btex[512:, 512:] * 0.14, 0.62, 0.94)
    st_h[512:, 512:] = np.clip(0.50 + btex[512:, 512:] * 0.14, 0.0, 1.0)

    st_rgb_u8 = np.clip(st_rgb, 0, 255).astype(np.uint8)
    st_nrm_u8 = C.height_to_dx_normal(st_h, strength=1.9)
    st_gloss_u8 = np.clip(np.rint(st_gloss * 255.0), 0, 255).astype(np.uint8)

    Image.fromarray(st_rgb_u8).save(os.path.join(SRC_DIR, "usp_match_steel_base.png"))
    C.write_albedo_vtf(os.path.join(out_dir, "usp_match_steel.vtf"), st_rgb_u8)
    C.write_normal_gloss_vtf(os.path.join(out_dir, "usp_match_steel_n.vtf"), st_nrm_u8, st_gloss_u8)
    C.write_pbr_vmt(
        os.path.join(out_dir, "usp_match_steel.vmt"),
        f"{MAT_DIR_REL}/usp_match_steel",
        f"{MAT_DIR_REL}/usp_match_steel_n",
        metalness=0.74,
        specular=1.10,
        phong_exp=36,
        phong_boost=2.0,
    )

    # 3. Matte Blued Competition Sights & Trigger Atlas (512x512)
    Q = 512
    sg_rgb = np.full((Q, Q, 3), (38, 40, 44), dtype=np.float32)
    sg_gloss = np.full((Q, Q), 0.34, dtype=np.float32)
    sg_h = np.full((Q, Q), 0.5, dtype=np.float32)
    sg_illum = np.zeros((Q, Q, 3), dtype=np.uint8)

    g2 = rng.normal(0.0, 1.5, (Q, Q)).astype(np.float32)
    sg_rgb += g2[..., None]
    # Top-Right (256..512, 0..256): Fine anti-glare horizontal serrations
    yy = np.arange(256, dtype=np.float32)[:, None]
    serr = np.sin(yy * (2.0 * math.pi / 8.0))
    sg_rgb[:256, 256:] = 30.0 + serr[..., None] * 10.0
    sg_h[:256, 256:] = 0.5 + serr * 0.25
    sg_gloss[:256, 256:] = 0.22
    # Bottom-Left (0..256, 256..512): Crisp bright white / soft tritium-green target aiming dot!
    sg_rgb[256:, :256] = (232.0, 248.0, 238.0)
    sg_gloss[256:, :256] = 0.65
    sg_illum[264:504, 8:248] = (175, 245, 200)  # subtle crisp target dot readability even in dim indoor rooms!
    # Bottom-Right (256..512, 256..512): Pitch-black deep bore & recessed slots
    sg_rgb[256:, 256:] = (10.0, 11.0, 13.0)
    sg_gloss[256:, 256:] = 0.10

    sg_rgb_u8 = np.clip(sg_rgb, 0, 255).astype(np.uint8)
    sg_nrm_u8 = C.height_to_dx_normal(sg_h, strength=2.6)
    sg_gloss_u8 = np.clip(np.rint(sg_gloss * 255.0), 0, 255).astype(np.uint8)

    C.write_albedo_vtf(os.path.join(out_dir, "usp_match_sights.vtf"), sg_rgb_u8)
    C.write_normal_gloss_vtf(os.path.join(out_dir, "usp_match_sights_n.vtf"), sg_nrm_u8, sg_gloss_u8)
    C.write_albedo_vtf(os.path.join(out_dir, "usp_match_sights_illum.vtf"), sg_illum)
    C.write_pbr_vmt(
        os.path.join(out_dir, "usp_match_sights.vmt"),
        f"{MAT_DIR_REL}/usp_match_sights",
        f"{MAT_DIR_REL}/usp_match_sights_n",
        metalness=0.42,
        specular=1.0,
        phong_exp=20,
        phong_boost=1.0,
        selfillum_mask=f"{MAT_DIR_REL}/usp_match_sights_illum",
        selfillum_tint="[0.7 0.95 0.78]",
    )

    return {"usp_body": rgb_u8, "usp_slide": sl_rgb_u8, "usp_match_steel": st_rgb_u8, "usp_match_sights": sg_rgb_u8}


# ------------------------------------------------------------------ Sounds & Script
USP_SOUNDS = {
    "Single": ("sound/weapons/usp/usp_unsilenced_01.wav", "CHAN_WEAPON", "0.82", "SNDLVL_GUNFIRE", True, ")"),
    "NPC_Single": ("sound/weapons/usp/usp_unsilenced_02.wav", "CHAN_WEAPON", "0.78", "SNDLVL_GUNFIRE", True, "^"),
    "Clipout": ("sound/weapons/usp/usp_clipout.wav", "CHAN_ITEM", "0.80", "SNDLVL_NORM", False, ""),
    "Clipin": ("sound/weapons/usp/usp_clipin.wav", "CHAN_ITEM", "0.80", "SNDLVL_NORM", False, ""),
    "Slideback": ("sound/weapons/usp/usp_slideback.wav", "CHAN_ITEM", "0.80", "SNDLVL_NORM", False, ""),
    "Sliderelease": ("sound/weapons/usp/usp_sliderelease.wav", "CHAN_ITEM", "0.80", "SNDLVL_NORM", False, ""),
    "Draw": ("sound/weapons/usp/usp_draw.wav", "CHAN_ITEM", "0.70", "SNDLVL_NORM", False, ""),
}


def make_usp_sounds(v):
    out_dir = os.path.join(C.MOD, "sound", *SND_DIR_REL.split("/"))
    os.makedirs(out_dir, exist_ok=True)
    C.extract_wav(v, "sound/weapons/usp/usp_unsilenced_03.wav", os.path.join(out_dir, "usp_unsilenced_03.wav"), force_mono=True)
    blocks = ["// make_csgo_weapons.py (USP Match / weapon_pistol): Counter-Strike: Global Offensive sounds"]
    for name, (src, chan, vol, lvl, mono, pfx) in USP_SOUNDS.items():
        base = os.path.basename(src)
        C.extract_wav(v, src, os.path.join(out_dir, base), force_mono=mono)
        if name == "Single":
            blocks.append(
                '"Weapon_RPMUSP.Single"\n{\n'
                '\t"channel"\t\t"CHAN_WEAPON"\n'
                '\t"volume"\t\t"0.82"\n'
                '\t"soundlevel"\t"SNDLVL_GUNFIRE"\n'
                '\t"pitch"\t\t\t"97,103"\n'
                '\t"rndwave"\n\t{\n'
                f'\t\t"wave"\t\t"){SND_DIR_REL}/usp_unsilenced_01.wav"\n'
                f'\t\t"wave"\t\t"){SND_DIR_REL}/usp_unsilenced_02.wav"\n'
                f'\t\t"wave"\t\t"){SND_DIR_REL}/usp_unsilenced_03.wav"\n'
                '\t}\n}\n'
            )
        elif name == "NPC_Single":
            blocks.append(
                '"Weapon_RPMUSP.NPC_Single"\n{\n'
                '\t"channel"\t\t"CHAN_WEAPON"\n'
                '\t"volume"\t\t"0.78"\n'
                '\t"soundlevel"\t"SNDLVL_GUNFIRE"\n'
                '\t"pitch"\t\t\t"95,105"\n'
                '\t"rndwave"\n\t{\n'
                f'\t\t"wave"\t\t"^{SND_DIR_REL}/usp_unsilenced_01.wav"\n'
                f'\t\t"wave"\t\t"^{SND_DIR_REL}/usp_unsilenced_02.wav"\n'
                '\t}\n}\n'
            )
        else:
            blocks.append(
                f'"Weapon_RPMUSP.{name}"\n{{\n'
                f'\t"channel"\t\t"{chan}"\n'
                f'\t"volume"\t\t"{vol}"\n'
                f'\t"soundlevel"\t"{lvl}"\n'
                f'\t"wave"\t\t\t"{pfx}{SND_DIR_REL}/{base}"\n}}\n'
            )
    return "\n".join(blocks)


def write_pistol_script(fov=68):
    script = f'''// Pistol -> CS:GO HK USP converted to HK USP Match (extended barrel, barrel weight, Match FCG & sights)
// Generated by make_csgo_weapons.py (uses_hands = 1 for ValveBiped c_arms)

WeaponData
{{
	"printname"				"#HL2_Pistol"
	"viewmodel"				"models/weapons/v_pistol.mdl"
	"playermodel"			"models/weapons/w_pistol.mdl"
	"anim_prefix"			"pistol"
	"bucket"				"1"
	"bucket_position"		"0"
	"bucket_360"			"0"
	"bucket_position_360"	"0"
	"viewmodel_fov"			"{fov}"
	"uses_hands"			"1"

	"clip_size"				"18"
	"primary_ammo"			"Pistol"
	"secondary_ammo"		"None"

	"weight"				"2"
	"rumble"				"1"
	"item_flags"			"0"

	SoundData
	{{
		"reload_npc"		"Weapon_Pistol.NPC_Reload"
		"empty"				"Weapon_Pistol.Empty"
		"single_shot"		"Weapon_RPMUSP.Single"
		"single_shot_npc"	"Weapon_RPMUSP.NPC_Single"
		"special1"			"Weapon_Pistol.Special1"
		"special2"			"Weapon_Pistol.Special2"
		"burst"				"Weapon_RPMUSP.Single"
	}}

	TextureData
	{{
		"weapon"		{{ "font" "WeaponIcons" "character" "d" }}
		"weapon_s"		{{ "font" "WeaponIconsSelected" "character" "d" }}
		"weapon_small"	{{ "font" "WeaponIconsSmall" "character" "d" }}
		"ammo"			{{ "font" "WeaponIconsSmall" "character" "p" }}
		"crosshair"		{{ "font" "Crosshairs" "character" "Q" }}
		"autoaim"		{{ "file" "sprites/crosshairs" "x" "0" "y" "48" "width" "24" "height" "24" }}
	}}
}}
'''
    with open(os.path.join(C.MOD, "scripts", "weapon_pistol.txt"), "w", encoding="ascii", newline="\n") as f:
        f.write(script)


# ------------------------------------------------------------------ Build USP Match
def build_usp_match(v, fs, preview_dir=None, fov=None, vm_preset="classic"):
    os.makedirs(SRC_DIR, exist_ok=True)
    mdl_path, ani_data = C.extract_csgo_model(v, "models/weapons/v_pist_223", SRC_DIR)
    m = C.dec.Model(fs, mdl_path)
    mv = C.dec.Model(fs, C.VB_REF)
    Wv = C.fk_bind(mv)

    scale, ratios = C.compute_arm_scale(m, mv)
    print("USP Match arm scale %.3f (fore, palm, f1, f2: %s)" % (scale, ", ".join("%.2f" % r for r in ratios)))

    low = {b.lower(): i for i, b in enumerate(m.bone_names)}
    seqs = {s["name"]: s for s in m.sequences()}
    Wb_raw = C.fk_bind(m)
    Wb_s = C.scale_world(Wb_raw, scale)

    idle_w = C.sample_csgo_seq(m, ani_data, fs, seqs["idle"], scale)
    retarget = C.CSGORetarget(idle_w[0], Wv, m, mv)

    GUN_PARTS = (
        ("usp", "v_weapon.223_parent"),
        ("usp_slide", "v_weapon.slide"),
        ("usp_trigger", "v_weapon.trigger"),
        ("usp_hammer", "v_weapon.hammer"),
        ("usp_clip", "v_weapon.clip"),
    )
    gi = {k: low[bn] for k, bn in GUN_PARTS}
    bone_map = {gi[k]: k for k, _ in GUN_PARTS}
    for extra in ("v_weapon.flash", "v_weapon.shelleject", "v_weapon.stattrack"):
        if extra in low:
            bone_map[low[extra]] = "usp"
    if "v_weapon.uid" in low:
        bone_map[low["v_weapon.uid"]] = "usp_slide"

    # Extract CS:GO USP studio bodypart ONLY (silencer bodypart 1 is omitted!)
    # Filter out the 4 replaced stock sub-parts:
    #   1) old Trigger mesh (replaced with Match Trigger + overtravel stop screw)
    #   2) old Hammer mesh (replaced with Match Skeletonized Spurred Hammer)
    #   3) old small Front & Rear Sights on Slide (Y > 3.59, replaced with Adjustable Match Target Sights)
    #   4) old short threaded muzzle stub protruding past Z > 7.36 (replaced with Extended 6.02" Stainless Match Barrel)
    W_par_inv_raw = C.inv(Wb_raw[gi["usp"]])
    bi_slide = gi["usp_slide"]
    bi_trig = gi["usp_trigger"]
    bi_ham = gi["usp_hammer"]
    bi_par = gi["usp"]

    def keep_usp_tri(tri, P, N, U, dom):
        b0 = int(dom[tri[0]])
        if b0 == bi_trig or b0 == bi_ham:
            return False
        pts = (W_par_inv_raw[:3, :3] @ P[tri].T).T + W_par_inv_raw[:3, 3]
        if b0 == bi_slide and pts[:, 1].max() > 3.588:
            zc = pts[:, 2].mean()
            if zc < 0.25 or zc > 6.15:
                return False
        if b0 == bi_par and pts[:, 2].max() > 7.365 and pts[:, 1].mean() > 2.55:
            return False
        return True

    tris_gun = C.extract_gun_tris(m, Wb_s, bone_map, scale, "usp_body", bodypart_idx=0, model_idx=0, tri_filter=keep_usp_tri)
    # Assign the classic HL2 brushed silver stainless material specifically to the reciprocating slide!
    tris_gun = [
        ("usp_slide" if corners[0][3] == "usp_slide" else mat, corners)
        for mat, corners in tris_gun
    ]

    # Build all 5 HK USP Match hardware upgrades in unscaled 223_parent local space
    parent_to_bone = {k: C.inv(Wb_raw[gi["usp"]]) @ Wb_raw[gi[k]] for k, _ in GUN_PARTS}
    mb = C.MeshBuilder(parent_to_bone, scale=scale)
    build_usp_match_parts(mb)
    tris_all = tris_gun + mb.tris
    print("USP Match mesh: %d base tris + %d Match upgrade tris = %d total" % (len(tris_gun), len(mb.tris), len(tris_all)))

    # Viewmodel placement = CS:GO viewmodel preset (default "classic": viewmodel_fov 54,
    # offset_x 2.5 right, offset_y 0, offset_z -1.5 down), eye space +X fwd, +Y left, +Z up
    VM_SHIFT, preset_fov = C.vm_preset_shift(vm_preset, scale)
    if fov is None:
        fov = preset_fov
    print("USP Match viewmodel preset '%s': fov %.0f, eye offset %s" % (vm_preset, fov, np.round(VM_SHIFT[:3, 3], 2)))

    def convert_frames(Wlist, extra_M=None, lock_slide_delta=None):
        out = []
        for Wc in Wlist:
            Moff = VM_SHIFT if extra_M is None else (extra_M @ VM_SHIFT)
            f = retarget.frame_bones(Wc, offset_M=Moff)
            for k, _ in GUN_PARTS:
                f[k] = Moff @ Wc[gi[k]]
            if lock_slide_delta is not None:
                f["usp_slide"] = f["usp"] @ lock_slide_delta
            out.append(f)
        return out

    def make_lower_frames(base_W, nframes, reverse=False):
        out = []
        for i in range(nframes):
            u = i / max(1, nframes - 1)
            if reverse:
                u = 1.0 - u
            s = u * u * (3.0 - 2.0 * u)
            M = np.eye(4)
            M[:3, :3] = C.euler_m((math.radians(-4.0 * s), math.radians(24.0 * s), math.radians(10.0 * s)))
            M[:3, 3] = np.array([-1.0 * s, 0.8 * s, -5.0 * s])
            out.append(convert_frames([base_W], M)[0])
        return out

    sk = C.arm_skeleton_order() + [
        ("usp", "ValveBiped.Bip01_R_Hand"),
        ("usp_slide", "usp"),
        ("usp_trigger", "usp"),
        ("usp_hammer", "usp"),
        ("usp_clip", "usp"),
    ]
    names = [n for n, _ in sk]
    parents = [p for _, p in sk]

    idle_f = convert_frames(idle_w)
    ref_frame = idle_f[0]
    C.write_smd_multi(os.path.join(SRC_DIR, "v_pistol_ref.smd"), names, parents, [ref_frame], tris_all, lambda b: ref_frame[b])

    allframes = {}
    qc_seqs = []

    def add_seq(name, frames, fps, act, extra="", events=()):
        allframes[name] = frames
        C.write_smd_multi(os.path.join(SRC_DIR, f"v_pistol_{name}.smd"), names, parents, frames)
        ev = " ".join('{ event %s %d "%s" }' % e for e in events)
        qc_seqs.append('$sequence "%s" "v_pistol_%s.smd" fps %.1f %s activity %s 1 %s' % (name, name, fps, extra, act, ev))

    shoot1_w = C.sample_csgo_seq(m, ani_data, fs, seqs["shoot1"], scale)
    shoot2_w = C.sample_csgo_seq(m, ani_data, fs, seqs["shoot2"], scale)
    shoot3_w = C.sample_csgo_seq(m, ani_data, fs, seqs["shoot3"], scale)
    dry_w = C.sample_csgo_seq(m, ani_data, fs, seqs["shoot_empty"], scale)
    reload_w = C.sample_csgo_seq(m, ani_data, fs, seqs["reload"], scale)
    draw_w = C.sample_csgo_seq(m, ani_data, fs, seqs["draw_silenced"], scale)  # draw_silenced is CS:GO's unsilenced draw!

    # Find furthest slide-back relative transform from shoot1 for idle_empty!
    s_rest = C.inv(idle_w[0][gi["usp"]]) @ idle_w[0][gi["usp_slide"]]
    s_locked = s_rest.copy()
    best_dist = 0.0
    for Wc in shoot1_w:
        rel_s = C.inv(Wc[gi["usp"]]) @ Wc[gi["usp_slide"]]
        d = float(np.linalg.norm(rel_s[:3, 3] - s_rest[:3, 3]))
        if d > best_dist:
            best_dist = d
            s_locked = rel_s

    shot_ev = (("AE_MUZZLEFLASH", 0, "PISTOL MUZZLE"), ("6001", 1, "0"))
    snd = lambda fr, s: ("5004", fr, "Weapon_RPMUSP." + s)

    add_seq("idle", idle_f, 30.0, "ACT_VM_IDLE", "loop")
    add_seq("idle_empty", convert_frames(idle_w, lock_slide_delta=s_locked), 30.0, "ACT_VM_IDLE_EMPTY", "loop")
    add_seq("fire", convert_frames(shoot1_w), 32.0, "ACT_VM_PRIMARYATTACK", "", shot_ev)
    add_seq("fire1", convert_frames(shoot2_w), 32.0, "ACT_VM_RECOIL1", "", shot_ev)
    add_seq("fire2", convert_frames(shoot3_w), 32.0, "ACT_VM_RECOIL2", "", shot_ev)
    add_seq("fire3", convert_frames(shoot1_w), 34.0, "ACT_VM_RECOIL3", "", shot_ev)
    add_seq("dryfire", convert_frames(dry_w), 30.0, "ACT_VM_DRYFIRE")

    # Reload (66 frames @ 33 fps ~ 2.0s) with synchronized CS:GO USP foley events
    r_evs = [
        snd(12, "Clipout"),
        snd(25, "Clipin"),
        snd(45, "Slideback"),
        snd(51, "Sliderelease"),
    ]
    add_seq("reload", convert_frames(reload_w), 33.0, "ACT_VM_RELOAD", "", r_evs)

    # Draw (31 frames @ 32 fps)
    d_evs = [
        snd(0, "Draw"),
        snd(8, "Slideback"),
        snd(14, "Sliderelease"),
    ]
    add_seq("draw", convert_frames(draw_w), 32.0, "ACT_VM_DRAW", "", d_evs)

    # Holster (smooth reverse of draw)
    hol_w = list(reversed(draw_w[:18]))
    add_seq("holster", convert_frames(hol_w), 36.0, "ACT_VM_HOLSTER")

    # Lowered pistol poses
    low_frames = make_lower_frames(idle_w[0], 11, reverse=False)
    add_seq("idletolow", low_frames, 30.0, "ACT_VM_IDLE_TO_LOWERED")
    add_seq("lowidle", [low_frames[-1], low_frames[-1]], 10.0, "ACT_VM_IDLE_LOWERED", "loop")
    add_seq("lowtoidle", make_lower_frames(idle_w[0], 11, reverse=True), 30.0, "ACT_VM_LOWERED_TO_IDLE")

    # Attachments in scaled usp local space:
    # Muzzle moved cleanly forward to tip of the NEW Extended 6.02" Match Barrel (Z = 9.65)!
    mz_pos = np.array([XC, BORE_Y, 9.65]) * scale
    muzzle = C.frame(np.array([0.0, 0.0, 1.0]), np.array([1.0, 0.0, 0.0]), mz_pos)
    # Shell eject (attachment #2 = "1"): ejects right (-X local), slightly up (+Y) & back (-Z)
    ej_pos = np.array([-0.24, 3.34, 3.23]) * scale
    eject_dir = C.norm(np.array([-0.85, 0.48, -0.20]))
    shell = C.frame(eject_dir, C.orth(np.array([0.0, 0.0, 1.0]), eject_dir), ej_pos)

    vqc = [
        '$modelname "weapons/v_pistol.mdl"',
        "$origin 0 0 0 -90",
        '$body "weapon" "v_pistol_ref.smd"',
        f'$cdmaterials "{MAT_DIR_REL}"',
    ]
    vqc += ['$bonemerge "%s"' % b for b in C.VIEW_BONES if b in names]
    vqc += [C.att_line("muzzle", "usp", muzzle), C.att_line("1", "usp", shell)]
    vqc += qc_seqs
    vqc_path = os.path.join(SRC_DIR, "v_pistol.qc")
    with open(vqc_path, "w", encoding="ascii", newline="\n") as f:
        f.write("// make_csgo_weapons.py: HL2RPM HK USP Match viewmodel (replaces stock v_pistol)\n" + "\n".join(vqc) + "\n")

    # ---------------- Worldmodel (w_pistol.mdl) ----------------
    # Match stock HL2 w_pistol's exact aiming orientation in ValveBiped.Bip01_R_Hand space
    # so Metrocops, Citizens, and third-person player aim straight horizontally!
    # Stock HL2 w_pistol muzzle in R_Hand: fwd=[0.997, -0.020, -0.078], up=[-0.081, -0.152, -0.985]
    fwd_rh = C.norm([0.997, -0.020, -0.078])
    up_rh = C.orth(np.array([-0.081, -0.152, -0.985]), fwd_rh)
    left_rh = np.cross(up_rh, fwd_rh)  # points along -Y in R_Hand (same handedness as FP & w_beretta)
    R_w = np.column_stack([left_rh, up_rh, fwd_rh])
    # Place USP Match grip center [XC, 0.0, 0.4] squarely inside R_Hand palm ([3.5, -1.5, -1.6])
    rel_gun = np.eye(4)
    rel_gun[:3, :3] = R_w
    rel_gun[:3, 3] = np.array([3.5, -1.5, -1.6]) - R_w @ (np.array([XC, 0.0, 0.4]) * scale)
    mz_rh = (rel_gun @ muzzle)[:3, 3]
    print("USP Match worldmodel muzzle in R_Hand space: %s (stock HL2 w_pistol was [11.31, -2.18, -4.58])" % np.round(mz_rh, 2))

    wnames = ["ValveBiped.Bip01_R_Hand", "ValveBiped.Weapon_bone", "usp_slide", "usp_trigger", "usp_hammer", "usp_clip"]
    wpar = [None, "ValveBiped.Bip01_R_Hand", "ValveBiped.Weapon_bone", "ValveBiped.Weapon_bone", "ValveBiped.Weapon_bone", "ValveBiped.Weapon_bone"]
    wb = "ValveBiped.Weapon_bone"
    wrel = {wb: rel_gun}
    for k in ("usp_slide", "usp_trigger", "usp_hammer", "usp_clip"):
        wrel[k] = rel_gun @ C.inv(ref_frame["usp"]) @ ref_frame[k]

    wtris = [(mat, [(p, n, uv, wb if b == "usp" else b) for p, n, uv, b in corners]) for mat, corners in tris_all]

    # Model's own pose (for 3D Inventory Preview Card hl2rpm_itempreview.cpp & CK viewport):
    # Stand UPRIGHT with sights pointing +Z UP, barrel along +X, left USP MATCH side along +Y!
    # (In NPC/player hands, bonemerge overrides ValveBiped.Bip01_R_Hand so only wrel matters.)
    R_local_to_ground = np.array([
        [0.0, 0.0, 1.0],   # local +Z (barrel) -> world +X
        [1.0, 0.0, 0.0],   # local +X (left)   -> world +Y
        [0.0, 1.0, 0.0],   # local +Y (up)     -> world +Z (UP!)
    ])
    H = np.eye(4)
    H[:3, :3] = R_local_to_ground @ rel_gun[:3, :3].T
    pts_g = np.array([(H @ wrel[b] @ np.r_[p, 1.0])[:3] for _, c in wtris for p, _, _, b in c])
    lo, hi = pts_g.min(0), pts_g.max(0)
    H[:3, 3] = -np.array([(lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5, lo[2]])
    wf = {"ValveBiped.Bip01_R_Hand": H}
    for k, M in wrel.items():
        wf[k] = H @ M

    C.write_smd_multi(os.path.join(SRC_DIR, "w_pistol_ref.smd"), wnames, wpar, [wf], wtris, lambda b: wf[b])
    C.write_smd_multi(os.path.join(SRC_DIR, "w_pistol_idle.smd"), wnames, wpar, [wf])
    C.write_phys_hull(os.path.join(SRC_DIR, "w_pistol_phys.smd"), "ValveBiped.Bip01_R_Hand", H, wtris, lambda b: wf[b])

    wqc = [
        '$modelname "weapons/w_pistol.mdl"',
        "$origin 0 0 0 -90",
        '$body "weapon" "w_pistol_ref.smd"',
        f'$cdmaterials "{MAT_DIR_REL}"',
        '$surfaceprop "weapon"',
        C.att_line("muzzle", wb, muzzle),
        C.att_line("1", wb, shell),
        '$sequence "idle" "w_pistol_idle.smd" fps 30 activity ACT_VM_IDLE 1',
        '$sequence "attack1" "w_pistol_idle.smd" fps 30 activity ACT_RANGE_ATTACK_PISTOL 1 '
        '{ event 3014 0 "" } { event AE_NPC_MUZZLEFLASH 0 "PISTOL MUZZLE" } { event 6001 0 "0" }',
        '$collisionmodel "w_pistol_phys.smd"\n{\n\t$mass 1.3\n}',
    ]
    wqc_path = os.path.join(SRC_DIR, "w_pistol.qc")
    with open(wqc_path, "w", encoding="ascii", newline="\n") as f:
        f.write("// make_csgo_weapons.py: HL2RPM HK USP Match worldmodel (replaces stock w_pistol)\n" + "\n".join(wqc) + "\n")

    mat_tex = make_usp_materials(v)
    snd_txt = make_usp_sounds(v)
    write_pistol_script(fov=int(round(fov)))

    rc1, txt1, bad1 = C.dec.compile_qc(vqc_path)
    print("v_pistol (USP Match): %s" % ("OK" if rc1 == 0 else "FAILED: " + " | ".join(bad1[:5])))
    if rc1 != 0:
        print(txt1[-2000:])
        raise RuntimeError("v_pistol compile failed")

    rc2, txt2, bad2 = C.dec.compile_qc(wqc_path)
    print("w_pistol (USP Match): %s" % ("OK" if rc2 == 0 else "FAILED: " + " | ".join(bad2[:5])))
    if rc2 != 0:
        print(txt2[-2000:])
        raise RuntimeError("w_pistol compile failed")

    if preview_dir:
        shots = [("idle", 0), ("fire", 2), ("idle_empty", 0), ("reload", 16), ("reload", 36), ("draw", 12)]
        C.render_fp_preview(
            fs, os.path.join(preview_dir, "usp_fp_citizen.png"), shots, allframes, tris_all,
            {"usp_body": (58, 60, 64), "usp_slide": (188, 192, 198), "usp_match_steel": (68, 72, 78), "usp_match_sights": (36, 38, 42)},
            muzzle, "usp", hands="models/hl2rpm/hands/citizen.mdl", fov=fov,
        )
        C.render_fp_preview(
            fs, os.path.join(preview_dir, "usp_fp_hev.png"), [("idle", 0), ("fire", 2), ("reload", 24)], allframes, tris_all,
            {"usp_body": (58, 60, 64), "usp_slide": (188, 192, 198), "usp_match_steel": (68, 72, 78), "usp_match_sights": (36, 38, 42)},
            muzzle, "usp", hands="models/hl2rpm/hands/hev.mdl", fov=fov,
        )
        bone_to_parent = {k: C.inv(Wb_s[gi["usp"]]) @ Wb_s[gi[k]] for k, _ in GUN_PARTS}
        C.render_gun_turntable(
            os.path.join(preview_dir, "usp_turntable.png"), tris_all, bone_to_parent, mat_tex, title="HK USP Match (HL2 Style)"
        )

    return snd_txt
