import os, sys
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vtf
from relief_proto import R, find_vtf, resize, blur, parse_vmt
T = 128
items = []
for line in open('bumpless.txt'):
    p = line.strip()
    pl = p.lower().replace('\\', '/')
    if not any(k in pl for k in ('/brick/', '/stone/', '/tile/', 'cobble')):
        continue
    kv = parse_vmt(os.path.join(R, p))
    bt = kv.get('$basetexture')
    f = find_vtf(bt) if bt else None
    if not f:
        continue
    try:
        al = resize(vtf.load(f, 256)[0], 256)
    except Exception as e:
        continue
    a = al[..., :3].astype(np.float64) / 255.0
    L = a[..., 0] * 0.3 + a[..., 1] * 0.59 + a[..., 2] * 0.11
    hp = blur(L - blur(L, 16.0), 0.8)
    lo, hi = np.percentile(hp, 2), np.percentile(hp, 55)
    h = np.clip((hp - lo) / (hi - lo), 0, 1)
    items.append((os.path.basename(pl)[:-4], resize(al[..., :3], T), resize((np.repeat(h[..., None], 3, 2) * 255).astype(np.uint8), T)))
cols = 6
for s in range(0, len(items), 36):
    chunk = items[s:s + 36]
    rows = (len(chunk) + cols - 1) // cols
    img = Image.new('RGB', (cols * (2 * T + 10), rows * (T + 16)), (40, 40, 40))
    d = ImageDraw.Draw(img)
    for i, (n, a, h) in enumerate(chunk):
        x, y = (i % cols) * (2 * T + 10), (i // cols) * (T + 16)
        img.paste(Image.fromarray(a), (x, y + 14))
        img.paste(Image.fromarray(h), (x + T, y + 14))
        d.text((x + 2, y + 1), '%d %s' % (s + i, n), fill=(255, 255, 0))
    img.save('bumpless_%d.png' % (s // 36))
print(len(items))
