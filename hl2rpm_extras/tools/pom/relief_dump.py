import os, sys
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vtf
from relief_proto import find_vtf, resize, blur, gauss_freq

def detail(alb):
    a = alb[..., :3].astype(np.float64) / 255.0
    L = a[..., 0] * 0.3 + a[..., 1] * 0.59 + a[..., 2] * 0.11
    n = L.shape[0]
    F = np.fft.fft2(L)
    k = np.fft.fftfreq(n) * 2 * np.pi
    kx, ky = np.meshgrid(k, k)
    k2 = kx * kx + ky * ky
    big, small = n / 16.0, 0.8 * n / 256
    g = (1 - np.exp(-k2 * big * big / 2)) * np.exp(-k2 * small * small / 2)
    return np.real(np.fft.ifft2(F * g))

name = sys.argv[1]; inv = len(sys.argv) > 2
al = resize(vtf.load(find_vtf(name), 256)[0], 256)
d = detail(al)
if inv: d = -d
lo, hi = np.percentile(d, 2), np.percentile(d, 55)
h = np.clip((d - lo) / (hi - lo), 0, 1)
gy, gx = np.gradient(h)
k = 1.6
nrm = np.stack([-gx * k * 2, -gy * k * 2, np.ones_like(h)], 2)  # central diff /2 already in gradient
nrm /= np.linalg.norm(nrm, axis=2, keepdims=True)
L = np.array([-0.6, -0.4, 0.7]); L /= np.linalg.norm(L)
lit = np.clip((nrm * L).sum(2), 0, 1)
alb = al[..., :3].astype(np.float64) / 255.0
img = np.concatenate([alb, np.repeat(h[..., None], 3, 2), np.clip(alb * (0.3 + 1.0 * lit[..., None]) * (0.65 + 0.35 * h[..., None]), 0, 1), (nrm * 0.5 + 0.5)], 1)
Image.fromarray((img * 255).astype(np.uint8)).resize((1024 * 2, 256 * 2), Image.NEAREST).save('dump_' + name.replace('/', '_') + '.png')
