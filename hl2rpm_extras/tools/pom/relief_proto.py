# Prototype: height maps for POM from the albedo (materials without a bump map), validated against
# the heights integrated from real bump maps of stock materials that have both.
import os, re, sys, glob
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
import vtf

R = r'E:/SourceModding/UsefulStuff/Source sdk 2013 HL2 unpacked vpk'
N = 256


def find_vtf(name):
    name = name.replace('\\', '/').lower()
    for g in ('hl2', 'episodic', 'ep2'):
        p = os.path.join(R, g, 'materials', name + '.vtf')
        if os.path.exists(p):
            return p
    return None


def load_gray(path, n=N):
    a, fmt, sz = vtf.load(path, n)
    return a


def resize(a, n):
    from PIL import Image
    if a.shape[0] == n and a.shape[1] == n:
        return a
    return np.asarray(Image.fromarray(a).resize((n, n), Image.BILINEAR))


def gauss_freq(n, sigma):
    k = np.fft.fftfreq(n) * 2 * np.pi
    kx, ky = np.meshgrid(k, k)
    return np.exp(-(kx * kx + ky * ky) * sigma * sigma / 2)


def blur(x, sigma):
    return np.real(np.fft.ifft2(np.fft.fft2(x) * gauss_freq(x.shape[0], sigma)))


def height_from_normal(nrm, ssbump=False):
    n = nrm.shape[0]
    v = nrm[..., :3].astype(np.float64) / 255.0
    if ssbump:
        B = np.array([[0.81649658, 0.0, 0.57735027], [-0.40824829, 0.70710678, 0.57735027], [-0.40824829, -0.70710678, 0.57735027]])
        nv = v[..., 0:1] * B[0] + v[..., 1:2] * B[1] + v[..., 2:3] * B[2]
    else:
        nv = v * 2 - 1
    nv /= np.linalg.norm(nv, axis=2, keepdims=True) + 1e-9
    iz = 1.0 / np.maximum(nv[..., 2], 0.25)
    gx = np.clip(-nv[..., 0] * iz, -3, 3)
    gy = np.clip(-nv[..., 1] * iz, -3, 3)
    Gx, Gy = np.fft.fft2(gx), np.fft.fft2(gy)
    k = np.fft.fftfreq(n) * 2 * np.pi
    kx, ky = np.meshgrid(k, k)
    k2 = kx * kx + ky * ky
    k2[0, 0] = 1
    H = -1j * (kx * Gx + ky * Gy) / k2
    H[0, 0] = 0
    return np.real(np.fft.ifft2(H))


def skew(x):
    x = x - x.mean()
    s = x.std() + 1e-9
    return (x ** 3).mean() / s ** 3


def height_from_albedo(alb, n=N):
    L = (alb[..., 0] * 0.3 + alb[..., 1] * 0.59 + alb[..., 2] * 0.11).astype(np.float64) / 255.0
    hp = L - blur(L, n / 16.0)
    hp = blur(hp, 1.0 * n / 256)
    s = skew(hp)
    pol = -1.0 if s > 0 else 1.0
    return hp * pol, s


def corr(a, b):
    a = a - a.mean(); b = b - b.mean()
    return (a * b).mean() / (a.std() * b.std() + 1e-12)


def parse_vmt(path):
    t = open(path, 'r', errors='ignore').read()
    kv = {}
    for m in re.finditer(r'"?(\$[a-zA-Z0-9_]+)"?\s+"?([^"\n]+)"?', t):
        kv[m.group(1).lower()] = m.group(2).strip()
    return kv


if __name__ == '__main__':
    folders = sys.argv[1:] or ['brick', 'stone', 'tile', 'concrete', 'cobble', 'plaster', 'wood', 'metal', 'nature', 'building_template']
    res = []
    for g in ('hl2', 'episodic', 'ep2'):
        for f in folders:
            for vmt in glob.glob(os.path.join(R, g, 'materials', f, '*.vmt')):
                kv = parse_vmt(vmt)
                b, bt = kv.get('$bumpmap'), kv.get('$basetexture')
                if not b or not bt or '$basetexture2' in kv:
                    continue
                pb, pa = find_vtf(b), find_vtf(bt)
                if not pb or not pa:
                    continue
                try:
                    nb = resize(vtf.load(pb, N)[0], N)
                    al = resize(vtf.load(pa, N)[0], N)
                except Exception as e:
                    continue
                hn = height_from_normal(nb, kv.get('$ssbump', '0') not in ('0', ''))
                hn = blur(hn - blur(hn, N / 16.0), 1.0)
                ha, s = height_from_albedo(al)
                c = corr(ha, hn)
                res.append((os.path.relpath(vmt, R), c, s))
    res.sort(key=lambda r: r[1])
    for r in res:
        print('%-70s corr %+.2f  skew %+.2f' % r)
    cs = np.array([r[1] for r in res])
    print('n=%d  right sign %d (%.0f%%), |corr|>0.2: %d right / %d wrong' % (
        len(cs), (cs > 0).sum(), 100.0 * (cs > 0).mean(), (cs > 0.2).sum(), (cs < -0.2).sum()))
