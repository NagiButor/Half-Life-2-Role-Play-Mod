# @title HL2RPM voice: setup on the Colab VM (packages, job, models)
import os, subprocess, sys, time, zipfile

t0 = time.time()
r = subprocess.run([sys.executable, "-m", "pip", "install", "-q", "qwen-tts", "ruaccent", "num2words"],
                   capture_output=True, text=True)
print("pip:", r.returncode, (r.stdout + r.stderr)[-800:])
import torch
print("torch", torch.__version__, torch.cuda.get_device_name(0) if torch.cuda.is_available() else "NO GPU")
os.makedirs("/content/job", exist_ok=True)
with zipfile.ZipFile("/content/job.zip") as z:
    z.extractall("/content/job")
from huggingface_hub import snapshot_download, hf_hub_download
snapshot_download("Qwen/Qwen3-TTS-12Hz-1.7B-Base")
snapshot_download("openai/whisper-large-v3-turbo", allow_patterns=["*.json", "*.safetensors", "*.txt"])
for f in ("lora/adapter.json", "lora/adapter.safetensors"):
    hf_hub_download("sknyazev/qwen3-tts-12hz-1.7b-ru-stress-gguf", f)
print("setup done in %.0f s" % (time.time() - t0))
