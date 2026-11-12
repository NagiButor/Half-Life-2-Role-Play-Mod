"""Run the voice job on a Colab T4 with the colab CLI, surviving a lost VM:
new session -> upload job -> setup -> lines in chunks (each chunk downloaded at once) -> stop.
Before the lines: designed voices and transcripts of new references (prep stage), merged into the local job.
Re-running skips everything already downloaded. usage: drive.py <job dir> <results dir> [--open-ui]"""
import json
import os
import re
import subprocess
import sys
import time
import zipfile

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
HERE = os.path.dirname(os.path.abspath(__file__))
JOB_DIR = os.path.abspath(sys.argv[1]) if len(sys.argv) > 2 else os.path.join(os.path.dirname(HERE), "job")
COLAB = os.path.join(HERE, "venv", "Scripts", "colab.exe")
SESSION = "voice"
CHUNK = 6
TOKEN_RE = re.compile(r"(token=)[A-Za-z0-9._\-]+")


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def colab(sub, *args, timeout=3600, env=None):
    cmd = [COLAB, sub, "-s", SESSION]
    for k, v in (env or {}).items():
        cmd += ["--env", "%s=%s" % (k, v)]
    cmd += list(args)
    try:
        # PYTHONUTF8: the CLI reads the script with the locale encoding (cp1251) otherwise - Cyrillic breaks it
        p = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace",
                           stdin=subprocess.DEVNULL, timeout=timeout + 120, env=dict(os.environ, PYTHONUTF8="1"))
    except subprocess.TimeoutExpired:
        return 124, "timeout"
    return p.returncode, TOKEN_RE.sub(r"\1<hidden>", (p.stdout or "") + (p.stderr or ""))


def alive():
    rc, out = colab("status", timeout=60)
    return rc == 0 and "Hardware" in out


def pack():
    path = os.path.join(HERE, "job_upload.zip")
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for root, _, files in os.walk(JOB_DIR):
            for fn in files:
                p = os.path.join(root, fn)
                z.write(p, os.path.relpath(p, JOB_DIR))
    return path


def new_session(open_ui):
    subprocess.run([COLAB, "stop", "-s", SESSION], capture_output=True, stdin=subprocess.DEVNULL)
    log("new T4 session")
    p = subprocess.run([COLAB, "new", "-s", SESSION, "--gpu", "T4"], capture_output=True, text=True,
                       stdin=subprocess.DEVNULL, timeout=600)
    if "READY" not in (p.stdout + p.stderr):
        raise SystemExit("session failed: " + TOKEN_RE.sub(r"\1<hidden>", (p.stdout + p.stderr)[-800:]))
    if open_ui:  # the browser tab attached to this VM keeps it an interactive session
        rc, out = colab("url", "--open", timeout=60)
        log("browser UI attached", rc)
    rc, out = colab("upload", pack(), "/content/job.zip", timeout=300)
    log("upload", rc)
    rc, out = colab("exec", "--timeout", "1800", "-f", os.path.join(HERE, "setup.py"), timeout=1800)
    log("setup", rc, "\n" + "\n".join(l for l in out.splitlines() if l.strip() and "%|" not in l)[-1200:])
    if rc != 0 or "setup done" not in out:
        raise RuntimeError("setup failed")


def remote_files(path="/content"):
    rc, out = colab("ls", path, timeout=60)
    return set(l.strip() for l in out.splitlines()) if rc == 0 else set()


def step(first, results):
    """The CLI often never receives the kernel's final reply although the cell has finished, and then waits for
    its whole timeout: start the cell, let the CLI give up after a few seconds (the kernel keeps running) and
    watch for the chunk's zip instead."""
    zname = "step_clone_%d.zip" % first
    t0 = time.time()
    if zname not in remote_files():
        colab("exec", "--timeout", "15", "-f", os.path.join(HERE, "step.py"), timeout=60,
              env={"FIRST": first, "COUNT": CHUNK})
        while zname not in remote_files():
            if time.time() - t0 > 3000:
                raise RuntimeError("chunk %d timed out" % first)
            if not alive():
                raise RuntimeError("session lost during chunk %d" % first)
            time.sleep(10)
    local = os.path.join(results, "_dl.zip")
    rc, out = colab("download", "/content/" + zname, local, timeout=600)
    if rc != 0 or not os.path.exists(local):
        raise RuntimeError("download failed: " + out[-300:])
    with zipfile.ZipFile(local) as z:
        z.extractall(results)
    os.remove(local)
    q = json.load(open(os.path.join(results, "qc_clone_%d.json" % first), encoding="utf-8"))
    lines = []
    for iid, r in q.items():
        if "best" not in r:
            lines.append("  %-24s FAILED" % iid)
            continue
        b = r["takes"][r["best"]]
        lines.append("  %-24s take %d/%d cer %.2f %.1fs%s | %s" % (iid, r["best"] + 1, len(r["takes"]), b["cer"], b["sec"],
                     " RUNAWAY" if b.get("runaway") else "", b["hyp"][:60]))
    log("chunk %d done in %.0f s\n%s" % (first, time.time() - t0, "\n".join(lines)))


def wait_zip(zname, t0, limit=3000):
    while zname not in remote_files():
        if time.time() - t0 > limit:
            raise RuntimeError(zname + " timed out")
        if not alive():
            raise RuntimeError("session lost while waiting for " + zname)
        time.sleep(10)


def prep(job, results):
    """designed voices -> refs of the job, transcripts of new refs -> ref_text; the job is uploaded again"""
    if all(d in job["voices"] for d in job.get("designs", {})) and all(v.get("ref_text") for v in job["voices"].values()):
        return job
    t0 = time.time()
    if "step_prep_0.zip" not in remote_files():
        colab("exec", "--timeout", "15", "-f", os.path.join(HERE, "step.py"), timeout=60, env={"STAGE": "prep"})
        wait_zip("step_prep_0.zip", t0)
    local = os.path.join(results, "_prep.zip")
    colab("download", "/content/step_prep_0.zip", local, timeout=600)
    with zipfile.ZipFile(local) as z:
        z.extractall(os.path.join(results, "_prep"))
    info = json.load(open(os.path.join(results, "_prep", "prep.json"), encoding="utf-8"))
    for did in info["designs"]:
        d = job["designs"][did]
        os.replace(os.path.join(results, "_prep", "design_%s.wav" % did), os.path.join(JOB_DIR, "refs", did + ".wav"))
        job["voices"][did] = {"ref": "refs/%s.wav" % did, "lang": d["lang"], "ref_text": d["text"]}
    for vid, text in info["refs"].items():
        job["voices"][vid]["ref_text"] = text
    json.dump(job, open(os.path.join(JOB_DIR, "job.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    colab("upload", pack(), "/content/job.zip", timeout=300)
    colab("exec", "--timeout", "60", "-f", os.path.join(HERE, "unpack.py"), timeout=120)
    log("prep done in %.0f s: designed %s, transcribed %s" % (time.time() - t0, info["designs"],
                                                               {k: v[:60] for k, v in info["refs"].items()}))
    return job


def main(results, open_ui):
    os.makedirs(results, exist_ok=True)
    job = json.load(open(os.path.join(JOB_DIR, "job.json"), encoding="utf-8"))
    t0 = time.time()
    for attempt in range(4):
        try:
            if not alive():
                new_session(open_ui)
            job = prep(job, results)
            for first in range(0, len(job["items"]), CHUNK):
                ids = [it["id"] for it in job["items"][first:first + CHUNK]]
                if all(os.path.exists(os.path.join(results, i + ".wav")) for i in ids):
                    continue
                step(first, results)
            break
        except RuntimeError as e:
            log("attempt", attempt, "failed:", e, "| session alive:", alive())
    missing = [it["id"] for it in job["items"] if not os.path.exists(os.path.join(results, it["id"] + ".wav"))]
    log("missing:", missing, "| total %.0f s" % (time.time() - t0))
    subprocess.run([COLAB, "stop", "-s", SESSION], capture_output=True, stdin=subprocess.DEVNULL)
    log("session stopped")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    main(args[-1], "--open-ui" in sys.argv)
