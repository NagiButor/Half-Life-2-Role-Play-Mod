"""Pictures of a VMF without compiling (the Creation Kit's renderer, dlgedit/world/vmfshot.py): top / front / side
Hammer-like 2D views, a shaded 3D view, a close-up of every AI task marker, info.json.
usage: python vmf_shot.py <map.vmf> <out dir> [--size 1400]"""
import os
import sys

CK = r"E:\SourceModding\HL2RPMSOURCE\source-sdk-2013-mapbase-v8.0\sp\src\hl2rpm_extras\tools\dialog_editor"
sys.path.insert(0, CK)
from dlgedit.world import vmfshot  # noqa: E402

if __name__ == "__main__":
    sys.exit(vmfshot.main(sys.argv))
