# @title HL2RPM voice: unpack a fresh job.zip over /content/job
import zipfile
with zipfile.ZipFile("/content/job.zip") as z:
    z.extractall("/content/job")
print("job unpacked")
