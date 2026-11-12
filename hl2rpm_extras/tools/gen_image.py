# -*- coding: utf-8 -*-
"""Images from Google's Gemini image models ("Nano Banana") for the mod: concepts, textures, icons, art.

    python gen_image.py --list                                  # the image models the key can use
    python gen_image.py "prompt" -o out.png [--model ID] [--aspect 16:9] [--size 1K|2K|4K]
                        [--ref picture.png ...] [--text]        # --ref: pictures to edit / follow
    python gen_image.py --prompt-file prompt.txt -o out.png     # a long prompt (UTF-8)

The key: the environment variable GEMINI_API_KEY, else (Windows) the same variable in the user's
registry environment - setx writes it there, and a program started before that does not see it yet.
The key goes only into the request header (x-goog-api-key): never into a URL, a file or the output.
The model: --model, else GEMINI_IMAGE_MODEL, else DEFAULT_MODEL. The image API is paid (no free
tier for the image models); every image Google makes carries an invisible SynthID watermark.
Only the standard library.
"""
import argparse
import base64
import json
import mimetypes
import os
import sys
import urllib.error
import urllib.request

API = "https://generativelanguage.googleapis.com/v1beta"
DEFAULT_MODEL = "gemini-nano-banana-2.1"      # (2026-11-02: --list shows it as "Nano Banana 2.1")


def api_key():
    k = os.environ.get("GEMINI_API_KEY", "").strip()
    if not k and sys.platform == "win32":
        import winreg
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as reg:
                k = str(winreg.QueryValueEx(reg, "GEMINI_API_KEY")[0]).strip()
        except OSError:
            k = ""
    if not k:
        raise SystemExit("no GEMINI_API_KEY (setx GEMINI_API_KEY \"...\" in your own terminal)")
    return k


def call(path, body=None, timeout=300):
    req = urllib.request.Request(API + path, data=None if body is None else json.dumps(body).encode("utf-8"),
                                 headers={"x-goog-api-key": api_key(), "Content-Type": "application/json"},
                                 method="GET" if body is None else "POST")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        try:
            msg = json.loads(e.read().decode("utf-8")).get("error", {})
            text = "%s %s: %s" % (msg.get("code", e.code), msg.get("status", ""), msg.get("message", ""))
        except Exception:
            text = "HTTP %d" % e.code
        raise SystemExit("Gemini API error - " + text)


def list_models():
    out, token = [], ""
    while True:
        r = call("/models?pageSize=1000" + ("&pageToken=" + token if token else ""))
        out += r.get("models", [])
        token = r.get("nextPageToken", "")
        if not token:
            break
    for m in out:
        name = m.get("name", "").replace("models/", "")
        if "image" in name.lower() or "banana" in (name + m.get("displayName", "")).lower():
            print("%-44s %s" % (name, m.get("displayName", "")))


def generate(prompt, out, model, aspect=None, size=None, refs=(), want_text=False):
    parts = [{"text": prompt}]
    for p in refs:
        mime = mimetypes.guess_type(p)[0] or "image/png"
        with open(p, "rb") as f:
            parts.append({"inline_data": {"mime_type": mime, "data": base64.b64encode(f.read()).decode("ascii")}})
    cfg = {"responseModalities": ["TEXT", "IMAGE"] if want_text else ["IMAGE"]}
    img = {}
    if aspect:
        img["aspectRatio"] = aspect
    if size:
        img["imageSize"] = size
    if img:
        cfg["imageConfig"] = img
    r = call("/models/%s:generateContent" % model, {"contents": [{"parts": parts}], "generationConfig": cfg})
    saved, texts = [], []
    for cand in r.get("candidates", []):
        for part in cand.get("content", {}).get("parts", []):
            data = part.get("inlineData") or part.get("inline_data")
            if data and data.get("data"):
                ext = mimetypes.guess_extension(data.get("mimeType") or data.get("mime_type") or "image/png") or ".png"
                base, oext = os.path.splitext(out)
                path = (base if oext else out) + ("_%d" % len(saved) if saved else "") + (oext or ext)
                os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
                with open(path, "wb") as f:
                    f.write(base64.b64decode(data["data"]))
                saved.append(path)
            elif part.get("text"):
                texts.append(part["text"])
    for t in texts:
        print("model:", t.strip())
    if not saved:
        why = [c.get("finishReason") for c in r.get("candidates", [])] or r.get("promptFeedback")
        raise SystemExit("no image in the answer (%s)" % (why,))
    use = r.get("usageMetadata", {})
    for p in saved:
        print("saved", p)
    if use:
        print("tokens: prompt %s, output %s" % (use.get("promptTokenCount"), use.get("candidatesTokenCount")))
    return saved


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("prompt", nargs="?")
    ap.add_argument("--prompt-file")
    ap.add_argument("-o", "--out", default="gen_image.png")
    ap.add_argument("--model", default=os.environ.get("GEMINI_IMAGE_MODEL") or DEFAULT_MODEL)
    ap.add_argument("--aspect", help="1:1, 16:9, 9:16, 4:3, 3:4, 3:2, 2:3, 21:9 ...")
    ap.add_argument("--size", help="1K, 2K or 4K (models that take it)")
    ap.add_argument("--ref", action="append", default=[], help="a picture to edit or follow (repeat)")
    ap.add_argument("--text", action="store_true", help="let the model answer with text too")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    if a.list:
        list_models()
        return
    prompt = open(a.prompt_file, encoding="utf-8").read() if a.prompt_file else a.prompt
    if not prompt:
        ap.error("a prompt is needed")
    generate(prompt, a.out, a.model, a.aspect, a.size, a.ref, a.text)


if __name__ == "__main__":
    main()
