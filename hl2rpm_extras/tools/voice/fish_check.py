"""Check the Fish Audio key (FISH_AUDIO_API_KEY from the environment / HKCU\\Environment): reachability, credit,
one short synthesis with the free model. The key goes only into the Authorization header and is never printed."""
import json
import os
import sys
import time
import urllib.error
import urllib.request
import winreg

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
HERE = os.path.dirname(os.path.abspath(__file__))


def key():
    k = os.environ.get("FISH_AUDIO_API_KEY", "")
    if not k:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as h:
            k = winreg.QueryValueEx(h, "FISH_AUDIO_API_KEY")[0]
    return k.strip()


K = key()


def call(path, body=None, headers=None, timeout=120):
    h = {"Authorization": "Bearer " + K}
    h.update(headers or {})
    data = None
    if body is not None:
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        h["Content-Type"] = "application/json"
    req = urllib.request.Request("https://api.fish.audio" + path, data=data, headers=h)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.headers.get("Content-Type", ""), r.read(), r.geturl()
    except urllib.error.HTTPError as e:
        return e.code, e.headers.get("Content-Type", ""), e.read(), e.geturl()
    except Exception as e:
        return None, "", repr(e).replace(K, "<key>").encode(), ""


def show(b):
    return b[:300].decode("utf-8", "replace").replace(K, "<key>")


print("key length:", len(K))
for path in ("/wallet/self/api-credit", "/wallet/self/package"):
    st, ct, b, url = call(path)
    print(path, st, ct, url.split("?")[0] if url else "", "|", show(b))
t = time.time()
st, ct, b, url = call("/v1/tts", {"text": "Проваливай, гражданин. Гэ-о по ночам ходят по двору.", "format": "wav",
                                  "latency": "normal"}, {"model": "s2.1-pro-free"})
print("tts", st, ct, "%d bytes in %.1f s" % (len(b), time.time() - t), "" if st == 200 else show(b))
if st == 200 and b[:4] == b"RIFF":
    out = os.path.join(HERE, "fish_test")
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, "check_default_voice.wav"), "wb").write(b)
    print("saved", os.path.join(out, "check_default_voice.wav"))
