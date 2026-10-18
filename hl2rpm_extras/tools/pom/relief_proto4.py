import os, sys, glob
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vtf
from relief_proto import R, N, find_vtf, resize, blur, height_from_normal, corr, parse_vmt

def largest_frac(mask):
    # connected components (4-neighbour, wrapping), union-find on a label grid
    h, w = mask.shape
    lab = -np.ones((h, w), np.int64)
    parent = []
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    ys, xs = np.nonzero(mask)
    for y, x in zip(ys.tolist(), xs.tolist()):
        up = lab[y - 1, x] if mask[y - 1, x] else -1
        lf = lab[y, x - 1] if mask[y, x - 1] else -1
        if up < 0 and lf < 0:
            lab[y, x] = len(parent); parent.append(len(parent))
        elif up >= 0 and lf >= 0:
            a, b = find(up), find(lf)
            lab[y, x] = a
            if a != b: parent[b] = a
        else:
            lab[y, x] = up if up >= 0 else lf
    # wrap edges
    for x in range(w):
        if mask[0, x] and mask[h - 1, x] and lab[0, x] >= 0 and lab[h - 1, x] >= 0:
            a, b = find(lab[0, x]), find(lab[h - 1, x])
            if a != b: parent[b] = a
    for y in range(h):
        if mask[y, 0] and mask[y, w - 1] and lab[y, 0] >= 0 and lab[y, w - 1] >= 0:
            a, b = find(lab[y, 0]), find(lab[y, w - 1])
            if a != b: parent[b] = a
    roots = np.array([find(l) for l in lab[mask].tolist()])
    if roots.size == 0: return 0.0
    _, cnt = np.unique(roots, return_counts=True)
    return cnt.max() / roots.size

def hp_of(alb, n):
    a = alb[..., :3].astype(np.float64) / 255.0
    L = a[..., 0] * 0.3 + a[..., 1] * 0.59 + a[..., 2] * 0.11
    return blur(L - blur(L, n / 16.0), 0.8 * n / 256)

M = 128
res = []
for g in ('hl2', 'episodic', 'ep2'):
    for f in ['brick', 'stone', 'tile', 'cobble', 'concrete']:
        for vmt in glob.glob(os.path.join(R, g, 'materials', f, '*.vmt')):
            kv = parse_vmt(vmt)
            b, bt = kv.get('$bumpmap'), kv.get('$basetexture')
            if not b or not bt or '$basetexture2' in kv:
                continue
            pb, pa = find_vtf(b), find_vtf(bt)
            if not pb or not pa:
                continue
            try:
                nb = resize(vtf.load(pb, N)[0], N); al = resize(vtf.load(pa, M)[0], M)
            except Exception:
                continue
            hn = height_from_normal(nb, kv.get('$ssbump', '0') not in ('0', ''))
            hn = blur(hn - blur(hn, N / 16.0), 1.0)
            hp = hp_of(al, M)
            hnS = np.asarray(__import__('PIL.Image', fromlist=['Image']).fromarray(hn.astype(np.float32)).resize((M, M)))
            c = corr(hp, hnS)
            q = 30
            fd = largest_frac(hp < np.percentile(hp, q))
            fb = largest_frac(hp > np.percentile(hp, 100 - q))
            res.append((os.path.basename(vmt), c, fd, fb))
for margin in (0.1, 0.2, 0.3, 0.4):
    for minf in (0.3, 0.5, 0.7):
        flip = lambda x: x[3] > x[2] + margin and x[3] > minf
        right = sum(1 for x in res if (x[1] < 0) == flip(x))
        nf = sum(1 for x in res if flip(x)); rf = sum(1 for x in res if flip(x) and x[1] < 0)
        conf = [x for x in res if abs(x[1]) > 0.2]
        rc = sum(1 for x in conf if (x[1] < 0) == flip(x))
        print('margin %.1f min %.1f: flips %d (right %d), overall %d/%d, confident %d/%d' % (margin, minf, nf, rf, right, len(res), rc, len(conf)))
print()
for x in sorted(res, key=lambda x: x[1])[:20]:
    print('%-36s corr %+.2f  dark net %.2f  bright net %.2f' % x)
