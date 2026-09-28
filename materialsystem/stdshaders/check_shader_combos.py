"""HL2RPM: find compiled shaders (.vcs) that don't match the C++ that uses them.

A mismatch makes the engine request a combo index that the .vcs doesn't have
-> "Using invalid shader combo" -> Error() -> crash (e.g. bullet decals, 2026-09).

For every .vcs in the mod's shaders/fxc this compares:
  1) the combo count in the .vcs header with the count implied by the .inc that
     game_shader_dx9 is compiled against (first match in the include-path order
     of game_shader_dx9_episodic.vcxproj: fxctmp9, vshtmp9, include);
  2) the GetIndex() formulas of that .inc with the .inc ShaderCompile generated
     next to the .vcs (include/), which catches reordered combos too.

Usage: python check_shader_combos.py [mod_shaders_fxc_dir]
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MOD_FXC = sys.argv[1] if len(sys.argv) > 1 else r"E:\Steam\steamapps\sourcemods\hl2rpm\shaders\fxc"
CPP_INC_DIRS = ["fxctmp9", "vshtmp9", "include"]  # order from the vcxproj
GENERATED_INC_DIR = "include"                     # where ShaderCompile235 writes

RE_RETURN = re.compile(r"return\s*(\(.*?)\+\s*0\s*;", re.S)
RE_TERM = re.compile(r"\(\s*(\d+)\s*\*\s*m_n(\w+)\s*\)")
RE_RANGE = re.compile(r"void\s+Set(\w+)\s*\(\s*int\s+i\s*\)\s*\{\s*Assert\(\s*i\s*>=\s*(-?\d+)\s*&&\s*i\s*<=\s*(-?\d+)\s*\)")


def read_text(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def parse_inc(path):
    """-> (formulas as list of [(mult, name)], combo count)"""
    text = read_text(path)
    # Combos like "1..100" are stored as (i - min), so the span is max - min.
    ranges = {m.group(1): int(m.group(3)) - int(m.group(2)) for m in RE_RANGE.finditer(text)}
    formulas = []
    total_max = 0
    for m in RE_RETURN.finditer(text):
        terms = [(int(a), b) for a, b in RE_TERM.findall(m.group(1))]
        formulas.append(terms)
        for mult, name in terms:
            total_max += mult * ranges.get(name, 0)
    # Static formulas are offset by the dynamic count; total = max index + 1.
    return formulas, total_max + 1


def vcs_total_combos(path):
    with open(path, "rb") as f:
        head = f.read(12)
    if len(head) < 12:
        return None
    version, total, dynamic = struct.unpack("<iii", head)
    return total


def find_inc(name, dirs):
    for d in dirs:
        p = os.path.join(HERE, d, name + ".inc")
        if os.path.exists(p):
            return p
    return None


def fmt(formulas):
    return " | ".join(" + ".join(f"{m}*{n}" for m, n in terms) for terms in formulas)


def main():
    bad = 0
    checked = 0
    for fname in sorted(os.listdir(MOD_FXC)):
        if not fname.lower().endswith(".vcs"):
            continue
        name = fname[:-4]
        cpp_inc = find_inc(name, CPP_INC_DIRS)
        if not cpp_inc:
            continue  # stock Valve shader (not built by us) or unused
        checked += 1
        problems = []

        cpp_formulas, cpp_total = parse_inc(cpp_inc)
        vcs_total = vcs_total_combos(os.path.join(MOD_FXC, fname))
        if vcs_total is not None and vcs_total != cpp_total:
            problems.append(f"combo count: .vcs has {vcs_total}, C++ .inc expects {cpp_total}")

        gen_inc = find_inc(name, [GENERATED_INC_DIR])
        if gen_inc and os.path.normcase(gen_inc) != os.path.normcase(cpp_inc):
            gen_formulas, _ = parse_inc(gen_inc)
            if gen_formulas != cpp_formulas:
                problems.append("combo order/set differs between C++ .inc and compiled .inc:\n"
                                f"        C++  ({os.path.relpath(cpp_inc, HERE)}): {fmt(cpp_formulas)}\n"
                                f"        .vcs ({os.path.relpath(gen_inc, HERE)}): {fmt(gen_formulas)}")

        if problems:
            bad += 1
            print(f"MISMATCH {fname}")
            for p in problems:
                print(f"    {p}")

    print(f"\nchecked {checked} shaders, {bad} mismatched")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
