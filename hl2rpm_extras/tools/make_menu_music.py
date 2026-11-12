"""The music of the main menu, synthesised from scratch (no samples) in the spirit of the
Half-Life 2 score (Triage at Dawn, Particle Ghost): D minor, 80 BPM, ~2:42, written to loop.

    drone and struck metal -> pads (Dm(add9) - Bbmaj7 - Gm7 - Asus4/A) -> glass arpeggio ->
    a slow half-time beat with sub bass -> an electric piano melody -> breakdown -> climax -> drone

    python make_menu_music.py [--out <dir>] [--seed N]

Writes hl2rpm_menu_theme.wav (44.1 kHz, 16 bit) next to this script's output and the MP3 the game
plays: sound/music/hl2rpm/menu/hl2rpm_menu_theme.mp3 (ffmpeg, 192 kbps). numpy only: filters are
FFT filters, time-varying brightness is a crossfade of filtered copies, the reverb is a convolution
with a generated impulse response.
"""
import math
import os
import subprocess
import sys
import wave

import numpy as np

SR = 44100
BPM = 80.0
BEAT = 60.0 / BPM
BAR = 4 * BEAT
BARS = 54
LENGTH = BARS * BAR + 4.0           # the reverb tail of the last bar
N = int(LENGTH * SR)

MOD = r"E:\Steam\steamapps\sourcemods\hl2rpm"
OUT_MP3 = os.path.join(MOD, "sound", "music", "hl2rpm", "menu", "hl2rpm_menu_theme.mp3")
FFMPEG = r"E:\Programs\ffmpeg-2026-01-29-git-c898ddb8fe-full_build\bin\ffmpeg.exe"

rng = np.random.default_rng(17)


def midi(n):
    return 440.0 * 2 ** ((n - 69) / 12.0)


def t_of(bar, beat=0.0):
    return bar * BAR + beat * BEAT


# ------------------------------------------------------------------ building blocks
def silence():
    return np.zeros((N, 2), np.float32)


def place(buf, sig, start, pan=0.0, gain=1.0):
    """mono or stereo sig into buf at a time (s), equal power pan -1..1"""
    i = int(start * SR)
    if i >= N:
        return
    # every sound ends in a 20 ms fade: a cut tail would click
    k = min(len(sig), int(0.02 * SR))
    sig = sig.copy()
    fade = np.linspace(1.0, 0.0, k)
    sig[-k:] *= fade if sig.ndim == 1 else fade[:, None]
    if sig.ndim == 1:
        a = (pan + 1) * math.pi / 4
        sig = np.stack([sig * math.cos(a), sig * math.sin(a)], 1)
    n = min(len(sig), N - i)
    buf[i:i + n] += sig[:n] * gain


def env_adsr(n, a, d, s, r, hold):
    """attack, decay, sustain level, release (s), hold = time until release (s)"""
    t = np.arange(n) / SR
    e = np.where(t < a, t / max(a, 1e-4), s + (1 - s) * np.exp(-(t - a) / max(d, 1e-4)))
    rel = t > hold
    e[rel] = e[rel] * np.exp(-(t[rel] - hold) / max(r, 1e-4))
    return e


def saw(freq, n, phase0=0.0):
    """band-limited sawtooth (polyBLEP); freq: scalar or per-sample array"""
    f = np.broadcast_to(np.asarray(freq, np.float64), (n,))
    dt = f / SR
    ph = (phase0 + np.cumsum(dt)) % 1.0
    s = 2 * ph - 1
    # polyBLEP around the wrap
    m = ph < dt
    x = ph[m] / dt[m]
    s[m] -= x + x - x * x - 1
    m = ph > 1 - dt
    x = (ph[m] - 1) / dt[m]
    s[m] -= x * x + x + x + 1
    return s


def sine(freq, n, phase0=0.0):
    f = np.broadcast_to(np.asarray(freq, np.float64), (n,))
    return np.sin(2 * np.pi * (phase0 + np.cumsum(f) / SR))


def fft_filter(x, lo=None, hi=None, slope=1.0):
    """zero phase band filter of a mono / stereo signal: smooth (butterworth-like) edges"""
    n = len(x)
    nf = 1 << int(math.ceil(math.log2(n + 1)))
    X = np.fft.rfft(x, nf, axis=0)
    f = np.fft.rfftfreq(nf, 1 / SR)
    g = np.ones_like(f)
    if hi:
        g *= 1 / np.sqrt(1 + (f / hi) ** (4 * slope))
    if lo:
        g *= 1 / np.sqrt(1 + (lo / np.maximum(f, 1e-3)) ** (4 * slope))
    if x.ndim == 2:
        g = g[:, None]
    return np.fft.irfft(X * g, nf, axis=0)[:n]


def sweep_filter(x, cutoff_env, cutoffs=(250, 600, 1400, 3200, 7500)):
    """a lowpass whose cutoff follows cutoff_env (Hz, per sample): crossfade of fixed lowpasses"""
    lc = np.log(np.clip(cutoff_env, cutoffs[0], cutoffs[-1]))
    lcs = np.log(cutoffs)
    out = np.zeros(x.shape, np.float32)
    prev = fft_filter(x, hi=cutoffs[0]).astype(np.float32)
    for i in range(len(cutoffs) - 1):
        cur = fft_filter(x, hi=cutoffs[i + 1]).astype(np.float32)     # (two copies at a time: memory)
        w = np.clip((lc - lcs[i]) / (lcs[i + 1] - lcs[i]), 0, 1).astype(np.float32)
        inside = (lc >= lcs[i]) & (lc <= lcs[i + 1])
        if x.ndim == 2:
            w, inside = w[:, None], inside[:, None]
        out += np.where(inside, prev * (1 - w) + cur * w, 0).astype(np.float32)
        prev = cur
    return out


def reverb_ir(seconds=3.6, pre=0.025, damp_hi=1.6, seed=3):
    """stereo hall: decorrelated noise, high frequencies die faster, a few early reflections"""
    r = np.random.default_rng(seed)
    n = int(seconds * SR)
    t = np.arange(n) / SR
    ir = np.zeros((n, 2))
    bands = [(None, 400, seconds), (400, 2500, seconds * 0.85), (2500, None, damp_hi)]
    for lo, hi, rt in bands:
        noise = r.standard_normal((n, 2))
        noise = fft_filter(noise, lo=lo, hi=hi)
        ir += noise * np.exp(-6.9 * t / rt)[:, None]
    ir *= np.clip(t / 0.08, 0, 1)[:, None] ** 0.5              # soft onset
    for d, g in [(0.011, 0.5), (0.017, 0.4), (0.023, 0.35), (0.031, 0.3), (0.043, 0.22)]:
        k = int(d * SR)
        ir[k, 0] += g * (1 if r.random() > 0.5 else -1) * 3
        ir[k + int(0.003 * SR), 1] += g * (1 if r.random() > 0.5 else -1) * 3
    ir = np.concatenate([np.zeros((int(pre * SR), 2)), ir])
    return ir / np.sqrt((ir ** 2).sum() / 2)


def convolve(x, ir):
    n = len(x) + len(ir)
    nf = 1 << int(math.ceil(math.log2(n)))
    out = np.zeros((len(x), 2))
    for c in range(2):
        xc = x[:, c] if x.ndim == 2 else x
        y = np.fft.irfft(np.fft.rfft(xc, nf) * np.fft.rfft(ir[:, c], nf), nf)
        out[:, c] = y[:len(x)]
    return out


def pingpong(x, delay, feedback, taps=7):
    """stereo ping-pong echo of a stereo signal (wet only)"""
    out = np.zeros_like(x)
    d = int(delay * SR)
    g = 1.0
    for k in range(1, taps + 1):
        g *= feedback
        sh = d * k
        if sh >= len(x):
            break
        src = x[:-sh].sum(1) * 0.5
        src = fft_filter(src, hi=6000 / k ** 0.3) if k > 2 else src
        ch = k % 2
        out[sh:, ch] += src * g
    return out


# ------------------------------------------------------------------ instruments
def pad_voice(note, dur, attack=1.6, release=2.8, detune=7.0, bright=1.0, seed=0):
    r = np.random.default_rng(seed)
    n = int((dur + release + 0.5) * SR)
    f = midi(note)
    t = np.arange(n) / SR
    vib = 1 + 0.0018 * np.sin(2 * np.pi * (0.13 + 0.05 * r.random()) * t + r.random() * 6)
    out = np.zeros((n, 2))
    for k, (cents, pan) in enumerate([(-detune, -0.7), (0.0, 0.0), (detune, 0.7)]):
        v = saw(f * 2 ** (cents / 1200) * vib, n, r.random()) * (0.8 if k == 1 else 1.0)
        a = (pan + 1) * math.pi / 4
        out[:, 0] += v * math.cos(a)
        out[:, 1] += v * math.sin(a)
    out += sine(f * 0.5, n)[:, None] * 0.12 * (1.0 if note > 55 else 0.0)
    e = env_adsr(n, attack, 3.0, 0.75, release, dur)
    return out * e[:, None] * 0.12


def metal_hit(f0, n_sec=6.0, seed=0):
    """a struck steel pipe: inharmonic modes with their own decays"""
    r = np.random.default_rng(seed)
    n = int(n_sec * SR)
    t = np.arange(n) / SR
    modes = [(1.0, 4.5, 1.0), (2.32, 3.0, 0.55), (4.25, 1.9, 0.35), (6.63, 1.2, 0.22), (9.38, 0.7, 0.12), (12.8, 0.4, 0.06)]
    s = np.zeros(n)
    for ratio, dec, amp in modes:
        fr = f0 * ratio * (1 + r.uniform(-0.004, 0.004))
        beat = 1 + 0.15 * np.sin(2 * np.pi * r.uniform(0.3, 1.2) * t)
        s += amp * np.sin(2 * np.pi * fr * t + r.random() * 6) * np.exp(-t / dec) * beat
    click = r.standard_normal(int(0.01 * SR)) * np.exp(-np.arange(int(0.01 * SR)) / (0.002 * SR))
    s[:len(click)] += fft_filter(click, lo=800) * 0.4
    return s * 0.25


def kick():
    n = int(0.6 * SR)
    t = np.arange(n) / SR
    f = 60 + 95 * np.exp(-t * 28)
    s = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 6.5)
    s += 0.25 * np.tanh(3 * s)
    click = rng.standard_normal(n) * np.exp(-t * 400) * 0.08
    return (s + fft_filter(click, lo=1500)) * 0.75


def snare():
    n = int(0.7 * SR)
    t = np.arange(n) / SR
    tone = np.sin(2 * np.pi * 185 * t) * np.exp(-t * 20) * 0.5 + np.sin(2 * np.pi * 330 * t) * np.exp(-t * 28) * 0.2
    noise = fft_filter(rng.standard_normal(n), lo=900, hi=7000) * np.exp(-t * 11) * 0.7
    return (tone + noise) * 0.5


def hat(open_=False, vel=1.0):
    n = int((0.35 if open_ else 0.08) * SR)
    t = np.arange(n) / SR
    s = fft_filter(rng.standard_normal(n), lo=7000) * np.exp(-t * (9 if open_ else 70))
    return s * 0.12 * vel


def glass(note, vel=1.0):
    """FM bell: glassy, a little inharmonic (modulator 3.5x)"""
    n = int(2.4 * SR)
    t = np.arange(n) / SR
    f = midi(note)
    idx = 2.2 * np.exp(-t * 4.0) + 0.25
    mod = np.sin(2 * np.pi * 3.5 * f * t) * idx
    car = np.sin(2 * np.pi * f * t + mod)
    car += 0.3 * np.sin(2 * np.pi * 2 * f * t + 0.5 * mod)
    e = np.minimum(t / 0.004, 1) * np.exp(-t / 0.9)
    return car * e * 0.16 * vel


def epiano(note, dur, vel=1.0):
    """FM electric piano: a soft tine, a warm body, stereo chorus"""
    n = int((dur + 2.5) * SR)
    t = np.arange(n) / SR
    out = np.zeros((n, 2))
    for c, cents in enumerate((-4.0, 4.0)):
        f = midi(note) * 2 ** (cents / 1200)
        body_idx = 1.6 * np.exp(-t * 2.2) + 0.15
        body = np.sin(2 * np.pi * f * t + body_idx * np.sin(2 * np.pi * f * t))
        tine = np.sin(2 * np.pi * f * t + 0.9 * np.exp(-t * 18) * np.sin(2 * np.pi * 14 * f * t)) * np.exp(-t * 9) * 0.35
        e = np.minimum(t / 0.006, 1) * np.exp(-t / 1.7)
        rel = t > dur
        e[rel] *= np.exp(-(t[rel] - dur) / 0.35)
        out[:, c] = (body + tine) * e
    return out * 0.17 * vel


def sub_bass(note, dur):
    n = int((dur + 0.4) * SR)
    t = np.arange(n) / SR
    f = midi(note)
    s = np.sin(2 * np.pi * f * t) + 0.18 * np.sin(2 * np.pi * 2 * f * t) + 0.05 * saw(f, n)
    e = np.minimum(t / 0.05, 1)
    rel = t > dur
    e[rel] *= np.exp(-(t[rel] - dur) / 0.12)
    return s * e * 0.26


def radio_chirp(seed):
    """a distant Combine radio: band-limited static bursts and a two-tone beep"""
    r = np.random.default_rng(seed)
    n = int(1.6 * SR)
    t = np.arange(n) / SR
    burst = fft_filter(r.standard_normal(n), lo=400, hi=3200)
    gate = (np.sin(2 * np.pi * r.uniform(5, 9) * t) > r.uniform(-0.2, 0.4)).astype(float)
    gate = fft_filter(gate, hi=60)
    s = burst * gate * np.exp(-t / 0.6) * 0.06
    k = int(r.uniform(0.2, 0.7) * SR)
    bt = np.arange(int(0.09 * SR)) / SR
    beep = np.sin(2 * np.pi * 1180 * bt) * np.sin(np.pi * bt / bt[-1])
    s[k:k + len(beep)] += beep * 0.05
    k2 = k + int(0.13 * SR)
    beep2 = np.sin(2 * np.pi * 880 * bt) * np.sin(np.pi * bt / bt[-1])
    s[k2:k2 + len(beep2)] += beep2 * 0.05
    return s


# ------------------------------------------------------------------ the score
CHORDS = [  # (pad notes, bass root)
    ([50, 57, 65, 69, 76], 38),       # Dm(add9)
    ([46, 53, 62, 69, 74], 46),       # Bbmaj7 (bass an octave up: the HL2 score keeps its bass above 60 Hz)
    ([43, 50, 58, 65, 70], 43),       # Gm7
    ([45, 52, 57, 62, 64], 45),       # Asus4 (-> A in the second bar)
]
ARP = [  # glass arpeggio, notes per chord (eighths)
    [69, 74, 77, 81, 77, 74, 72, 74],
    [70, 74, 77, 81, 77, 74, 69, 74],
    [67, 70, 74, 77, 74, 70, 69, 70],
    [69, 73, 76, 81, 76, 73, 71, 73],
]
MELODY = [  # (bar offset, beat, length in beats, note) - 8 bars
    (0, 0, 2, 69), (0, 2, 1, 67), (0, 3, 1, 65),
    (1, 0, 3, 64), (1, 3, 1, 62),
    (2, 0, 2, 65), (2, 2, 1, 67), (2, 3, 1, 69),
    (3, 0, 2, 70), (3, 2, 2, 69),
    (4, 0, 2, 67), (4, 2, 1, 70), (4, 3, 1, 74),
    (5, 0, 2, 77), (5, 2, 1, 76), (5, 3, 1, 74),
    (6, 0, 3, 73), (6, 3, 1, 76),
    (7, 0, 4, 69),
]


def section_levels(bar):
    """(pads, beat, arp, melody, bass, brightness 0..1) of a bar"""
    if bar < 8:
        return (0.35 + 0.08 * bar, 0, 0, 0, 0, 0.1 + 0.03 * bar)
    if bar < 16:
        return (1.0, 0, 0.0 if bar < 12 else 0.6, 0, 0, 0.35 + 0.02 * (bar - 8))
    if bar < 24:
        return (0.9, 1.0, 0.8, 0, 1.0, 0.6)
    if bar < 32:
        return (0.85, 1.0, 0.65, 1.0, 1.0, 0.65)
    if bar < 40:
        return (1.0, 0, 0.35, 1.0, 0.0, 0.5)
    if bar < 48:
        return (1.0, 1.0, 0.9, 1.0 if bar >= 44 else 0.0, 1.0, 0.85)
    return (max(0.0, 0.9 - 0.15 * (bar - 48)), 0, 0.3 if bar < 50 else 0, 0, 0, 0.3)


def build():
    print("score...")
    pads = silence()
    drums = silence()
    bells = silence()
    keys = silence()
    bass = silence()
    fx = silence()

    # drone: D1 + A1 + D2, slowly beating, the whole track, louder at both ends
    t = np.arange(N) / SR
    drone = (np.sin(2 * np.pi * midi(38) * t) * 0.42 + np.sin(2 * np.pi * midi(45) * 1.0007 * t) * 0.26
             + saw(midi(50) * (1 + 0.0015 * np.sin(2 * np.pi * 0.05 * t)), N) * 0.10)
    drone = fft_filter(drone, lo=45, hi=260)
    end_t = BARS * BAR
    dl = 0.55 + 0.45 * (np.clip(1 - t / 30, 0, 1) + np.clip((t - (end_t - 24)) / 24, 0, 1))
    dl *= np.clip(t / 4.0, 0, 1) * np.clip((LENGTH - t) / 3.0, 0, 1)
    fx += np.stack([drone, np.roll(drone, 220)], 1) * (dl * 0.28)[:, None]

    # wind: filtered noise breathing
    wind = fft_filter(rng.standard_normal((N, 2)), lo=150, hi=1200)
    wl = 0.5 + 0.5 * np.sin(2 * np.pi * t / 23.0) * np.sin(2 * np.pi * t / 9.0)
    fx += wind * (wl * 0.02 * dl)[:, None]

    # struck metal: sparse, mostly at the phrase starts
    for bar, f0, pan in [(0, 98, -0.4), (4, 131, 0.5), (8, 98, -0.2), (16, 87, 0.3), (24, 116, -0.5),
                         (32, 98, 0.0), (36, 131, 0.4), (40, 87, -0.3), (48, 98, 0.2), (51, 131, -0.4)]:
        place(fx, metal_hit(f0, seed=bar), t_of(bar, 0.02), pan, 0.9)

    # Combine radio in the distance
    for i, bar in enumerate([2, 6, 13, 34, 38, 49, 52]):
        place(fx, radio_chirp(i), t_of(bar, rng.uniform(1, 3)), rng.uniform(-0.8, 0.8), 0.7)

    # pads: 2 bars per chord
    brightness = np.zeros(N)
    pad_gain = np.zeros(N)
    for bar in range(0, BARS, 2):
        ci = (bar // 2) % 4
        notes, root = CHORDS[ci]
        lv = section_levels(bar)
        if lv[0] <= 0:
            continue
        for k, nt in enumerate(notes):
            if ci == 3 and k == 3:
                # Asus4 resolves to A in the second bar: D -> C#
                place(pads, pad_voice(62, BAR - 0.2, seed=bar * 10 + k), t_of(bar), 0, lv[0])
                place(pads, pad_voice(61, BAR, attack=0.9, seed=bar * 10 + k + 5), t_of(bar + 1), 0, lv[0])
                continue
            place(pads, pad_voice(nt, 2 * BAR - 0.2, seed=bar * 10 + k), t_of(bar), 0, lv[0] * (0.7 if k == 4 else 1.0))
        i0, i1 = int(t_of(bar) * SR), min(N, int(t_of(bar + 2) * SR))
        brightness[i0:i1] = lv[5]
    # the filter opens and breathes
    sm = np.convolve(brightness, np.ones(int(2.0 * SR)) / int(2.0 * SR), mode="same")
    lfo = 0.5 + 0.5 * np.sin(2 * np.pi * t / (4 * BAR))
    cutoff = 300 * (16 ** (sm * (0.85 + 0.15 * lfo)))
    print("pads filter...")
    pads = sweep_filter(pads, cutoff)

    # beat, bass
    print("beat...")
    k_env = np.zeros(N)
    kk, sn = kick(), snare()
    for bar in range(BARS):
        lv = section_levels(bar)
        if lv[1] <= 0:
            continue
        fill = (bar % 8) == 7
        for step in range(16):
            tt = t_of(bar, step / 4.0)
            swing = 0.018 if step % 2 else 0.0
            if step in (0, 6, 10) or (fill and step in (12, 14)):
                place(drums, kk, tt, 0, lv[1])
                i0 = int(tt * SR)
                k_env[i0:i0 + int(0.25 * SR)] = np.maximum(k_env[i0:i0 + int(0.25 * SR)], np.exp(-np.arange(min(int(0.25 * SR), N - i0)) / (0.08 * SR)))
            if step == 8:
                place(drums, sn, tt, 0.05, lv[1])
            if step == 15 and fill:
                place(drums, sn, tt, 0.05, lv[1] * 0.5)
            if step % 2 == 0 or rng.random() < 0.3:
                vel = (1.0 if step % 4 == 2 else 0.55) * rng.uniform(0.8, 1.1)
                place(drums, hat(open_=(step == 14 and bar % 2 == 1), vel=vel), tt + swing, 0.35, lv[1])
        ci = (bar // 2) % 4
        if lv[4] > 0:
            root = CHORDS[ci][1]
            for beat, length in [(0, 1.4), (1.5, 0.4), (2, 1.4), (3.5, 0.4)]:
                place(bass, sub_bass(root, length * BEAT), t_of(bar, beat), 0, lv[4])

    # glass arpeggio (eighths), with ping-pong echoes
    for bar in range(BARS):
        lv = section_levels(bar)
        if lv[2] <= 0:
            continue
        ci = (bar // 2) % 4
        pattern = ARP[ci]
        for i, nt in enumerate(pattern):
            vel = (1.0 if i % 4 == 0 else 0.7) * rng.uniform(0.85, 1.05)
            place(bells, glass(nt, vel), t_of(bar, i * 0.5) + rng.uniform(0, 0.006), rng.uniform(-0.3, 0.3), lv[2])

    # electric piano melody
    for start in (24, 32, 44):
        for bo, beat, ln, nt in MELODY:
            bar = start + bo
            if bar >= BARS:
                continue
            lv = section_levels(bar)
            if lv[3] <= 0:
                continue
            place(keys, epiano(nt, ln * BEAT * 0.95, 0.9 + 0.1 * rng.random()), t_of(bar, beat) + rng.uniform(0, 0.01), 0, lv[3])
            # an octave below in the breakdown, softer
            if 32 <= bar < 40:
                place(keys, epiano(nt - 12, ln * BEAT * 0.95, 0.45), t_of(bar, beat) + 0.012, 0, lv[3])

    # ------------------------------------------------------------ mix
    print("mix, reverb...")
    duck = 1 - 0.28 * fft_filter(k_env, hi=40)
    pads *= duck[:, None]
    bells_wet = pingpong(bells, 0.75 * BEAT, 0.42)
    hall = reverb_ir(3.8)
    room = reverb_ir(1.4, pre=0.012, damp_hi=0.6, seed=9)
    send_hall = pads * 0.45 + bells * 0.55 + bells_wet * 0.5 + keys * 0.5 + fx * 0.7 + drums * 0.12
    send_room = drums * 0.35 + keys * 0.15
    wet = convolve(send_hall, hall) * 0.42 + convolve(send_room, room) * 0.25
    dry = pads * 0.9 + bells * 0.55 + bells_wet * 0.35 + keys * 0.8 + bass * 0.9 + drums * 0.75 + fx * 0.6
    mix = dry + wet
    mix = fft_filter(mix, lo=40, slope=1.5)              # the HL2 score has little below 60 Hz
    mix -= mix.mean(0)
    # gentle glue: a slow compressor on the sum, then soft clipping
    level = np.sqrt(fft_filter((mix ** 2).mean(1), hi=3) + 1e-9)
    target = np.percentile(level, 90)
    gain = np.where(level > target, (target / level) ** 0.35, 1.0)
    mix *= gain[:, None]
    mix /= np.abs(mix).max() / 0.95
    mix = np.tanh(mix * 1.25) / np.tanh(1.25)
    mix *= 10 ** (-1.0 / 20) / np.abs(mix).max()          # -1 dBFS
    # the very end and start: fades for a clean loop
    fade_in = np.clip(t / 0.5, 0, 1)
    fade_out = np.clip((LENGTH - t) / 2.5, 0, 1)
    mix *= (fade_in * fade_out)[:, None]
    stems = {"pads": pads, "drums": drums, "bells": bells, "keys": keys, "bass": bass, "fx": fx}
    return mix, stems


def write_wav(path, x):
    y = np.clip(x, -1, 1)
    y = (y * 32767 + rng.uniform(-0.5, 0.5, y.shape)).astype(np.int16)    # dithered
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(y.tobytes())


def report(mix):
    rms = np.sqrt((mix ** 2).mean())
    print("length %.1f s, peak %.2f dBFS, rms %.1f dBFS" % (len(mix) / SR, 20 * np.log10(np.abs(mix).max()), 20 * np.log10(rms)))
    for bar in (0, 8, 16, 24, 32, 40, 48, 52):
        seg = mix[int(t_of(bar) * SR):int(t_of(bar + 2) * SR)]
        print("  bar %2d: rms %.1f dBFS" % (bar, 20 * np.log10(np.sqrt((seg ** 2).mean()) + 1e-9)))


def main(argv):
    out_dir = argv[argv.index("--out") + 1] if "--out" in argv else os.path.join(os.path.dirname(os.path.abspath(__file__)), "_music")
    os.makedirs(out_dir, exist_ok=True)
    mix, stems = build()
    report(mix)
    wav = os.path.join(out_dir, "hl2rpm_menu_theme.wav")
    write_wav(wav, mix)
    print("wav:", wav)
    os.makedirs(os.path.dirname(OUT_MP3), exist_ok=True)
    subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-i", wav, "-codec:a", "libmp3lame", "-b:a", "192k", "-ar", "44100", OUT_MP3], check=True)
    print("mp3:", OUT_MP3, os.path.getsize(OUT_MP3) // 1024, "KB")


if __name__ == "__main__":
    main(sys.argv[1:])
