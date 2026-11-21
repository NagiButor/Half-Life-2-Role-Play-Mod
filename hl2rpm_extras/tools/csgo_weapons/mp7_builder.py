"""Build the HL2RPM MP7 (replacing stock HL2 weapon_smg1: v_smg1.mdl + w_smg1.mdl):
- Source mesh & animations: CS:GO legacy v_smg_mp7 (MDL v49 + .ani STUDIO_FRAMEANIM)
- ValveBiped c_arms retargeting (works with all 8 HL2RPM suit hand models via uses_hands=1)
- Authentic HL2 Combine Claw emblem + CMB stencil painted on both left & right receiver panels
- Deferred PBR textures (VTF v7.2 DXT1 albedo + DXT5 DX normal map with PBR gloss in alpha)
- High-detail Tactical Holographic Sight mounted on the top Picatinny rail (with $mostlyopaque
  two-pass optical lens + glowing additive red 65-MOA holographic reticle)
- CS:GO MP7 sounds (mono 3D gunfire + synchronized handling foley) + scripts/weapon_smg1.txt
"""
import math
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

from . import csgo_core as C

SRC_DIR = os.path.join(C.SRC_ROOT, "mp7")
MAT_DIR_REL = "models/weapons/hl2rpm/mp7"
SND_DIR_REL = "weapons/hl2rpm_mp7"

FONT_BOLD = r"C:\Windows\Fonts\arialbd.ttf"


# ------------------------------------------------------------------ Combine Claw Emblem stencil
def render_combine_emblem(size=512):
    """Render an ultra-crisp, authentic Half-Life 2 Combine Claw emblem (matches HL2 decal_posters005a)
    into a float mask (0..1, size x size). Solid central orb gripped by asymmetric angled two-pronged claw."""
    ss = size * 2
    img = Image.new("L", (ss, ss), 0)
    dr = ImageDraw.Draw(img)
    cx, cy = ss * 0.52, ss * 0.56
    r_orb = ss * 0.165
    r_gap = ss * 0.225
    r_out = ss * 0.345

    # Outer angular claw polygon (classic HL2 Combine silhouette: boxy notched back + two dramatic prongs up-left)
    # Drawn in normalized coordinates around (cx, cy), rotated so prongs point up-left (~135 deg)
    def pt(nx, ny):
        return (cx + nx * ss, cy + ny * ss)

    # Thick base + right/bottom heavy collar + long upper prong + shorter lower-left prong
    claw_poly = [
        pt(-0.34, -0.40),  # tip of long upper prong (sharp blade tip)
        pt(0.05, -0.24),   # upper-right shoulder bevel
        pt(0.30, -0.06),   # outer right chamfer top
        pt(0.34, 0.12),    # outer right flat
        pt(0.24, 0.30),    # outer bottom-right bevel
        pt(0.00, 0.36),    # outer bottom flat
        pt(-0.24, 0.28),   # outer bottom-left corner
        pt(-0.40, 0.04),   # tip of shorter lower-left prong
        pt(-0.42, -0.14),  # sharp inner point of lower prong
        pt(-0.18, 0.08),   # inner throat lower
        pt(0.06, 0.18),    # inner throat bottom
        pt(0.18, 0.04),    # inner throat right
        pt(0.10, -0.10),   # inner throat upper
        pt(-0.16, -0.22),  # inner edge of long upper prong
    ]
    dr.polygon(claw_poly, fill=255)

    # Punch clean circular gap around the central orb so the claw hugs it with a crisp concentric margin
    dr.ellipse((cx - r_gap, cy - r_gap, cx + r_gap, cy + r_gap), fill=0)

    # Solid central Combine orb
    dr.ellipse((cx - r_orb, cy - r_orb, cx + r_orb, cy + r_orb), fill=255)

    # Subtle angular notch cut on the back-right edge (authentic Combine graphic detail)
    notch = [
        pt(0.27, 0.02),
        pt(0.37, -0.01),
        pt(0.37, 0.09),
        pt(0.27, 0.07),
    ]
    dr.polygon(notch, fill=0)

    out = img.resize((size, size), Image.LANCZOS)
    return np.asarray(out, dtype=np.float32) / 255.0


def render_cmb_stencil_block(w=276, h=110, flip_claw_x=False):
    """Compose Combine Claw emblem + crisp tactical 'CMB' + 'C27 // OW' stencil block (0..1 float mask)."""
    mask = np.zeros((h, w), dtype=np.float32)
    em_sz = min(h - 10, 96)
    em = render_combine_emblem(em_sz)
    if flip_claw_x:
        em = em[:, ::-1]
    ex = 8
    ey = (h - em_sz) // 2
    mask[ey : ey + em_sz, ex : ex + em_sz] = np.maximum(mask[ey : ey + em_sz, ex : ex + em_sz], em)

    # Crisp tactical stencil lettering next to emblem (fits comfortably with generous right margin!)
    txt_im = Image.new("L", (w, h), 0)
    dr = ImageDraw.Draw(txt_im)
    f_big = ImageFont.truetype(FONT_BOLD, 40)
    f_sub = ImageFont.truetype(FONT_BOLD, 18)
    tx = ex + em_sz + 14
    dr.rectangle((tx, ey + 6, w - 12, ey + 11), fill=220)
    dr.text((tx, ey + 16), "CMB", font=f_big, fill=255)
    dr.text((tx + 2, ey + 62), "C27 // OW", font=f_sub, fill=220)
    dr.rectangle((tx, ey + 86, w - 12, ey + 90), fill=185)
    t_arr = np.asarray(txt_im, dtype=np.float32) / 255.0
    mask = np.maximum(mask, t_arr)
    return mask


# ------------------------------------------------------------------ MP7 Holographic Sight geometry
def build_holosight_geometry(mb, bone="mp7"):
    """Build a detailed tactical Combine/EOTech-style Holographic Sight mounted on the MP7 top
    Picatinny rail (rail top Y=4.207, width X=[-0.45..+0.45], clear Z=[-2.0..+5.0])."""
    H = "mp7_holo"
    G = "mp7_holo_glass"
    R = "mp7_holo_reticle"

    # UV regions inside 1024x1024 mp7_holo texture atlas:
    UV_BODY = (0.04, 0.04, 0.46, 0.46)     # pure bead-blasted matte dark anodized aluminum
    UV_SIDE = (0.54, 0.78, 0.74, 0.96)     # clean side-hood subtle markings
    UV_RAIL = (0.52, 0.04, 0.96, 0.46)     # darker satin steel mount & clamp
    UV_CTRL = (0.04, 0.52, 0.46, 0.96)     # rear emitter control panel (+/- buttons, markings)
    UV_DIAL = (0.52, 0.52, 0.74, 0.74)     # machined windage/elevation turret dial
    UV_LED = (0.78, 0.54, 0.94, 0.70)      # Combine cyan status LED indicator
    UV_DARK = (0.78, 0.78, 0.95, 0.95)     # deep matte anti-glare inner bezel

    # 1. Low-profile folded backup iron sight caps neatly covering trimmed CS:GO rail sight stubs
    mb.box(H, bone, -0.44, 0.44, 4.18, 4.44, -3.45, -2.75, UV_RAIL)
    mb.box(H, bone, -0.34, 0.34, 4.42, 4.50, -3.32, -2.92, UV_DARK)
    mb.box(H, bone, -0.44, 0.44, 4.18, 4.44, 5.82, 6.52, UV_RAIL)
    mb.box(H, bone, -0.30, 0.30, 4.42, 4.50, 5.98, 6.38, UV_DARK)

    # 2. Picatinny Rail Mount Base (clamping tightly around rail X=+-0.45, Y=4.06..4.58, Z=-1.60..+2.42)
    def base_ring(z, xw=0.56, y0=4.08, y1=4.58):
        return [
            (-xw + 0.07, y0, z),
            (xw - 0.07, y0, z),
            (xw, y0 + 0.08, z),
            (xw, y1 - 0.06, z),
            (xw - 0.06, y1, z),
            (-xw + 0.06, y1, z),
            (-xw, y1 - 0.06, z),
            (-xw, y0 + 0.08, z),
        ]

    mb.loft_z(H, bone, [
        base_ring(-1.62, 0.50, 4.14, 4.54),
        base_ring(-1.48, 0.56, 4.08, 4.58),
        base_ring(2.30, 0.56, 4.08, 4.58),
        base_ring(2.44, 0.50, 4.14, 4.54),
    ], UV_RAIL)

    # Left-side (+X) QD throw lever & knurled locking nut (facing player!)
    mb.box(H, bone, 0.55, 0.67, 4.18, 4.46, -0.35, 0.95, UV_RAIL)
    mb.box(H, bone, 0.64, 0.72, 4.22, 4.42, -0.75, -0.15, UV_DIAL)  # lever handle
    mb.cylinder_axis(H, bone, (0.55, 4.32, 0.55), (0.71, 4.32, 0.55), 0.12, 0.11, 14, UV_DIAL)
    # Right-side clamp recoil bolts
    for zb in (-0.85, 0.45, 1.75):
        mb.cylinder_axis(H, bone, (-0.54, 4.32, zb), (-0.64, 4.32, zb), 0.09, 0.08, 12, UV_DIAL)

    # 3. Forward Battery / Laser Emitter Compartment (Z=+0.45..+2.52, Y=4.56..5.22, chamfered Combine nose)
    def bat_ring(z, xw, y0, y1, ch=0.14):
        return [
            (-xw, y0, z),
            (xw, y0, z),
            (xw, y1 - ch, z),
            (xw - ch, y1, z),
            (-xw + ch, y1, z),
            (-xw, y1 - ch, z),
        ]

    mb.loft_z(H, bone, [
        bat_ring(0.45, 0.62, 4.56, 5.20, 0.14),
        bat_ring(2.05, 0.62, 4.56, 5.18, 0.14),
        bat_ring(2.42, 0.58, 4.56, 5.04, 0.14),
        bat_ring(2.55, 0.48, 4.58, 4.92, 0.10),
    ], UV_BODY)

    # Transverse CR123 battery cap cylinder protruding slightly at front-right (iconic EOTech EXPS detail)
    mb.cylinder_axis(H, bone, (-0.58, 4.86, 1.82), (0.58, 4.86, 1.82), 0.26, 0.26, 18, UV_RAIL)
    mb.cylinder_axis(H, bone, (-0.60, 4.86, 1.82), (-0.70, 4.86, 1.82), 0.22, 0.20, 16, UV_DIAL)
    mb.cylinder_axis(H, bone, (0.58, 4.86, 1.82), (0.68, 4.86, 1.82), 0.22, 0.20, 16, UV_DIAL)

    # 4. Rugged Double-Wall Protective Armored U-Hood (Z=-1.52..+0.78, clear window X=+-0.56, Y=4.76..5.78)
    # Build the hood from clean watertight solid segments:
    # Lower optical deck (between rail base & window bottom, Y=4.56..4.76)
    mb.box(H, bone, -0.70, 0.70, 4.56, 4.76, -1.52, 0.72, UV_BODY)

    # Left & Right armored hood sidewalls (sleek tapered profile in Z-Y, extruded in X!)
    # Outer wall silhouette in (z, y): slightly swept forward at top, angled rear chamfer
    side_poly = [
        (-1.52, 4.58),
        (0.76, 4.58),
        (0.76, 5.72),
        (0.54, 5.96),
        (-1.26, 5.96),
        (-1.52, 5.74),
    ]
    # Left wall (+X: +0.58 to +0.76) and Right wall (-X: -0.76 to -0.58)
    mb.extrude_x(H, bone, side_poly, 0.58, 0.76, UV_BODY)
    mb.extrude_x(H, bone, side_poly, -0.76, -0.58, UV_BODY)

    # Top protective hood roof bridge spanning between left & right walls (Y=5.78..5.98, chamfered top edges)
    def top_ring(z, xw=0.72, y0=5.78, y1=5.98):
        return [
            (-xw + 0.04, y0, z),
            (xw - 0.04, y0, z),
            (xw, y0 + 0.04, z),
            (xw - 0.14, y1, z),
            (-xw + 0.14, y1, z),
            (-xw, y0 + 0.04, z),
        ]

    mb.loft_z(H, bone, [
        top_ring(-1.32, 0.70, 5.78, 5.96),
        top_ring(-1.20, 0.74, 5.78, 5.98),
        top_ring(0.50, 0.74, 5.78, 5.98),
        top_ring(0.64, 0.70, 5.78, 5.96),
    ], UV_BODY)

    # Inner recessed dark optical bezel frame (gives crisp double-layer depth inside the armored hood!)
    mb.box(H, bone, 0.54, 0.59, 4.74, 5.79, -1.18, 0.54, UV_DARK)
    mb.box(H, bone, -0.59, -0.54, 4.74, 5.79, -1.18, 0.54, UV_DARK)
    mb.box(H, bone, -0.56, 0.56, 5.74, 5.80, -1.18, 0.54, UV_DARK)
    mb.box(H, bone, -0.56, 0.56, 4.73, 4.79, -1.18, 0.54, UV_DARK)

    # Side armored hood reinforcement ribs & recessed relief accents
    for sgn in (-1.0, 1.0):
        x0, x1 = (0.75, 0.80) if sgn > 0 else (-0.80, -0.75)
        mb.box(H, bone, x0, x1, 4.72, 5.66, -1.32, -1.10, UV_RAIL)
        mb.box(H, bone, x0, x1, 4.72, 5.66, 0.34, 0.56, UV_RAIL)
        # Small top hood mounting screws
        mb.cylinder_axis(H, bone, (sgn * 0.74, 5.32, -0.38), (sgn * 0.81, 5.32, -0.38), 0.08, 0.07, 12, UV_DIAL)

    # 5. Rear Emitter Control Panel facing the shooter (Z = -1.52..-1.62)
    mb.box(H, bone, -0.54, 0.54, 4.54, 4.76, -1.62, -1.50, UV_CTRL)
    # Raised tactile brightness buttons [-], [NV], [+]
    mb.box(H, bone, 0.18, 0.38, 4.59, 4.71, -1.66, -1.61, UV_DARK)
    mb.box(H, bone, -0.10, 0.10, 4.59, 4.71, -1.66, -1.61, UV_DARK)
    mb.box(H, bone, -0.38, -0.18, 4.59, 4.71, -1.66, -1.61, UV_DARK)
    # Glowing Combine tactical cyan power status LED strip on rear-left of hood!
    mb.box(H, bone, 0.59, 0.72, 4.62, 4.70, -1.57, -1.51, UV_LED)

    # 6. Right-side Windage & Elevation Adjustment Turrets (-X side)
    mb.cylinder_axis(H, bone, (-0.75, 4.96, -0.72), (-0.85, 4.96, -0.72), 0.13, 0.12, 14, UV_DIAL)
    mb.cylinder_axis(H, bone, (-0.75, 4.96, -0.08), (-0.85, 4.96, -0.08), 0.13, 0.12, 14, UV_DIAL)

    # 7. Translucent Anti-Reflective Optical Lens Glass (Z = +0.34, strictly inside inner bezel)
    # Slight tilt like real holosight beam-splitter glass!
    mb.quad(G, bone, (-0.54, 4.79, 0.36), (0.54, 4.79, 0.36), (0.54, 5.74, 0.28), (-0.54, 5.74, 0.28),
            (0.0, 0.0, 1.0, 1.0), C.norm((0.0, -0.08, -1.0)))

    # 8. Glowing Holographic Reticle Plane (Z = -0.96, inside inner bezel -1.18..+0.54,
    # slightly biased X=-0.06 toward shooter's hip-fire sightline so the full 65-MOA ring
    # clears the left armored hood wall from first-person view while staying centered in 3D/world!)
    mb.quad(R, bone, (0.36, 4.82, -0.96), (-0.48, 4.82, -0.96), (-0.48, 5.70, -0.96), (0.36, 5.70, -0.96),
            (0.0, 0.0, 1.0, 1.0), (0.0, 0.0, -1.0))


# ------------------------------------------------------------------ MP7 Deferred PBR Textures
def make_mp7_materials(v):
    out_dir = os.path.join(C.MOD, "materials", *MAT_DIR_REL.split("/"))
    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(SRC_DIR, exist_ok=True)

    # 1. Decode CS:GO smg_mp7 diffuse+alpha (2048) and exponent (512 -> 2048)
    raw_diff = v.read("materials/models/weapons/v_models/smg_mp7/smg_mp7.vtf")
    raw_exp = v.read("materials/models/weapons/v_models/smg_mp7/smg_mp7_exponent.vtf")
    im_diff = C.vtf_to_rgba(raw_diff)
    im_exp = C.vtf_to_rgba(raw_exp).resize(im_diff.size, Image.BILINEAR)

    diff = np.asarray(im_diff, dtype=np.float32)
    rgb = diff[..., :3] / 255.0
    csgo_alpha = diff[..., 3] / 255.0
    exp_arr = np.asarray(im_exp, dtype=np.float32) / 255.0
    exp_r = exp_arr[..., 0]  # phong exponent
    exp_g = exp_arr[..., 1]  # bare metal / rail / hardware mask!

    # Re-grade CS:GO's very dark diffuse for HL2RPM deferred PBR (keep rich tactical charcoal,
    # lift crushed blacks & edge readability so details pop in both sunlight and shade)
    rgb_pbr = np.power(np.clip(rgb, 0.0, 1.0), 0.75) * 1.24 + 0.045
    # Bare metal parts (rails, bolt, barrel, screws: exp_g > 0.5) get slightly cooler steel lift
    metal_mask = np.clip(exp_g, 0.0, 1.0)
    rgb_pbr[..., 0] += metal_mask * 0.045
    rgb_pbr[..., 1] += metal_mask * 0.052
    rgb_pbr[..., 2] += metal_mask * 0.068
    rgb_pbr = np.clip(rgb_pbr, 0.0, 1.0)

    # Synthesize per-texel PBR gloss (0..1) stored in normal map alpha:
    # Polymer receiver/grip: ~0.30..0.42 (satin polymer)
    # Metal rails/barrel/bolt (exp_g): ~0.58..0.78 (anodized/blued steel)
    # CS:GO edge scratches/wear (csgo_alpha): boosts specular gloss cleanly to ~0.82
    gloss = 0.28 + 0.30 * metal_mask + 0.25 * np.clip(exp_r * 3.0, 0.0, 1.0) + 0.32 * np.sqrt(np.clip(csgo_alpha, 0.0, 1.0))
    gloss = np.clip(gloss, 0.16, 0.88)

    # Synthesize subtle micro-surface normal map from luminance + metal seams
    luma = 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]
    blur_l = np.asarray(Image.fromarray((np.clip(luma, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(3.0)), dtype=np.float32) / 255.0
    height = np.clip(0.5 + (luma - blur_l) * 1.6 + csgo_alpha * 0.12, 0.0, 1.0)

    # Paint authentic HL2 Combine Claw Emblem + CMB stencil onto BOTH upper receiver side panels!
    # From UV probe:
    #   PanelB_left  (+X facing player in FPS): x=43..788, y=666..895 (du/dZ < 0 => left is forward, top is +Y up)
    #     Oval takedown pin holes at x~265 and x~545, y~775 -> clean flat zone x=290..515, y=695..850
    #     PLUS secondary flat rear stock-receiver zone x=575..745, y=700..850
    #   PanelC_right (-X worldmodel right side): x=811..1556, y=657..897 (du/dZ > 0 => right is forward, top is +Y up)
    #     Oval takedown pin holes at x~1035 and x~1310, y~775 -> clean flat zone x=1065..1285, y=695..850
    rng = np.random.default_rng(2707)
    wear_noise = np.asarray(
        Image.fromarray((rng.uniform(0, 255, (512, 512))).astype(np.uint8))
        .filter(ImageFilter.GaussianBlur(1.2))
        .resize((2048, 2048), Image.BILINEAR),
        dtype=np.float32,
    ) / 255.0

    stencil_l = render_cmb_stencil_block(w=276, h=110, flip_claw_x=False)
    stencil_r = render_cmb_stencil_block(w=276, h=110, flip_claw_x=True)

    # Combine Overwatch tactical off-white / cool steel-blue paint color
    paint_col = np.array([0.88, 0.92, 0.96], dtype=np.float32)

    def stamp_stencil(st_mask, x0, y0):
        sh, sw = st_mask.shape
        # Realistic micro-chipping & existing weapon surface wear bleeding through the stencil
        w_sub = wear_noise[y0 : y0 + sh, x0 : x0 + sw]
        s_sub = csgo_alpha[y0 : y0 + sh, x0 : x0 + sw]
        chip = np.clip((w_sub - 0.16) * 2.2, 0.40, 1.0) * (1.0 - np.clip(s_sub * 1.4, 0.0, 0.72))
        a = np.clip(st_mask * chip * 0.92, 0.0, 1.0)
        rgb_pbr[y0 : y0 + sh, x0 : x0 + sw] = (
            rgb_pbr[y0 : y0 + sh, x0 : x0 + sw] * (1.0 - a[..., None]) + paint_col[None, None, :] * a[..., None]
        )
        # Matte stencil paint sits slightly duller than brushed metal & adds faint raised paint edge
        gloss[y0 : y0 + sh, x0 : x0 + sw] = gloss[y0 : y0 + sh, x0 : x0 + sw] * (1.0 - a * 0.45) + 0.24 * (a * 0.45)
        height[y0 : y0 + sh, x0 : x0 + sw] = np.clip(height[y0 : y0 + sh, x0 : x0 + sw] + a * 0.06, 0.0, 1.0)

    # Stamp prominently on LEFT receiver (directly visible in first person!)
    stamp_stencil(stencil_l, 272, 696)
    # Stamp matching upright Combine Claw + CMB on RIGHT receiver (PanelC_right)
    stamp_stencil(stencil_r, 1052, 696)

    rgb_u8 = np.clip(np.rint(rgb_pbr * 255.0), 0, 255).astype(np.uint8)
    nrm_u8 = C.height_to_dx_normal(height, strength=2.4)
    gloss_u8 = np.clip(np.rint(gloss * 255.0), 0, 255).astype(np.uint8)

    Image.fromarray(rgb_u8).save(os.path.join(SRC_DIR, "mp7_body_base.png"))
    Image.fromarray(nrm_u8).save(os.path.join(SRC_DIR, "mp7_body_normal.png"))
    Image.fromarray(gloss_u8).save(os.path.join(SRC_DIR, "mp7_body_gloss.png"))

    C.write_albedo_vtf(os.path.join(out_dir, "mp7_body.vtf"), rgb_u8)
    C.write_normal_gloss_vtf(os.path.join(out_dir, "mp7_body_n.vtf"), nrm_u8, gloss_u8)
    C.write_pbr_vmt(
        os.path.join(out_dir, "mp7_body.vmt"),
        f"{MAT_DIR_REL}/mp7_body",
        f"{MAT_DIR_REL}/mp7_body_n",
        metalness=0.36,
        specular=1.05,
        phong_exp=26,
        phong_boost=1.4,
    )

    # 2. Holographic Sight Housing PBR Atlas (1024x1024): anodized aluminum + steel rail + rear controls + cyan LED
    S = 1024
    h_rgb = np.full((S, S, 3), (58, 62, 68), dtype=np.float32)
    h_height = np.full((S, S), 0.5, dtype=np.float32)
    h_gloss = np.full((S, S), 0.44, dtype=np.float32)
    h_illum = np.zeros((S, S, 3), dtype=np.uint8)

    # Fine bead-blasted anodized aluminum micro-grain across entire atlas
    grain = rng.normal(0.0, 1.0, (S, S)).astype(np.float32)
    h_rgb += grain[..., None] * 2.2
    h_height += grain * 0.025

    # Region 1 (0..512, 0..512): Pure clean bead-blasted anodized aluminum hood & body (no rotated text wrapping!)
    im_h = Image.fromarray(np.clip(h_rgb, 0, 255).astype(np.uint8))
    dr = ImageDraw.Draw(im_h)
    dr.rectangle((20, 20, 492, 492), outline=(76, 82, 90), width=2)

    # Region 2 (512..1024, 0..512): Darker parkerized steel mount & QD clamp
    dr.rectangle((512, 0, 1024, 512), fill=(46, 50, 56))
    for y in range(24, 490, 28):
        dr.line([(530, y), (1000, y)], fill=(62, 67, 74), width=2)
    h_gloss[0:512, 512:1024] = 0.58

    # Region 3 (0..512, 512..1024): Rear control panel (- / NV / + tactile icons)
    dr.rectangle((0, 512, 512, 1024), fill=(48, 52, 58))
    dr.rectangle((36, 548, 476, 988), outline=(82, 88, 96), width=4)
    dr.text((100, 720), "[-]    NV    [+]", fill=(205, 215, 225), font=ImageFont.truetype(FONT_BOLD, 42))
    h_gloss[512:1024, 0:512] = 0.32

    # Region 4 (512..768, 512..768): Knurled metallic turret dials & bolts
    dr.rectangle((512, 512, 768, 768), fill=(84, 88, 96))
    for x in range(520, 764, 10):
        dr.line([(x, 516), (x, 764)], fill=(56, 60, 66), width=3)
    dr.ellipse((576, 576, 704, 704), outline=(126, 132, 140), width=5)
    dr.rectangle((592, 634, 688, 646), fill=(28, 30, 34))  # screw slot
    h_gloss[512:768, 512:768] = 0.74

    # Region 5 (768..1024, 512..768): Glowing Combine cyan power LED indicator
    dr.rectangle((768, 512, 1024, 768), fill=(48, 225, 255))
    h_illum[520:760, 776:1016] = (65, 235, 255)
    h_gloss[512:768, 768:1024] = 0.90

    # Region 6 (768..1024, 768..1024): Deep matte anti-glare inner optic bezel
    dr.rectangle((768, 768, 1024, 1024), fill=(30, 32, 36))
    h_gloss[768:1024, 768:1024] = 0.20

    h_rgb_u8 = np.clip(np.asarray(im_h, dtype=np.float32) + grain[..., None] * 1.8, 0, 255).astype(np.uint8)
    # Build height for holosight normal map
    hluma = (0.3 * h_rgb_u8[..., 0] + 0.59 * h_rgb_u8[..., 1] + 0.11 * h_rgb_u8[..., 2]) / 255.0
    h_nrm_u8 = C.height_to_dx_normal(np.clip(h_height + (hluma - 0.2) * 0.35, 0.0, 1.0), strength=2.6)
    h_gloss_u8 = np.clip(np.rint(h_gloss * 255.0), 0, 255).astype(np.uint8)

    Image.fromarray(h_rgb_u8).save(os.path.join(SRC_DIR, "mp7_holo_base.png"))
    C.write_albedo_vtf(os.path.join(out_dir, "mp7_holo.vtf"), h_rgb_u8)
    C.write_normal_gloss_vtf(os.path.join(out_dir, "mp7_holo_n.vtf"), h_nrm_u8, h_gloss_u8)
    C.write_albedo_vtf(os.path.join(out_dir, "mp7_holo_illum.vtf"), h_illum)
    C.write_pbr_vmt(
        os.path.join(out_dir, "mp7_holo.vmt"),
        f"{MAT_DIR_REL}/mp7_holo",
        f"{MAT_DIR_REL}/mp7_holo_n",
        metalness=0.52,
        specular=1.05,
        phong_exp=28,
        phong_boost=1.6,
        selfillum_mask=f"{MAT_DIR_REL}/mp7_holo_illum",
        selfillum_tint="[0.6 1.5 1.8]",
    )

    # 3. Anti-reflective optical glass lens (256x256, additive subtle cool blue-cyan edge sheen)
    G = 256
    yy, xx = np.mgrid[0:G, 0:G].astype(np.float32) / (G - 1)
    # Subtle diagonal reflection streak + soft edge vignette
    edge = np.maximum(np.abs(xx - 0.5), np.abs(yy - 0.5)) * 2.0
    streak = np.exp(-((xx * 0.8 + yy * 0.6 - 0.55) / 0.16) ** 2) * 0.45
    g_int = np.clip(0.05 + 0.14 * (edge ** 2.4) + 0.09 * streak, 0.0, 0.32)
    g_rgb = np.stack([g_int * 40.0, g_int * 155.0, g_int * 210.0], axis=-1)
    g_a = np.clip(g_int * 255.0, 0, 255)
    C.write_albedo_vtf(os.path.join(out_dir, "mp7_holo_glass.vtf"), np.clip(g_rgb, 0, 255).astype(np.uint8),
                       g_a.astype(np.uint8), flags=0x10 | 0x4 | 0x8)
    with open(os.path.join(out_dir, "mp7_holo_glass.vmt"), "w", encoding="ascii", newline="\n") as f:
        f.write(
            '"UnlitGeneric"\n{\n'
            f'\t"$basetexture"\t"{MAT_DIR_REL}/mp7_holo_glass"\n'
            '\t"$additive"\t\t"1"\n'
            '\t"$translucent"\t"1"\n'
            '\t"$nocull"\t\t"1"\n'
            '\t"$nofog"\t\t"1"\n'
            '\t"$model"\t\t"1"\n'
            '}\n'
        )

    # 4. Crisp Glowing Holographic Reticle (512x512, additive bright EOTech 65-MOA ring + ticks + dot)
    R = 512
    ret_core = Image.new("L", (R, R), 0)
    dr = ImageDraw.Draw(ret_core)
    rc = R // 2
    rad = 94
    # Main crisp 65-MOA holographic ring
    dr.ellipse((rc - rad, rc - rad, rc + rad, rc + rad), outline=255, width=7)
    # 4 cardinal tick marks (extending slightly inward & outward from the ring, iconic EOTech reticle!)
    tk_in, tk_out, tk_w = 72, 114, 7
    dr.rectangle((rc - tk_w // 2, rc - tk_out, rc + tk_w // 2, rc - tk_in), fill=255)
    dr.rectangle((rc - tk_w // 2, rc + tk_in, rc + tk_w // 2, rc + tk_out), fill=255)
    dr.rectangle((rc - tk_out, rc - tk_w // 2, rc - tk_in, rc + tk_w // 2), fill=255)
    dr.rectangle((rc + tk_in, rc - tk_w // 2, rc + tk_out, rc + tk_w // 2), fill=255)
    # Razor-sharp central aiming dot
    dr.ellipse((rc - 9, rc - 9, rc + 9, rc + 9), fill=255)

    core_f = np.asarray(ret_core, dtype=np.float32) / 255.0
    glow1 = np.asarray(ret_core.filter(ImageFilter.GaussianBlur(5.0)), dtype=np.float32) / 255.0
    glow2 = np.asarray(ret_core.filter(ImageFilter.GaussianBlur(14.0)), dtype=np.float32) / 255.0

    # Hot holographic red-orange core with crimson bloom halo (high contrast in both sky & darkness!)
    rr = np.clip(core_f * 1.0 + glow1 * 0.90 + glow2 * 0.35, 0.0, 1.0)
    gg = np.clip(core_f * 0.38 + glow1 * 0.14 + glow2 * 0.03, 0.0, 1.0)
    bb = np.clip(core_f * 0.16 + glow1 * 0.04, 0.0, 1.0)
    # Extra hot center dot
    yy, xx = np.mgrid[0:R, 0:R].astype(np.float32)
    dot_hot = np.exp(-((xx - rc) ** 2 + (yy - rc) ** 2) / (2.0 * 6.0 ** 2))
    gg = np.clip(gg + dot_hot * 0.55, 0.0, 1.0)
    bb = np.clip(bb + dot_hot * 0.30, 0.0, 1.0)

    ret_rgb = np.stack([rr * 255.0, gg * 255.0, bb * 255.0], axis=-1).astype(np.uint8)
    ret_a = np.clip(np.maximum(core_f, glow1 * 0.85 + glow2 * 0.4) * 255.0, 0, 255).astype(np.uint8)
    Image.fromarray(ret_rgb).save(os.path.join(SRC_DIR, "mp7_holo_reticle.png"))
    C.write_albedo_vtf(os.path.join(out_dir, "mp7_holo_reticle.vtf"), ret_rgb, ret_a, flags=0x10 | 0x4 | 0x8)
    with open(os.path.join(out_dir, "mp7_holo_reticle.vmt"), "w", encoding="ascii", newline="\n") as f:
        f.write(
            '"UnlitGeneric"\n{\n'
            f'\t"$basetexture"\t"{MAT_DIR_REL}/mp7_holo_reticle"\n'
            '\t"$additive"\t\t"1"\n'
            '\t"$translucent"\t"1"\n'
            '\t"$nocull"\t\t"1"\n'
            '\t"$nofog"\t\t"1"\n'
            '\t"$model"\t\t"1"\n'
            '}\n'
        )
    return {"mp7_body": rgb_u8, "mp7_holo": h_rgb_u8}


# ------------------------------------------------------------------ Sounds & Script
MP7_SOUNDS = {
    # script suffix: (csgo wav path, channel, vol, level, mono, spatial_prefix)
    "Single": ("sound/weapons/mp7/mp7_01.wav", "CHAN_WEAPON", "0.85", "SNDLVL_GUNFIRE", True, ")"),
    "NPC_Single": ("sound/weapons/mp7/mp7_02.wav", "CHAN_WEAPON", "0.80", "SNDLVL_GUNFIRE", True, "^"),
    "Clipout": ("sound/weapons/mp7/mp7_clipout.wav", "CHAN_ITEM", "0.80", "SNDLVL_NORM", False, ""),
    "Clipin": ("sound/weapons/mp7/mp7_clipin.wav", "CHAN_ITEM", "0.80", "SNDLVL_NORM", False, ""),
    "Slideback": ("sound/weapons/mp7/mp7_slideback.wav", "CHAN_ITEM", "0.80", "SNDLVL_NORM", False, ""),
    "SlideForward": ("sound/weapons/mp7/mp7_slideforward.wav", "CHAN_ITEM", "0.80", "SNDLVL_NORM", False, ""),
    "Draw": ("sound/weapons/mp7/mp7_draw.wav", "CHAN_ITEM", "0.70", "SNDLVL_NORM", False, ""),
}


def make_mp7_sounds(v):
    out_dir = os.path.join(C.MOD, "sound", *SND_DIR_REL.split("/"))
    os.makedirs(out_dir, exist_ok=True)
    # Also extract mp7_03/04 for rich rndwave variety
    for extra in ("sound/weapons/mp7/mp7_03.wav", "sound/weapons/mp7/mp7_04.wav"):
        C.extract_wav(v, extra, os.path.join(out_dir, os.path.basename(extra)), force_mono=True)
    blocks = ["// make_csgo_weapons.py (MP7 / weapon_smg1): Counter-Strike: Global Offensive sounds"]
    for name, (src, chan, vol, lvl, mono, pfx) in MP7_SOUNDS.items():
        base = os.path.basename(src)
        C.extract_wav(v, src, os.path.join(out_dir, base), force_mono=mono)
        if name == "Single":
            blocks.append(
                '"Weapon_RPMMP7.Single"\n{\n'
                '\t"channel"\t\t"CHAN_WEAPON"\n'
                '\t"volume"\t\t"0.85"\n'
                '\t"soundlevel"\t"SNDLVL_GUNFIRE"\n'
                '\t"pitch"\t\t\t"97,103"\n'
                '\t"rndwave"\n\t{\n'
                f'\t\t"wave"\t\t"){SND_DIR_REL}/mp7_01.wav"\n'
                f'\t\t"wave"\t\t"){SND_DIR_REL}/mp7_02.wav"\n'
                f'\t\t"wave"\t\t"){SND_DIR_REL}/mp7_03.wav"\n'
                f'\t\t"wave"\t\t"){SND_DIR_REL}/mp7_04.wav"\n'
                '\t}\n}\n'
            )
        elif name == "NPC_Single":
            blocks.append(
                '"Weapon_RPMMP7.NPC_Single"\n{\n'
                '\t"channel"\t\t"CHAN_WEAPON"\n'
                '\t"volume"\t\t"0.78"\n'
                '\t"soundlevel"\t"SNDLVL_GUNFIRE"\n'
                '\t"pitch"\t\t\t"95,105"\n'
                '\t"rndwave"\n\t{\n'
                f'\t\t"wave"\t\t"^{SND_DIR_REL}/mp7_01.wav"\n'
                f'\t\t"wave"\t\t"^{SND_DIR_REL}/mp7_02.wav"\n'
                '\t}\n}\n'
            )
        else:
            blocks.append(
                f'"Weapon_RPMMP7.{name}"\n{{\n'
                f'\t"channel"\t\t"{chan}"\n'
                f'\t"volume"\t\t"{vol}"\n'
                f'\t"soundlevel"\t"{lvl}"\n'
                f'\t"wave"\t\t\t"{pfx}{SND_DIR_REL}/{base}"\n}}\n'
            )
    return "\n".join(blocks)


def write_smg1_script(fov=68):
    script = f'''// Small Machine Gun 1 -> CS:GO MP7 (Combine Overwatch issue with holographic sight)
// Generated by make_csgo_weapons.py (uses_hands = 1 for ValveBiped c_arms)

WeaponData
{{
	"printname"				"#HL2_SMG1"
	"viewmodel"				"models/weapons/v_smg1.mdl"
	"playermodel"			"models/weapons/w_smg1.mdl"
	"anim_prefix"			"smg2"
	"bucket"				"2"
	"bucket_position"		"0"
	"bucket_360"			"1"
	"bucket_position_360"	"0"
	"viewmodel_fov"			"{fov}"
	"uses_hands"			"1"

	"clip_size"				"45"
	"clip2_size"			"-1"
	"default_clip"			"45"
	"default_clip2"			"-1"

	"primary_ammo"			"SMG1"
	"secondary_ammo"		"SMG1_Grenade"

	"weight"				"3"
	"rumble"				"3"
	"item_flags"			"0"

	SoundData
	{{
		"reload_npc"		"Weapon_SMG1.NPC_Reload"
		"empty"				"Weapon_SMG1.Empty"
		"single_shot"		"Weapon_RPMMP7.Single"
		"single_shot_npc"	"Weapon_RPMMP7.NPC_Single"
		"special1"			"Weapon_SMG1.Special1"
		"special2"			"Weapon_SMG1.Special2"
		"double_shot"		"Weapon_SMG1.Double"
		"burst"				"Weapon_RPMMP7.Single"
	}}

	TextureData
	{{
		"weapon"		{{ "font" "WeaponIcons" "character" "a" }}
		"weapon_s"		{{ "font" "WeaponIconsSelected" "character" "a" }}
		"weapon_small"	{{ "font" "WeaponIconsSmall" "character" "a" }}
		"ammo"			{{ "font" "WeaponIconsSmall" "character" "r" }}
		"ammo2"			{{ "font" "WeaponIconsSmall" "character" "t" }}
		"crosshair"		{{ "font" "Crosshairs" "character" "Q" }}
		"autoaim"		{{ "file" "sprites/crosshairs" "x" "0" "y" "48" "width" "24" "height" "24" }}
	}}
}}
'''
    with open(os.path.join(C.MOD, "scripts", "weapon_smg1.txt"), "w", encoding="ascii", newline="\n") as f:
        f.write(script)


# ------------------------------------------------------------------ Build MP7
def build_mp7(v, fs, preview_dir=None, fov=None, vm_preset="classic"):
    os.makedirs(SRC_DIR, exist_ok=True)
    mdl_path, ani_data = C.extract_csgo_model(v, "models/weapons/v_smg_mp7", SRC_DIR)
    m = C.dec.Model(fs, mdl_path)
    mv = C.dec.Model(fs, C.VB_REF)
    Wv = C.fk_bind(mv)

    scale, ratios = C.compute_arm_scale(m, mv)
    print("MP7 arm scale %.3f (fore, palm, f1, f2: %s)" % (scale, ", ".join("%.2f" % r for r in ratios)))

    low = {b.lower(): i for i, b in enumerate(m.bone_names)}
    seqs = {s["name"]: s for s in m.sequences()}
    Wb_raw = C.fk_bind(m)
    Wb_s = C.scale_world(Wb_raw, scale)

    idle_w = C.sample_csgo_seq(m, ani_data, fs, seqs["idle"], scale)
    retarget = C.CSGORetarget(idle_w[0], Wv, m, mv)

    # Gun bones mapped into clean target names
    GUN_PARTS = (
        ("mp7", "v_weapon.mp7_parent"),
        ("mp7_bolt", "v_weapon.bolt"),
        ("mp7_trigger", "v_weapon.trigger"),
        ("mp7_clip", "v_weapon.clip"),
    )
    gi = {k: low[bn] for k, bn in GUN_PARTS}
    # Any mesh vert bound to flash/shelleject/stattrack/uid goes onto mp7 parent
    bone_map = {gi[k]: k for k, _ in GUN_PARTS}
    for extra in ("v_weapon.flash", "v_weapon.shelleject", "v_weapon.stattrack", "v_weapon.uid"):
        if extra in low:
            bone_map[low[extra]] = "mp7"

    # Extract CS:GO MP7 mesh, neatly trimming the protruding raised vertical iron sight posts (>4.44)
    # so our low-profile folded BUIS + holographic sight sit cleanly with zero obstruction!
    W_par_inv_raw = C.inv(Wb_raw[gi["mp7"]])

    def keep_mp7_tri(tri, P, N, U, dom):
        pts = (W_par_inv_raw[:3, :3] @ P[tri].T).T + W_par_inv_raw[:3, 3]
        if pts[:, 1].max() > 4.44:
            zc = pts[:, 2].mean()
            if zc < -2.3 or zc > 5.2:
                return False
        return True

    tris_gun = C.extract_gun_tris(m, Wb_s, bone_map, scale, "mp7_body", tri_filter=keep_mp7_tri)

    # Build holographic sight & folded BUIS caps in unscaled mp7_parent local space
    parent_to_bone = {k: C.inv(Wb_raw[gi["mp7"]]) @ Wb_raw[gi[k]] for k, _ in GUN_PARTS}
    mb = C.MeshBuilder(parent_to_bone, scale=scale)
    build_holosight_geometry(mb, bone="mp7")
    tris_all = tris_gun + mb.tris
    print("MP7 mesh: %d base tris + %d holosight tris = %d total" % (len(tris_gun), len(mb.tris), len(tris_all)))

    # Viewmodel placement = CS:GO viewmodel preset (default "classic": viewmodel_fov 54,
    # offset_x 2.5 right, offset_y 0, offset_z -1.5 down), eye space +X fwd, +Y left, +Z up
    VM_SHIFT, preset_fov = C.vm_preset_shift(vm_preset, scale)
    if fov is None:
        fov = preset_fov
    print("MP7 viewmodel preset '%s': fov %.0f, eye offset %s" % (vm_preset, fov, np.round(VM_SHIFT[:3, 3], 2)))

    def convert_frames(Wlist, extra_M=None):
        out = []
        for Wc in Wlist:
            Moff = VM_SHIFT if extra_M is None else (extra_M @ VM_SHIFT)
            f = retarget.frame_bones(Wc, offset_M=Moff)
            for k, _ in GUN_PARTS:
                f[k] = Moff @ Wc[gi[k]]
            out.append(f)
        return out

    def make_kick_frames(base_W, nframes, back, up, pitch_deg):
        out = []
        for i in range(nframes):
            t = i / max(1, nframes - 1)
            # Fast impulse peaking around t=0.18, smooth damped recovery
            k = math.sin(min(1.0, t / 0.18) * math.pi * 0.5) if t < 0.18 else math.exp(-5.0 * (t - 0.18)) * (1.0 - t) / 0.82
            M = np.eye(4)
            M[:3, :3] = C.euler_m((0.0, math.radians(-pitch_deg * k), 0.0))
            M[:3, 3] = np.array([-back * k, 0.0, up * k])
            out.append(convert_frames([base_W], M)[0])
        return out

    def make_lower_frames(base_W, nframes, reverse=False):
        out = []
        for i in range(nframes):
            u = i / max(1, nframes - 1)
            if reverse:
                u = 1.0 - u
            s = u * u * (3.0 - 2.0 * u)
            M = np.eye(4)
            M[:3, :3] = C.euler_m((math.radians(-6.0 * s), math.radians(18.0 * s), math.radians(14.0 * s)))
            M[:3, 3] = np.array([-1.2 * s, 1.4 * s, -4.6 * s])
            out.append(convert_frames([base_W], M)[0])
        return out

    sk = C.arm_skeleton_order() + [
        ("mp7", "ValveBiped.Bip01_R_Hand"),
        ("mp7_bolt", "mp7"),
        ("mp7_trigger", "mp7"),
        ("mp7_clip", "mp7"),
    ]
    names = [n for n, _ in sk]
    parents = [p for _, p in sk]

    idle_f = convert_frames(idle_w)
    ref_frame = idle_f[0]
    C.write_smd_multi(os.path.join(SRC_DIR, "v_smg1_ref.smd"), names, parents, [ref_frame], tris_all, lambda b: ref_frame[b])

    allframes = {}
    qc_seqs = []

    def add_seq(name, frames, fps, act, extra="", events=()):
        allframes[name] = frames
        C.write_smd_multi(os.path.join(SRC_DIR, f"v_smg1_{name}.smd"), names, parents, frames)
        ev = " ".join('{ event %s %d "%s" }' % e for e in events)
        qc_seqs.append('$sequence "%s" "v_smg1_%s.smd" fps %.1f %s activity %s 1 %s' % (name, name, fps, extra, act, ev))

    shoot_w = C.sample_csgo_seq(m, ani_data, fs, seqs["shoot1"], scale)
    reload_w = C.sample_csgo_seq(m, ani_data, fs, seqs["reload"], scale)
    draw_w = C.sample_csgo_seq(m, ani_data, fs, seqs["draw"], scale)

    shot_ev = (("AE_MUZZLEFLASH", 0, "SMG1 MUZZLE"), ("6001", 0, "0"))
    snd = lambda fr, s: ("5004", fr, "Weapon_RPMMP7." + s)

    add_seq("idle", idle_f, 30.0, "ACT_VM_IDLE", "loop")
    add_seq("fire01", convert_frames(shoot_w), 34.0, "ACT_VM_PRIMARYATTACK", "", shot_ev)
    # Slight roll/pitch variation across recoil1..3 so rapid full-auto bursts feel alive!
    for idx_r, (rname, ract, ry, rz) in enumerate((
        ("fire02", "ACT_VM_RECOIL1", -0.8, 0.5),
        ("fire03", "ACT_VM_RECOIL2", -1.4, -0.6),
        ("fire04", "ACT_VM_RECOIL3", -1.8, 0.8),
    )):
        fr_r = []
        for i, Wc in enumerate(shoot_w):
            k = math.exp(-0.28 * i)
            M = np.eye(4)
            M[:3, :3] = C.euler_m((math.radians(rz * k), math.radians(ry * k), 0.0))
            fr_r.append(convert_frames([Wc], M)[0])
        add_seq(rname, fr_r, 34.0, ract, "", shot_ev)

    add_seq("altfire", make_kick_frames(idle_w[0], 18, back=2.6, up=0.9, pitch_deg=7.5), 30.0, "ACT_VM_SECONDARYATTACK")
    add_seq("dryfire", make_kick_frames(idle_w[0], 8, back=0.25, up=0.05, pitch_deg=0.7), 30.0, "ACT_VM_DRYFIRE")

    # Reload (95 frames @ 38 fps ~ 2.5s) with synchronized CS:GO MP7 sound events
    r_evs = [
        snd(9, "Clipout"),
        snd(42, "Clipin"),
        snd(64, "Slideback"),
        snd(70, "SlideForward"),
    ]
    add_seq("reload", convert_frames(reload_w), 38.0, "ACT_VM_RELOAD", "", r_evs)

    # Draw (31 frames @ 32 fps)
    d_evs = [
        snd(0, "Draw"),
        snd(8, "Slideback"),
        snd(13, "SlideForward"),
    ]
    add_seq("draw", convert_frames(draw_w), 32.0, "ACT_VM_DRAW", "", d_evs)

    # Holster (smooth reverse of first half of draw)
    hol_w = list(reversed(draw_w[:18]))
    add_seq("holster", convert_frames(hol_w), 36.0, "ACT_VM_HOLSTER")

    # Lowered weapon animations (for friendly NPC zones)
    low_frames = make_lower_frames(idle_w[0], 12, reverse=False)
    add_seq("idletolow", low_frames, 30.0, "ACT_VM_IDLE_TO_LOWERED")
    add_seq("lowidle", [low_frames[-1], low_frames[-1]], 10.0, "ACT_VM_IDLE_LOWERED", "loop")
    add_seq("lowtoidle", make_lower_frames(idle_w[0], 12, reverse=True), 30.0, "ACT_VM_LOWERED_TO_IDLE")

    # Attachments in scaled mp7 local space (+Z fwd, +Y up, +X left):
    # Muzzle: X along barrel (+Z local), Y left (+X local), Z up (+Y local)
    mz_pos = np.array([0.0, 2.31, 9.85]) * scale
    muzzle = C.frame(np.array([0.0, 0.0, 1.0]), np.array([1.0, 0.0, 0.0]), mz_pos)
    # Shell eject (attachment #2 = "1"): ejects right (-X local), slightly up (+Y local) & slightly back (-Z local)
    ej_pos = np.array([-0.55, 2.38, -0.66]) * scale
    eject_dir = C.norm(np.array([-0.88, 0.42, -0.22]))
    shell = C.frame(eject_dir, C.orth(np.array([0.0, 0.0, 1.0]), eject_dir), ej_pos)

    vqc = [
        '$modelname "weapons/v_smg1.mdl"',
        "$origin 0 0 0 -90",
        "$mostlyopaque",
        '$body "weapon" "v_smg1_ref.smd"',
        f'$cdmaterials "{MAT_DIR_REL}"',
    ]
    vqc += ['$bonemerge "%s"' % b for b in C.VIEW_BONES if b in names]
    vqc += [C.att_line("muzzle", "mp7", muzzle), C.att_line("1", "mp7", shell)]
    vqc += qc_seqs
    vqc_path = os.path.join(SRC_DIR, "v_smg1.qc")
    with open(vqc_path, "w", encoding="ascii", newline="\n") as f:
        f.write("// make_csgo_weapons.py: HL2RPM MP7 viewmodel (replaces stock v_smg1)\n" + "\n".join(vqc) + "\n")

    # ---------------- Worldmodel (w_smg1.mdl) ----------------
    # Match stock HL2 w_smg1's exact aiming orientation in ValveBiped.Bip01_R_Hand space
    # so Combine Soldiers, Metrocops, Citizens, and third-person player aim straight horizontally!
    # Stock HL2 w_smg1 muzzle in R_Hand: fwd=[0.988, -0.006, -0.157], up=[-0.157, -0.033, -0.987]
    fwd_rh = C.norm([0.988, -0.006, -0.157])
    up_rh = C.orth(np.array([-0.157, -0.033, -0.987]), fwd_rh)
    left_rh = np.cross(up_rh, fwd_rh)  # points along -Y in R_Hand (same left-side handedness as FP)
    R_w = np.column_stack([left_rh, up_rh, fwd_rh])  # gun local (+X left, +Y up, +Z fwd) -> R_Hand
    # Place MP7 pistol grip center [0, 0.0, 0.0] squarely inside R_Hand palm ([4.4, -1.5, -3.6])
    rel_gun = np.eye(4)
    rel_gun[:3, :3] = R_w
    rel_gun[:3, 3] = np.array([4.4, -1.5, -3.6]) - R_w @ (np.array([0.0, 0.2, 0.1]) * scale)
    mz_rh = (rel_gun @ muzzle)[:3, 3]
    print("MP7 worldmodel muzzle in R_Hand space: %s (stock HL2 w_smg1 was [17.12, -1.47, -8.18])" % np.round(mz_rh, 2))

    # Build worldmodel skeleton (same moving bones under Weapon_bone so no vertex re-indexing needed)
    wnames = ["ValveBiped.Bip01_R_Hand", "ValveBiped.Weapon_bone", "mp7_bolt", "mp7_trigger", "mp7_clip"]
    wpar = [None, "ValveBiped.Bip01_R_Hand", "ValveBiped.Weapon_bone", "ValveBiped.Weapon_bone", "ValveBiped.Weapon_bone"]
    wb = "ValveBiped.Weapon_bone"
    wrel = {wb: rel_gun}
    for k in ("mp7_bolt", "mp7_trigger", "mp7_clip"):
        wrel[k] = rel_gun @ C.inv(ref_frame["mp7"]) @ ref_frame[k]

    # Map tris bone "mp7" -> "ValveBiped.Weapon_bone" for worldmodel
    wtris = [(mat, [(p, n, uv, wb if b == "mp7" else b) for p, n, uv, b in corners]) for mat, corners in tris_all]

    # Model's own pose (for 3D Inventory Preview Card hl2rpm_itempreview.cpp & CK viewport):
    # Stand UPRIGHT with sights pointing +Z UP, barrel along +X, left Combine logo along +Y!
    # (In NPC/player hands, bonemerge overrides ValveBiped.Bip01_R_Hand so only wrel matters.)
    R_local_to_ground = np.array([
        [0.0, 0.0, 1.0],   # local +Z (barrel) -> world +X
        [1.0, 0.0, 0.0],   # local +X (left)   -> world +Y
        [0.0, 1.0, 0.0],   # local +Y (up)     -> world +Z (UP!)
    ])
    H = np.eye(4)
    H[:3, :3] = R_local_to_ground @ rel_gun[:3, :3].T
    # Center XY and rest bottom strictly at Z=0
    pts_g = np.array([(H @ wrel[b] @ np.r_[p, 1.0])[:3] for _, c in wtris for p, _, _, b in c])
    lo, hi = pts_g.min(0), pts_g.max(0)
    H[:3, 3] = -np.array([(lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5, lo[2]])
    wf = {"ValveBiped.Bip01_R_Hand": H}
    for k, M in wrel.items():
        wf[k] = H @ M

    C.write_smd_multi(os.path.join(SRC_DIR, "w_smg1_ref.smd"), wnames, wpar, [wf], wtris, lambda b: wf[b])
    C.write_smd_multi(os.path.join(SRC_DIR, "w_smg1_idle.smd"), wnames, wpar, [wf])
    # Convex collision hull (opaque body only, excludes translucent reticle/glass planes)
    phys_tris = [t for t in wtris if t[0] in ("mp7_body", "mp7_holo")]
    C.write_phys_hull(os.path.join(SRC_DIR, "w_smg1_phys.smd"), "ValveBiped.Bip01_R_Hand", H, phys_tris, lambda b: wf[b])

    wqc = [
        '$modelname "weapons/w_smg1.mdl"',
        "$origin 0 0 0 -90",
        "$mostlyopaque",
        '$body "weapon" "w_smg1_ref.smd"',
        f'$cdmaterials "{MAT_DIR_REL}"',
        '$surfaceprop "weapon"',
        C.att_line("muzzle", wb, muzzle),
        C.att_line("1", wb, shell),
        '$sequence "idle" "w_smg1_idle.smd" fps 30 activity ACT_VM_IDLE 1',
        '$sequence "attack1" "w_smg1_idle.smd" fps 20 activity ACT_RANGE_ATTACK_SMG1 1 '
        '{ event AE_NPC_MUZZLEFLASH 0 "SMG1 MUZZLE" } { event 3002 0 "" } { event 6001 0 "0" }',
        '$collisionmodel "w_smg1_phys.smd"\n{\n\t$mass 2.4\n}',
    ]
    wqc_path = os.path.join(SRC_DIR, "w_smg1.qc")
    with open(wqc_path, "w", encoding="ascii", newline="\n") as f:
        f.write("// make_csgo_weapons.py: HL2RPM MP7 worldmodel (replaces stock w_smg1)\n" + "\n".join(wqc) + "\n")

    # Materials, sounds, scripts, compile
    mat_tex = make_mp7_materials(v)
    snd_txt = make_mp7_sounds(v)
    write_smg1_script(fov=int(round(fov)))

    rc1, txt1, bad1 = C.dec.compile_qc(vqc_path)
    print("v_smg1 (MP7): %s" % ("OK" if rc1 == 0 else "FAILED: " + " | ".join(bad1[:5])))
    if rc1 != 0:
        print(txt1[-2000:])
        raise RuntimeError("v_smg1 compile failed")

    rc2, txt2, bad2 = C.dec.compile_qc(wqc_path)
    print("w_smg1 (MP7): %s" % ("OK" if rc2 == 0 else "FAILED: " + " | ".join(bad2[:5])))
    if rc2 != 0:
        print(txt2[-2000:])
        raise RuntimeError("w_smg1 compile failed")

    if preview_dir:
        shots = [("idle", 0), ("fire01", 2), ("reload", 18), ("reload", 42), ("reload", 68), ("draw", 10)]
        C.render_fp_preview(
            fs, os.path.join(preview_dir, "mp7_fp_citizen.png"), shots, allframes, tris_all,
            {"mp7_body": (115, 120, 128), "mp7_holo": (70, 76, 86), "mp7_holo_reticle": (255, 60, 40)},
            muzzle, "mp7", hands="models/hl2rpm/hands/citizen.mdl", fov=fov,
        )
        C.render_fp_preview(
            fs, os.path.join(preview_dir, "mp7_fp_cp.png"), [("idle", 0), ("reload", 35), ("draw", 12)], allframes, tris_all,
            {"mp7_body": (115, 120, 128), "mp7_holo": (70, 76, 86), "mp7_holo_reticle": (255, 60, 40)},
            muzzle, "mp7", hands="models/hl2rpm/hands/cp.mdl", fov=fov,
        )
        bone_to_parent = {k: C.inv(Wb_s[gi["mp7"]]) @ Wb_s[gi[k]] for k, _ in GUN_PARTS}
        C.render_gun_turntable(
            os.path.join(preview_dir, "mp7_turntable.png"), tris_all, bone_to_parent, mat_tex, title="MP7 Combine Holosight"
        )

    return snd_txt
