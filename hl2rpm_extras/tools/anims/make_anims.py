"""Build the HL2RPM animation library.

    python make_anims.py [--skel male,female,police,combine] [--only <name>[,..]] [--preview <dir>] [--no-compile] [--no-hook]

Writes SMD/QC sources into modelsrc\\hl2rpm_anims\\<skeleton>, compiles
models/hl2rpm/anims/rpm_<skeleton>_anims.mdl and the thin shared models of the mod that hook the
library into every character of that skeleton (rpm_anim.build)."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from rpm_anim import build as B  # noqa: E402
from rpm_anim import rig as R  # noqa: E402
from rpm_anim import library  # noqa: E402
from rpm_anim import preview as P  # noqa: E402


def main(argv):
    skels = ["male"]
    if "--skel" in argv:
        skels = argv[argv.index("--skel") + 1].split(",")
    only = set(argv[argv.index("--only") + 1].split(",")) if "--only" in argv else None
    prev_dir = argv[argv.index("--preview") + 1] if "--preview" in argv else None
    for skel in skels:
        t0 = time.time()
        rig = R.Rig.from_model(B.SKELETONS[skel]["rig"])
        seqs = library.build(rig, skel, only)
        print("%s: %d sequences (%.1f s)" % (skel, len(seqs), time.time() - t0))
        if prev_dir:
            os.makedirs(prev_dir, exist_ok=True)
            built = [x for x in seqs if not isinstance(x, B.Cached)]
            for k in range(0, len(built), 8):
                part = built[k:k + 8]
                P.sheet([s.clip for s in part], os.path.join(prev_dir, "%s_%02d.png" % (skel, k // 8)), frames_per_clip=6,
                        labels=[s.name for s in part])
        qc, params = B.write_library(skel, seqs, rig)
        if "--no-compile" in argv:
            continue
        ok, txt = B.compile(qc)
        print("  library %s: %s" % (os.path.basename(qc), "ok" if ok else "FAILED"))
        if not ok:
            return 1
        if "--no-hook" not in argv:
            hq = B.write_hook(skel, params)
            ok, txt = B.compile(hq)
            print("  hook %s: %s" % (os.path.basename(hq), "ok" if ok else "FAILED"))
            if not ok:
                return 1
    if "--no-actbusy" not in argv:
        print("  actbusy: %s" % B.write_actbusy(library.ACTBUSY))
        print("  seats: %s" % B.write_seat_params(library.seat_params(R.Rig.from_model(B.SKELETONS["male"]["rig"]))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
