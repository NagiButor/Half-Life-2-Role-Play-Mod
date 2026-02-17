import argparse
import os
import re
from dataclasses import dataclass


@dataclass(frozen=True)
class ComboVar:
	name: str
	min_value: int
	max_value: int

	@property
	def size(self) -> int:
		return (self.max_value - self.min_value) + 1


_COMBO_RE = re.compile(r'^\s*//\s*(STATIC|DYNAMIC):\s*"([^"]+)"\s*"([^"]+)"\s*$', re.IGNORECASE)
_RANGE_RE = re.compile(r'^\s*(\d+)\s*(?:\.\.\s*(\d+))?\s*$')


def _parse_range(s: str) -> tuple[int, int]:
	m = _RANGE_RE.match(s)
	if not m:
		raise ValueError(f"Bad range: {s!r}")
	lo = int(m.group(1))
	hi = int(m.group(2) or m.group(1))
	if hi < lo:
		raise ValueError(f"Bad range: {s!r}")
	return lo, hi


def _extract_combos(fxc_path: str) -> tuple[list[ComboVar], list[ComboVar]]:
	static_vars: list[ComboVar] = []
	dynamic_vars: list[ComboVar] = []
	with open(fxc_path, "r", encoding="utf-8", errors="replace") as f:
		for line in f:
			m = _COMBO_RE.match(line)
			if not m:
				continue
			kind = m.group(1).upper()
			name = m.group(2).strip()
			lo, hi = _parse_range(m.group(3))
			var = ComboVar(name=name, min_value=lo, max_value=hi)
			if kind == "STATIC":
				static_vars.append(var)
			else:
				dynamic_vars.append(var)
	return static_vars, dynamic_vars


def _shader_prefix(shader_name: str) -> str:
	lower = shader_name.lower()
	if "_vs" in lower:
		return "vsh"
	return "psh"


def _compute_weights(vars_: list[ComboVar], base: int) -> list[int]:
	weights: list[int] = []
	w = base
	for v in vars_:
		weights.append(w)
		w *= v.size
	return weights


def _emit_index_class(shader_name: str, class_suffix: str, vars_: list[ComboVar], weights: list[int]) -> str:
	lines: list[str] = []
	lines.append(f"class {shader_name}_{class_suffix}_Index")
	lines.append("{")
	for v in vars_:
		lines.append("private:")
		lines.append(f"\tint m_n{v.name};")
		lines.append("#ifdef _DEBUG")
		lines.append(f"\tbool m_b{v.name};")
		lines.append("#endif")
		lines.append("public:")
		lines.append(f"\tvoid Set{v.name}( int i )")
		lines.append("\t{")
		lines.append(f"\t\tAssert( i >= {v.min_value} && i <= {v.max_value} );")
		lines.append(f"\t\tm_n{v.name} = i;")
		lines.append("#ifdef _DEBUG")
		lines.append(f"\t\tm_b{v.name} = true;")
		lines.append("#endif")
		lines.append("\t}")
		lines.append(f"\tvoid Set{v.name}( bool i )")
		lines.append("\t{")
		lines.append(f"\t\tm_n{v.name} = i ? 1 : 0;")
		lines.append("#ifdef _DEBUG")
		lines.append(f"\t\tm_b{v.name} = true;")
		lines.append("#endif")
		lines.append("\t}")
	lines.append("public:")
	lines.append(f"\t{shader_name}_{class_suffix}_Index()")
	lines.append("\t{")
	for v in vars_:
		lines.append("#ifdef _DEBUG")
		lines.append(f"\t\tm_b{v.name} = false;")
		lines.append("#endif // _DEBUG")
		lines.append(f"\t\tm_n{v.name} = 0;")
	lines.append("\t}")
	lines.append("\tint GetIndex()")
	lines.append("\t{")
	lines.append("\t\t// Asserts to make sure that we aren't using any skipped combinations.")
	lines.append("\t\t// Asserts to make sure that we are setting all of the combination vars.")
	lines.append("#ifdef _DEBUG")
	if vars_:
		and_expr = " && ".join([f"m_b{v.name}" for v in vars_])
		lines.append(f"\t\tbool bAll{class_suffix}VarsDefined = {and_expr};")
		lines.append(f"\t\tAssert( bAll{class_suffix}VarsDefined );")
	else:
		lines.append(f"\t\tbool bAll{class_suffix}VarsDefined = true;")
		lines.append(f"\t\tAssert( bAll{class_suffix}VarsDefined );")
	lines.append("#endif // _DEBUG")
	if vars_:
		terms = [f"( {weights[i]} * m_n{vars_[i].name} )" for i in range(len(vars_))]
		lines.append(f"\t\treturn {' + '.join(terms)} + 0;")
	else:
		lines.append("\t\treturn 0;")
	lines.append("\t}")
	lines.append("};")
	return "\n".join(lines)


def _emit_test_macro(shader_name: str, class_suffix: str, vars_: list[ComboVar]) -> str:
	prefix = _shader_prefix(shader_name)
	which = "static" if class_suffix == "Static" else "dynamic"
	if not vars_:
		return f"#define shader{class_suffix}Test_{shader_name} 0"
	terms = [f"{prefix}_forgot_to_set_{which}_{v.name}" for v in vars_]
	return f"#define shader{class_suffix}Test_{shader_name} " + " + ".join(terms) + " + 0"


def generate_inc_for_fxc(fxc_path: str, out_dir: str) -> str:
	base_name = os.path.splitext(os.path.basename(fxc_path))[0]
	shader_name = base_name.lower()
	static_vars, dynamic_vars = _extract_combos(fxc_path)
	dynamic_total = 1
	for v in dynamic_vars:
		dynamic_total *= v.size
	static_weights = _compute_weights(static_vars, dynamic_total)
	dynamic_weights = _compute_weights(dynamic_vars, 1)

	out_lines: list[str] = []
	out_lines.append('#include "shaderlib/cshader.h"')
	out_lines.append(_emit_index_class(shader_name, "Static", static_vars, static_weights))
	out_lines.append(_emit_test_macro(shader_name, "Static", static_vars))
	out_lines.append(_emit_index_class(shader_name, "Dynamic", dynamic_vars, dynamic_weights))
	out_lines.append(_emit_test_macro(shader_name, "Dynamic", dynamic_vars))
	out_text = "\n".join(out_lines) + "\n"

	out_path = os.path.join(out_dir, f"{base_name}.inc")
	with open(out_path, "w", encoding="utf-8", newline="\n") as f:
		f.write(out_text)
	return out_path


def main() -> int:
	ap = argparse.ArgumentParser()
	ap.add_argument("fxc", nargs="+")
	ap.add_argument("--out-dir", default=".")
	args = ap.parse_args()

	for fxc in args.fxc:
		generate_inc_for_fxc(fxc, args.out_dir)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
