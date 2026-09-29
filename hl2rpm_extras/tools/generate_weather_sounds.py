"""HL2RPM: procedural weather ambience loops (rain outside, rain on the roof, wind).

Half-Life 2 has thunder but no rain/wind loops, so these are synthesized:
  - all filtering is done in the frequency domain on the whole buffer, which is
    circular, so every file loops seamlessly;
  - individual drops are placed with wrap-around for the same reason;
  - a 'cue ' chunk at sample 0 tells the Source engine to loop the file.

Usage: python generate_weather_sounds.py <output dir>
       (the mod expects them in sound/hl2rpm/weather/)
"""
import os
import struct
import sys

import numpy as np

RATE = 44100
SECONDS = 16
N = RATE * SECONDS
rng = np.random.default_rng(1337)


def shape_noise(spectrum_fn, channels=2):
    """White noise shaped by spectrum_fn(freqs) -> gain, circular (loopable)."""
    out = []
    freqs = np.fft.rfftfreq(N, 1.0 / RATE)
    gain = spectrum_fn(np.maximum(freqs, 1.0))
    for _ in range(channels):
        spec = np.fft.rfft(rng.standard_normal(N))
        out.append(np.fft.irfft(spec * gain, N))
    return np.array(out)


def bandpass(lo, hi, slope=2.0):
    def fn(f):
        g = 1.0 / (1.0 + (lo / f) ** (2 * slope))
        g *= 1.0 / (1.0 + (f / hi) ** (2 * slope))
        return g
    return fn


def pink(f):
    return 1.0 / np.sqrt(f)


def add_drops(buf, rate_per_s, dur_ms, freq_lo, freq_hi, amp, pan_spread=0.8, noisy=0.5):
    """Short decaying bursts ('plinks' + clicks) at random times, wrapped around the loop."""
    count = int(rate_per_s * SECONDS)
    for _ in range(count):
        start = rng.integers(0, N)
        length = int(RATE * rng.uniform(dur_ms[0], dur_ms[1]) / 1000.0)
        t = np.arange(length) / RATE
        decay = np.exp(-t * rng.uniform(250.0, 900.0))
        f = rng.uniform(freq_lo, freq_hi)
        tone = np.sin(2 * np.pi * f * t * (1.0 - t * 30.0))  # small downward chirp
        click = rng.standard_normal(length)
        burst = (tone * (1.0 - noisy) + click * noisy) * decay * amp * rng.uniform(0.3, 1.0)
        pan = 0.5 + rng.uniform(-0.5, 0.5) * pan_spread
        idx = (start + np.arange(length)) % N
        buf[0, idx] += burst * (1.0 - pan)
        buf[1, idx] += burst * pan


def periodic_lfo(cycles_list, phases=None):
    """Sum of sines with an integer number of cycles over the loop (loopable)."""
    t = np.arange(N) / N
    lfo = np.zeros(N)
    for i, c in enumerate(cycles_list):
        ph = rng.uniform(0, 2 * np.pi) if phases is None else phases[i]
        lfo += np.sin(2 * np.pi * c * t + ph) / (i + 1)
    lfo -= lfo.min()
    return lfo / max(lfo.max(), 1e-9)


def normalize(buf, peak=0.7):
    m = np.max(np.abs(buf))
    return buf * (peak / m) if m > 0 else buf


def write_wav(path, buf):
    """16-bit stereo PCM with a 'cue ' chunk (loop point at 0) for Source."""
    data = np.clip(buf.T, -1.0, 1.0)
    pcm = (data * 32767.0).astype('<i2').tobytes()
    channels = buf.shape[0]
    fmt = struct.pack('<HHIIHH', 1, channels, RATE, RATE * channels * 2, channels * 2, 16)
    cue = struct.pack('<I', 1) + struct.pack('<II4sIII', 1, 0, b'data', 0, 0, 0)
    chunks = b'fmt ' + struct.pack('<I', len(fmt)) + fmt
    chunks += b'cue ' + struct.pack('<I', len(cue)) + cue
    chunks += b'data' + struct.pack('<I', len(pcm)) + pcm
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 4 + len(chunks)) + b'WAVE' + chunks)
    print('wrote', path, len(pcm) // 1024, 'KB')


def rain_outside():
    # hiss of countless drops + low body of the downpour + close individual drops
    hiss = shape_noise(lambda f: bandpass(900, 9000, 1.5)(f) * pink(f) ** 0.4)
    body = shape_noise(lambda f: bandpass(90, 700, 1.5)(f) * pink(f))
    hiss /= np.std(hiss)
    body /= np.std(body)
    # slow density fluctuation (loopable)
    swell = 0.85 + 0.15 * periodic_lfo([1, 3, 7])
    buf = hiss * 0.55 * swell + body * 0.30
    drops = np.zeros_like(buf)
    add_drops(drops, 380, (3, 12), 1800, 6500, 1.0, noisy=0.55)
    add_drops(drops, 60, (8, 25), 700, 2200, 1.6, noisy=0.35)
    buf += drops * 0.9
    return normalize(buf, 0.75)


def rain_roof():
    # heard from inside: muffled rumble and dull taps on the roof / windows
    rumble = shape_noise(lambda f: bandpass(60, 900, 2.0)(f) * pink(f))
    rumble /= np.std(rumble)
    taps = np.zeros_like(rumble)
    add_drops(taps, 140, (10, 35), 180, 650, 1.0, pan_spread=0.5, noisy=0.25)
    # muffle the taps too
    for c in range(2):
        spec = np.fft.rfft(taps[c])
        freqs = np.fft.rfftfreq(N, 1.0 / RATE)
        taps[c] = np.fft.irfft(spec * bandpass(80, 1400, 2.0)(np.maximum(freqs, 1.0)), N)
    taps /= max(np.std(taps), 1e-9)
    buf = rumble * 0.6 + taps * 0.45
    return normalize(buf, 0.7)


def wind():
    low = shape_noise(lambda f: bandpass(80, 380, 1.5)(f) * pink(f))
    mid = shape_noise(lambda f: bandpass(380, 1600, 2.5)(f) * pink(f) ** 0.5)
    low /= np.std(low)
    mid /= np.std(mid)
    gust = 0.35 + 0.65 * periodic_lfo([2, 3, 5])
    whistle = 0.15 + 0.85 * periodic_lfo([3, 4, 9]) ** 2
    buf = low * (0.5 + 0.5 * gust) + mid * 0.45 * gust * whistle
    return normalize(buf, 0.7)


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(out, exist_ok=True)
    write_wav(os.path.join(out, 'rain_loop.wav'), rain_outside())
    write_wav(os.path.join(out, 'rain_roof_loop.wav'), rain_roof())
    write_wav(os.path.join(out, 'wind_loop.wav'), wind())
