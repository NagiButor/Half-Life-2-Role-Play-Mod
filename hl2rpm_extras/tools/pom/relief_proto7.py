import os, sys, glob, time
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vtf
from relief_proto import R, find_vtf, resize, blur

def label_frac(mask):
    # largest connected component fraction, 4-neighbour, wrapping; iterative label propagation (numpy)
    h, w = mask.shape
    lab = np.where(mask, np.arange(h * w).reshape(h, w), -1)
    big = h * w + 1
    while True:
        m = np.where(mask, lab, big)
        nb = np.minimum.reduce([m, np.roll(m, 1, 0), np.roll(m, -1, 0), np.roll(m, 1, 1), np.roll(m, -1, 1)])
        new = np.where(mask, nb, -1)
        if (new == lab).all():
            break
        lab = new
    v = lab[mask]
    if v.size == 0:
        return 0.0
    _, c = np.unique(v, return_counts=True)
    return c.max() / v.size

def dil(m, r):
    o = m.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dx * dx + dy * dy <= r * r:
                o |= np.roll(np.roll(m, dy, 0), dx, 1)
    return o

def nets(alb, n, hpdiv, q, d):
    a = resize(alb, n)[..., :3].astype(np.float64) / 255.0
    L = a[..., 0] * 0.3 + a[..., 1] * 0.59 + a[..., 2] * 0.11
    hp = blur(L - blur(L, n / hpdiv), 0.8 * n / 256)
    md = hp < np.percentile(hp, q); mb = hp > np.percentile(hp, 100 - q)
    if d:
        md = dil(md, d); mb = dil(mb, d)
    return label_frac(md), label_frac(mb)

if __name__ == '__main__':
    for nm in ['brick/brickfloor001a', 'tile/tilefloor008a', 'brick/brickwall048d', 'stone/stonefloor006a']:
        alb = vtf.load(find_vtf(nm), 256)[0]
        for n in (128, 256):
            for hpdiv in (16, 32):
                for q in (20, 30):
                    for d in (0, 1):
                        t = time.time()
                        fd, fb = nets(alb, n, hpdiv, q, d)
                        print('%-22s n%d hp/%d q%d d%d: dark %.2f bright %.2f  (%.1fs)' % (nm, n, hpdiv, q, d, fd, fb, time.time() - t))
