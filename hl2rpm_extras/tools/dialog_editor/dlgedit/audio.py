"""WAV handling for voice-over: format check, conversion to what Source plays
(PCM 16-bit, 11025/22050/44100 Hz, mono), audio hash, waveform, playback."""
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile

try:
    import numpy as np
except ImportError:  # the editor still works, conversion of odd formats does not
    np = None

SOURCE_RATES = (11025, 22050, 44100)
AUDIO_EXTS = (".wav", ".mp3", ".ogg", ".flac", ".m4a", ".aac", ".opus", ".wma")


class WavInfo:
    def __init__(self):
        self.ok = False
        self.error = ""
        self.fmt = 0
        self.channels = 0
        self.rate = 0
        self.bits = 0
        self.frames = 0
        self.duration = 0.0
        self.has_phonemes = False
        self.data_offset = 0
        self.data_size = 0

    def source_ready(self):
        """True if the engine plays the file as is and lip sync works (mono)."""
        return self.ok and self.fmt == 1 and self.bits == 16 and self.rate in SOURCE_RATES and self.channels == 1

    def describe(self):
        if not self.ok:
            return self.error or "не WAV"
        kind = {1: "PCM", 3: "float"}.get(self.fmt, "fmt %d" % self.fmt)
        ch = "моно" if self.channels == 1 else "стерео" if self.channels == 2 else "%d кан." % self.channels
        return "%.2f с · %d Гц · %d бит %s · %s" % (self.duration, self.rate, self.bits, kind, ch)


def _chunks(data):
    idx = 12
    n = len(data)
    while idx + 8 <= n:
        cid = data[idx:idx + 4]
        size = struct.unpack("<I", data[idx + 4:idx + 8])[0]
        yield cid, idx + 8, size
        idx += 8 + size + (size & 1)


def wav_info(path):
    info = WavInfo()
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        info.error = str(e)
        return info
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        info.error = "не RIFF/WAVE"
        return info
    for cid, off, size in _chunks(data):
        if cid == b"fmt ":
            fmt, ch, rate, _br, _ba, bits = struct.unpack("<HHIIHH", data[off:off + 16])
            if fmt == 0xFFFE and size >= 26:  # WAVE_FORMAT_EXTENSIBLE: real format in the GUID
                fmt = struct.unpack("<H", data[off + 24:off + 26])[0]
            info.fmt, info.channels, info.rate, info.bits = fmt, ch, rate, bits
        elif cid == b"data":
            info.data_offset, info.data_size = off, min(size, len(data) - off)
        elif cid == b"VDAT":
            info.has_phonemes = True
    if not info.rate or not info.data_size:
        info.error = "нет fmt/data"
        return info
    bpf = max(1, info.channels * info.bits // 8)
    info.frames = info.data_size // bpf
    info.duration = info.frames / float(info.rate)
    info.ok = True
    return info


def audio_hash(path):
    """Hash of the sound samples only: embedding lip sync (VDAT) keeps it."""
    info = wav_info(path)
    if not info.ok:
        return ""
    with open(path, "rb") as f:
        f.seek(info.data_offset)
        return hashlib.sha1(f.read(info.data_size)).hexdigest()[:16]


def _read_samples(path, info):
    with open(path, "rb") as f:
        f.seek(info.data_offset)
        raw = f.read(info.data_size)
    if info.fmt == 3 and info.bits == 32:
        a = np.frombuffer(raw, dtype="<f4").astype(np.float64)
    elif info.fmt == 3 and info.bits == 64:
        a = np.frombuffer(raw, dtype="<f8").astype(np.float64)
    elif info.bits == 8:
        a = (np.frombuffer(raw, dtype=np.uint8).astype(np.float64) - 128.0) / 128.0
    elif info.bits == 16:
        a = np.frombuffer(raw[:len(raw) // 2 * 2], dtype="<i2").astype(np.float64) / 32768.0
    elif info.bits == 24:
        b = np.frombuffer(raw[:len(raw) // 3 * 3], dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        v = np.where(v & 0x800000, v - 0x1000000, v)
        a = v.astype(np.float64) / 8388608.0
    elif info.bits == 32:
        a = np.frombuffer(raw, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError("неподдерживаемая разрядность %d" % info.bits)
    if info.channels > 1:
        a = a[: len(a) // info.channels * info.channels].reshape(-1, info.channels)
    else:
        a = a.reshape(-1, 1)
    return a


def _resample(x, src, dst):
    if src == dst:
        return x
    # windowed-sinc low pass when going down, then linear interpolation
    if dst < src:
        cutoff = 0.5 * dst / src * 0.95
        taps = 63
        t = np.arange(taps) - (taps - 1) / 2.0
        h = 2 * cutoff * np.sinc(2 * cutoff * t) * np.hamming(taps)
        h /= h.sum()
        x = np.convolve(x, h, mode="same")
    n_out = int(round(len(x) * dst / float(src)))
    pos = np.arange(n_out) * (src / float(dst))
    return np.interp(pos, np.arange(len(x)), x)


def write_wav16_mono(path, samples, rate):
    s = np.clip(samples, -1.0, 1.0)
    pcm = (s * 32767.0).round().astype("<i2").tobytes()
    hdr = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
    fmt = b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    data = b"data" + struct.pack("<I", len(pcm)) + pcm
    with open(path, "wb") as f:
        f.write(hdr + fmt + data)


def find_ffmpeg(hint=""):
    if hint and os.path.isfile(hint):
        return hint
    p = shutil.which("ffmpeg")
    if p:
        return p
    for root in (r"E:\Programs", r"C:\Program Files", r"C:\ffmpeg"):
        if not os.path.isdir(root):
            continue
        for name in os.listdir(root):
            if "ffmpeg" in name.lower():
                cand = os.path.join(root, name, "bin", "ffmpeg.exe")
                if os.path.isfile(cand):
                    return cand
    return ""


def convert_for_source(src, dst, ffmpeg_hint=""):
    """Copy/convert any audio file into a Source-ready WAV at dst.
    Returns (ok, message). Phoneme data (VDAT) of a ready WAV is kept."""
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    ext = os.path.splitext(src)[1].lower()
    tmp_from_ffmpeg = None
    try:
        if ext != ".wav":
            ff = find_ffmpeg(ffmpeg_hint)
            if not ff:
                return False, "для %s нужен ffmpeg (укажите путь в настройках) или WAV" % ext
            fd, tmp_from_ffmpeg = tempfile.mkstemp(suffix=".wav")
            os.close(fd)
            flags = 0x08000000 if sys.platform == "win32" else 0  # CREATE_NO_WINDOW
            r = subprocess.run([ff, "-y", "-loglevel", "error", "-i", src, "-ac", "1", "-ar", "44100",
                                "-c:a", "pcm_s16le", tmp_from_ffmpeg], capture_output=True, creationflags=flags)
            if r.returncode != 0:
                return False, "ffmpeg: " + r.stderr.decode("utf-8", "replace")[-300:]
            src = tmp_from_ffmpeg
        info = wav_info(src)
        if not info.ok:
            return False, "файл не читается как WAV: " + info.error
        if info.source_ready():
            if os.path.abspath(src) != os.path.abspath(dst):
                shutil.copyfile(src, dst)
            return True, "скопирован (%s)" % info.describe()
        if np is None:
            return False, "нужен numpy для конвертации " + info.describe()
        a = _read_samples(src, info).mean(axis=1)
        rate = info.rate
        target = rate if rate in SOURCE_RATES else (44100 if rate > 22050 else 22050)
        a = _resample(a, rate, target)
        tmp = dst + ".tmp"
        write_wav16_mono(tmp, a, target)
        os.replace(tmp, dst)
        return True, "сконвертирован: %s → %d Гц 16 бит моно" % (info.describe(), target)
    finally:
        if tmp_from_ffmpeg and os.path.exists(tmp_from_ffmpeg):
            os.remove(tmp_from_ffmpeg)


def waveform(path, buckets=400):
    """Peak envelope (list of floats 0..1) for drawing."""
    info = wav_info(path)
    if not info.ok or np is None:
        return []
    try:
        a = np.abs(_read_samples(path, info)).max(axis=1)
    except Exception:
        return []
    if len(a) == 0:
        return []
    n = min(buckets, len(a))
    edges = np.linspace(0, len(a), n + 1).astype(int)
    peaks = [float(a[edges[i]:max(edges[i] + 1, edges[i + 1])].max()) for i in range(n)]
    m = max(peaks) or 1.0
    return [p / m for p in peaks]


def read_phonemes(path):
    """Words of the lip sync stored in the WAV (VDAT): [(word, start, end)]."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return []
    if data[:4] != b"RIFF":
        return []
    for cid, off, size in _chunks(data):
        if cid == b"VDAT":
            raw = data[off:off + size].split(b"\0")[0]
            try:
                text = raw.decode("utf-8")
            except UnicodeDecodeError:
                text = raw.decode("cp1251", "replace")   # the Russian generator writes words in cp1251
            words = []
            for line in text.splitlines():
                parts = line.split()
                if len(parts) >= 4 and parts[0] == "WORD":
                    try:
                        words.append((parts[1].strip('"'), float(parts[2]), float(parts[3])))
                    except ValueError:
                        pass
            return words
    return []


# --- playback -----------------------------------------------------------------
_playing_tmp = None


def play(path):
    """Play a sound file asynchronously (Windows)."""
    global _playing_tmp
    stop()
    if not path or not os.path.exists(path):
        return False
    if sys.platform != "win32":
        return False
    import winsound
    src = path
    info = wav_info(path)
    if not info.ok or info.fmt not in (1,) or info.bits not in (8, 16):
        # winsound plays PCM only: make a temporary PCM copy
        fd, tmp = tempfile.mkstemp(suffix=".wav")
        os.close(fd)
        ok, _ = convert_for_source(path, tmp)
        if not ok:
            return False
        _playing_tmp = tmp
        src = tmp
    winsound.PlaySound(src, winsound.SND_FILENAME | winsound.SND_ASYNC | winsound.SND_NODEFAULT)
    return True


def stop():
    if sys.platform == "win32":
        import winsound
        winsound.PlaySound(None, 0)
