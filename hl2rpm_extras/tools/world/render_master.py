"""City-27 master plan, light cartographic style."""
import json, math, sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import city27_plan as P

OUT = Path(r'E:\SourceModding\HL2RPMSOURCE\hl2rpm_map_developing')
WX0, WX1 = -1620, 1420
WY0, WY1 = -1560, 1290
SC = 1.15
MAPW = int((WX1 - WX0) * SC)
MAPH = int((WY1 - WY0) * SC)
PADL, PADT = 90, 236
PANEL = 980
TABLE_H = 262
W = PADL + MAPW + 56 + PANEL
H = PADT + MAPH + 40 + TABLE_H + 34

def px(p): return (PADL + (p[0] - WX0) * SC, PADT + (WY1 - p[1]) * SC)
def pxf(x, y): return (PADL + (x - WX0) * SC, PADT + (WY1 - y) * SC)

PAPER = '#f3efe4'
INK = '#22302f'
im = Image.new('RGB', (W, H), PAPER)

def F(sz):
    for nm in (r'C:\Windows\Fonts\bahnschrift.ttf', r'C:\Windows\Fonts\seguisb.ttf',
               r'C:\Windows\Fonts\arialbd.ttf'):
        try: return ImageFont.truetype(nm, sz)
        except OSError: pass
    return ImageFont.load_default()

d = ImageDraw.Draw(im)
def txt(xy, s, sz=24, col=INK, anchor='la', plate=False, pc=PAPER):
    f = F(sz)
    if plate:
        bb = d.textbbox(xy, s, font=f, anchor=anchor)
        d.rectangle((bb[0]-4, bb[1]-1, bb[2]+4, bb[3]+1), fill=pc)
    d.text(xy, s, font=f, fill=col, anchor=anchor)

KIND_FILL = {'city': '#cfe0dd', 'belt': '#e8dcbe', 'waste': '#dde0c4', 'nexus': '#f0cdc3'}
KIND_LINE = {'city': '#2e6f74', 'belt': '#a97c2f', 'waste': '#6f7c35', 'nexus': '#b03f28'}

# ---------------------------------------------------------------- heightfield
SUB = 3
hw, hh = W // SUB + 1, H // SUB + 1
gx, gy = np.meshgrid(np.linspace(WX0, WX1, hw), np.linspace(WY1, WY0, hh))
HGT = np.zeros((hh, hw), np.float32)
bumps = [(-1440, 640, 340, 210), (-1230, 1030, 300, 260), (-1540, -240, 270, 170),
         (-1130, -1200, 310, 200), (-1170, -800, 200, 130), (-1300, -500, 220, 100),
         (-770, 1190, 300, 140), (-340, 1230, 260, 120), (610, 1190, 330, 70),
         (1230, -1260, 430, 40), (900, -900, 390, 24)]
for bx, by, br, bh in bumps:
    HGT += bh * np.exp(-(((gx - bx) ** 2 + (gy - by) ** 2) / (br ** 2)))
dune = np.clip(np.sin((gx - 950) / 62.0) * np.cos((gy + 1180) / 88.0), 0, 1)
HGT += dune * 26 * np.exp(-(((gx - 1280) ** 2 + (gy + 1230) ** 2) / 560 ** 2))

# relief wash
tone = np.clip(HGT / 260.0, 0, 1)[..., None]
c0 = np.array([243, 239, 228], np.float32)
c1 = np.array([214, 214, 188], np.float32)
relief = Image.fromarray(np.clip(c0*(1-tone) + c1*tone, 0, 255).astype('uint8')) \
              .resize((W, H), Image.BICUBIC).filter(ImageFilter.GaussianBlur(1.0))
im.paste(relief, (PADL, PADT))
d = ImageDraw.Draw(im)

for lvl in range(15, 300, 15):
    major = (lvl % 75 == 0)
    m = (HGT >= lvl).astype(np.uint8) * 255
    edge = Image.fromarray(m, 'L').resize((W, H), Image.NEAREST).filter(ImageFilter.FIND_EDGES)
    mask = Image.new('L', (W, H), 0)
    mask.paste(edge.point(lambda v: (170 if major else 95) if v > 8 else 0), (PADL, PADT))
    im.paste(Image.new('RGB', (W, H), '#9d9578' if major else '#c2bba1'), (0, 0), mask)
d = ImageDraw.Draw(im)

# ------------------------------------------------------------------ zone tint
for z in P.ZONES:
    m = Image.new('L', (W, H), 0)
    ImageDraw.Draw(m).polygon([px(p) for p in z['poly']], fill=255)
    im.paste(Image.new('RGB', (W, H), KIND_FILL[z['kind']]), (0, 0), m.point(lambda v: int(v * 0.55)))
d = ImageDraw.Draw(im)

# ------------------------------------------------------------------ city mass
city_union = [z['poly'] for z in P.ZONES if z['kind'] == 'city']
cmask = Image.new('L', (W, H), 0)
cmd = ImageDraw.Draw(cmask)
for poly in city_union:
    cmd.polygon([px(p) for p in poly], fill=255)

mass = Image.new('RGB', (W, H), PAPER)
md = ImageDraw.Draw(mass)
osm_path = OUT / 'osm_core.json'
if osm_path.exists():
    els = json.loads(osm_path.read_text(encoding='utf-8'))['elements']
    def gxy(lat, lon):
        x, y = P.real_xy(lat, lon)
        r = math.hypot(x, y)
        if r < 1e-9: return (0.0, 0.0)
        k = P.compress_radius(r) / r
        return (x * k, y * k)
    for e in els:
        g = e.get('geometry')
        if not g: continue
        t = e.get('tags', {})
        pts = [gxy(q['lat'], q['lon']) for q in g]
        if 'building' in t and len(pts) >= 3:
            md.polygon([px(p) for p in pts], fill='#9c9483', outline='#857d6e')
        elif t.get('natural') == 'water' and len(pts) >= 3:
            md.polygon([px(p) for p in pts], fill='#9dc2cd')
    for nm, rpts, cls in P.ROADS:
        md.line([px(p) for p in rpts], fill='#fbf8ee', width=7, joint='curve')
im.paste(mass, (0, 0), cmask)
d = ImageDraw.Draw(im)

# ------------------------------------------------------------------ river
river = P.RIVER_PEST + list(reversed(P.RIVER_BUDA))
d.polygon([px(p) for p in river], fill='#a9cbd5')
d.line([px(p) for p in P.RIVER_PEST], fill='#6d9aa8', width=2)
d.line([px(p) for p in P.RIVER_BUDA], fill='#6d9aa8', width=2)
isl = [(-1146, 330), (-1122, 360), (-1114, 260), (-1126, 160), (-1144, 150), (-1150, 250)]
d.polygon([px(p) for p in isl], fill='#c2cda8', outline='#6d9aa8')

# ------------------------------------------------------------------ set pieces
for nm, x0, y0, n, ang in P.VILLAGES:
    cols = max(2, int(math.sqrt(n * 3)))
    for i in range(n * 3):
        dx = (i % cols) * 26 + rng.normal(0, 2)
        dy = (i // cols) * 20 + rng.normal(0, 2)
        ca, sa = math.cos(ang), math.sin(ang)
        rx = x0 + dx * ca - dy * sa
        ry = y0 + dx * sa + dy * ca
        w_ = 10 + (i * 7) % 5; h_ = 8 + (i * 5) % 4
        a_ = pxf(rx, ry)
        d.rectangle((a_[0], a_[1], a_[0] + w_, a_[1] + h_), fill='#b6ac95', outline='#948a76')

for pts in P.AIRPORT_RUNWAYS:
    d.line([px(p) for p in pts], fill='#cfc8b6', width=15, joint='curve')
    d.line([px(p) for p in pts], fill='#fbf8ee', width=3, joint='curve')
tx, ty, tw, th = P.AIRPORT_TERMINAL
d.rectangle((pxf(tx-tw/2, ty+th/2), pxf(tx+tw/2, ty-th/2)), fill='#c6bfad', outline='#9d9481', width=2)

d.line([px(p) for p in P.M0_RING], fill='#cfc8b6', width=9, joint='curve')
d.line([px(p) for p in P.M0_RING], fill='#fbf8ee', width=3, joint='curve')
for lx, ly, lr in P.M0_LOOPS:
    d.arc((pxf(lx-lr, ly+lr*0.7), pxf(lx+lr, ly-lr*0.7)), 0, 360, fill='#cfc8b6', width=6)
    d.arc((pxf(lx-lr, ly+lr*0.7), pxf(lx+lr, ly-lr*0.7)), 0, 360, fill='#fbf8ee', width=2)

for pts in P.POWERLINES:
    seq = []
    for a_, b_ in zip(pts, pts[1:]):
        L = math.hypot(b_[0]-a_[0], b_[1]-a_[1])
        n = max(1, int(L / 55))
        seq += [(a_[0]+(b_[0]-a_[0])*i/n, a_[1]+(b_[1]-a_[1])*i/n) for i in range(n+1)]
    for q in seq:
        c = pxf(*q)
        d.rectangle((c[0]-2, c[1]-2, c[0]+2, c[1]+2), fill='#8d846e')

for bx, by, ba in P.BARGES:
    l, w_ = 34, 9
    ca, sa = math.cos(ba), math.sin(ba)
    pts = [(bx+dx*ca-dy*sa, by+dx*sa+dy*ca) for dx, dy in
           [(-l/2, -w_/2), (l/2, -w_/2), (l/2, w_/2), (-l/2, w_/2)]]
    d.polygon([px(p) for p in pts], fill='#b3a48c', outline='#8b7d68')

for rx, ry, rn in P.RUINS:
    for i in range(rn * 6):
        a_ = pxf(rx + (i % 6) * 16 - 48, ry + (i // 6) * 14 - 30)
        d.rectangle((a_[0], a_[1], a_[0]+11, a_[1]+8), fill='#c3bca8', outline='#a49c88')

# ------------------------------------------------------------------ infra
for name, pts, cls in P.ROADS:
    w = {'primary': 4, 'secondary': 3}.get(cls, 3)
    d.line([px(p) for p in pts], fill='#fbf8ee', width=int(w * 2.6), joint='curve')
    d.line([px(p) for p in pts], fill='#c8a45f', width=w, joint='curve')
for name, pts in P.RAILS:
    d.line([px(p) for p in pts], fill='#8a6b56', width=3, joint='curve')
    d.line([px(p) for p in pts], fill=PAPER, width=1, joint='curve')
for name, pts in P.CANALS:
    d.line([px(p) for p in pts], fill='#6d9aa8', width=4, joint='curve')
for name, pts in P.METRO:
    d.line([px(p) for p in pts], fill='#7a63b0', width=3, joint='curve')

# ------------------------------------------------------------------ borders
for z in P.ZONES:
    pts = [px(p) for p in z['poly']] + [px(z['poly'][0])]
    d.line(pts, fill='#ffffff', width=5, joint='curve')
    d.line(pts, fill=KIND_LINE[z['kind']], width=2, joint='curve')

d.line([px(p) for p in P.WALL], fill='#ffffff', width=12, joint='curve')
d.line([px(p) for p in P.WALL], fill='#b8482c', width=6, joint='curve')

def dashed(pts, col, w=4, dash=11, gap=8):
    for a, b in zip(pts, pts[1:]):
        x1, y1 = px(a); x2, y2 = px(b)
        L = math.hypot(x2-x1, y2-y1)
        if not L: continue
        u = 0
        while u < L:
            v = min(u + dash, L)
            d.line((x1+(x2-x1)*u/L, y1+(y2-y1)*u/L, x1+(x2-x1)*v/L, y1+(y2-y1)*v/L), fill=col, width=w)
            u += dash + gap
dashed(P.QUAR_FENCE, '#b8912c', 4, 9, 7)

# ------------------------------------------------------------------ citadel
nx, ny = pxf(P.CITADEL['x'], P.CITADEL['y'])
rings = Image.new('L', (W, H), 0)
rd = ImageDraw.Draw(rings)
for r in (500, 1000, 1500, 2000):
    rd.ellipse([pxf(-r, r), pxf(r, -r)], outline=255, width=2)
clip = Image.new('L', (W, H), 0)
ImageDraw.Draw(clip).rectangle((PADL, PADT, PADL + MAPW, PADT + MAPH), fill=255)
rings = Image.composite(rings, Image.new('L', (W, H), 0), clip)
im.paste(Image.new('RGB', (W, H), '#a8674f'), (0, 0), rings.point(lambda v: int(v * 0.55)))
d = ImageDraw.Draw(im)
for r, lab in [(500, '500 м'), (1000, '1 км'), (1500, '1.5 км'), (2000, '2 км')]:
    txt((nx + 6, ny - r * SC - 12), lab, 14, '#8a5c46', 'lb')
nevx = int(P.CITADEL['nexus_r'] * SC)
d.ellipse((nx-nevx, ny-nevx, nx+nevx, ny+nevx), fill='#e3aca0', outline='#a8402a', width=3)
d.polygon([(nx, ny-22), (nx+13, ny+10), (nx, ny+4), (nx-13, ny+10)], fill='#a8402a')

# ------------------------------------------------------------------ labels
ZONE_LABEL = {
    'station': (-880, -215), 'ring': (-900, 330), 'embankment': (-860, -740),
    'oktogon': (-490, -640), 'terezvaros': (-490, -60), 'keleti': (60, -740),
    'park': (-150, 250),
    'promzona': (-700, 1010), 'canals': (60, 1010), 'fields': (720, 1010), 'm0': (1200, 1010),
    'hills': (-1350, 980), 'outskirts': (700, 170), 'road': (700, -560),
    'quarantine': (-1250, -780), 'gellert': (-1400, -330), 'flats': (-500, -1250),
    'airbase': (760, -1330),
}
for z in P.ZONES:
    cx, cy = pxf(*ZONE_LABEL[z['id']])
    txt((cx, cy), z['name'].upper(), 25, '#1b2a29', 'mm', plate=True)
    txt((cx, cy + 22), z['sub'], 16, '#4d5f5c', 'mm', plate=True)
    txt((cx, cy + 41), z['bsp'], 13, '#7b8a85', 'mm', plate=True)

PLACES = [
    ('Дунай', -1215, -180, 21, '#3c7482'),
    ('Цепной мост', -1000, -500, 14, '#3c7482'),
    ('Маргит-мост', -1084, 75, 14, '#3c7482'),
    ('Гора Геллерт', -1340, -900, 13, '#5a6b4a'),
    ('Замковый холм', -1265, -560, 13, '#5a6b4a'),
    ('ТЭЦ', -700, 1230, 13, '#8a6420'),
    ('Газгольдеры', -430, 1185, 12, '#8a6420'),
    ('Совхоз', 640, 1195, 13, '#55632a'),
    ('Депо', 880, 1150, 12, '#7a6a3a'),
    ('Заправка', 1120, 1120, 12, '#7a6a3a'),
    ('Блокпост', 470, -300, 12, '#8a6a2a'),
    ('Полосы', 660, -1175, 13, '#6e6448'),
    ('Терминал', 1140, -1395, 12, '#6e6448'),
    ('Мели и баржи', -740, -1235, 12, '#4a7078'),
    ('Насосная', -280, -1435, 12, '#4a7078'),
    ('Вышка Альянса', -1370, 650, 12, '#4f6b34'),
    ('Лагерь', -1510, 390, 12, '#4f6b34'),
    ('Посёлок', 610, 60, 12, '#8a6420'),
    ('Разборка', 900, -60, 12, '#8a6420'),
    ('Шоссе', 850, -455, 12, '#8a6a2a'),
    ('Келети', -60, -900, 13, '#3b4a48'),
]
for s, x, y, sz, col in PLACES:
    txt(pxf(x, y), s, sz, col, 'mm', plate=True)

tcol = {'street': '#5b6a66', 'checkpoint': '#c25a32', 'gate': '#b8860b', 'rail': '#8a5c44',
        'bridge': '#2e7f92', 'canal': '#2e8f96', 'underground': '#6b4fae', 'wild': '#5d7a2a',
        'open': '#6f7c35'}
for tid, a, b, typ, (x, y), name in P.TRANSITIONS:
    cx, cy = px((x, y)); r = 13
    d.ellipse((cx-r-2, cy-r-2, cx+r+2, cy+r+2), fill=PAPER)
    d.ellipse((cx-r, cy-r, cx+r, cy+r), outline=tcol[typ], width=3)
    txt((cx, cy+1), tid[1:], 15, tcol[typ], 'mm')

d.rectangle((PADL, PADT, PADL + MAPW, PADT + MAPH), outline='#8c9a95', width=2)

# ------------------------------------------------------------------ header
txt((PADL, 30), 'СИТИ-27', 64, '#172625')
txt((PADL + 330, 46), 'МИР И ОКРЕСТНОСТИ  •  ПЛАН СВЕРХУ  •  ЭТАП 1: СХЕМА', 24, '#5d706c')
txt((PADL, 110), '18 карт-зон  •  33 перехода  •  сжатие центра 2.2:1, пояса 3.2:1, пустоши 6–10:1',
    20, '#475955')
txt((PADL, 140), 'Начало координат — ось Цитадели. +X восток, +Y север. Красная линия — Стена, пунктир — периметр карантина.',
    17, '#5d706c')

sby = PADT + MAPH + 12
x0 = PADL
for k in range(4):
    d.rectangle((x0 + k * 250 * SC, sby, x0 + (k+1) * 250 * SC, sby + 9),
                fill=('#22302f' if k % 2 == 0 else '#ffffff'))
for k, lab in [(0, '0'), (250, '250'), (500, '500'), (1000, '1000 м')]:
    txt((x0 + k * SC, sby + 12), lab, 14, '#5d706c', 'ma' if k == 1000 else 'la')

ccx, ccy = PADL + MAPW - 54, PADT + 58
d.ellipse((ccx-30, ccy-30, ccx+30, ccy+30), outline='#8c9a95', width=2)
d.polygon([(ccx, ccy-24), (ccx-9, ccy+9), (ccx, ccy+3), (ccx+9, ccy+9)], fill='#22302f')
txt((ccx, ccy-50), 'С', 18, '#22302f', 'mb')

# ------------------------------------------------------------------ panel
LX = PADL + MAPW + 56
yy = 30
txt((LX, yy), 'УСЛОВНЫЕ ОБОЗНАЧЕНИЯ', 22, '#172625'); yy += 34
for kind, lab in [('city', 'Город за Стеной'), ('belt', 'Пояс у Стены'),
                  ('waste', 'Пустошь'), ('nexus', 'Нексус / Цитадель')]:
    d.rectangle((LX, yy, LX + 22, yy + 16), fill=KIND_FILL[kind], outline=KIND_LINE[kind], width=2)
    txt((LX + 32, yy - 3), lab, 18, '#334541'); yy += 25
yy += 3
types = [('street', 'Улица'), ('checkpoint', 'КПП'), ('gate', 'Ворота в Стене'),
         ('rail', 'Ж/д путь'), ('bridge', 'Мост'), ('canal', 'Канал'),
         ('underground', 'Под землёй'), ('wild', 'Проход в преграде'), ('open', 'Открытый край')]
for typ, lab in types:
    cx, cy = LX + 11, yy + 9
    d.ellipse((cx-8, cy-8, cx+8, cy+8), fill=PAPER, outline=tcol[typ], width=3)
    txt((LX + 32, yy - 3), lab, 17, '#334541'); yy += 22

yy += 14
txt((LX, yy), 'ЦИТАДЕЛЬ', 20, '#a8402a'); yy += 25
for s in ['высота 800 м над улицей', 'основание 300 × 300 м', 'Нексус — круг r = 165 м',
          'кольца 0.5 / 1 / 1.5 / 2 км', 'в 3D-скайбоксе 1:32']:
    txt((LX, yy), s, 16, '#4d5f5c'); yy += 20

yy += 16
txt((LX, yy), 'СЖАТИЕ', 20, '#172625'); yy += 25
for s in ['центр (r < 1.1 км) — 2.2 : 1', 'пояс (1.1–3 км) — 3.2 : 1',
          'ближняя пустошь (3–7 км) — 6 : 1', 'дальняя (7–20 км) — 10 : 1']:
    txt((LX, yy), s, 16, '#4d5f5c'); yy += 20

yy += 16
txt((LX, yy), 'РАЗМЕРЫ', 20, '#172625'); yy += 25
for s in ['мир 3.04 × 2.85 км (8.6 км²)', 'город 1.50 × 1.38 км (2.1 км²)',
          'пояс и пустошь — 76 %', 'зона 0.14–1.00 км²', 'предел BSP 832 × 832 м']:
    txt((LX, yy), s, 16, '#4d5f5c'); yy += 20

# ------------------------------------------------------------------ table
TY = PADT + MAPH + 40
d.line((PADL, TY - 10, PADL + MAPW, TY - 10), fill='#c9c3b0', width=2)
txt((PADL, TY), 'ПЕРЕХОДЫ МЕЖДУ КАРТАМИ', 20, '#172625')
names = {z['id']: z['name'] for z in P.ZONES}
cols, rows = 3, 11
colw = MAPW // cols
for i, (tid, a, b, typ, xy, name) in enumerate(P.TRANSITIONS):
    c, r = i // rows, i % rows
    tx = PADL + c * colw
    ty = TY + 30 + r * 20
    d.ellipse((tx-5, ty+3, tx+5, ty+13), outline=tcol[typ], width=2)
    txt((tx + 14, ty), f"{tid[1:]:>2s}", 14, tcol[typ])
    txt((tx + 34, ty), f"{names[a]} — {names[b]}", 14, '#334541')
    txt((tx + 34 + 268, ty), name, 13, '#7b8a85')

im.save(OUT / 'city27_master_plan.png', optimize=True)
print('WROTE', im.size)
