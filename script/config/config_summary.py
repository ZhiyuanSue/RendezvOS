import json
import os
import sys
from typing import Dict, List, Tuple


def _parse_env_line(line: str) -> Tuple[str, str]:
    s = line.strip()
    if not s or s.startswith("#"):
        return ("", "")
    for op in ("+=", ":=", "="):
        if op in s:
            left, right = s.split(op, 1)
            return (left.strip(), right.strip())
    return ("", "")


def _read_env_vars(path: str) -> Dict[str, str]:
    out: Dict[str, str] = {}
    if not os.path.isfile(path):
        return out
    with open(path, "r") as f:
        for line in f:
            k, v = _parse_env_line(line)
            if not k:
                continue
            out[k] = v
    return out


def _extract_macros(flags: str) -> Dict[str, List[str]]:
    toks = flags.split()
    defs: List[str] = []
    undefs: List[str] = []
    i = 0
    while i < len(toks):
        t = toks[i]
        if t == "-D" and i + 1 < len(toks):
            defs.append(toks[i + 1])
            i += 2
            continue
        if t.startswith("-D") and len(t) > 2:
            defs.append(t[2:])
            i += 1
            continue
        if t == "-U" and i + 1 < len(toks):
            undefs.append(toks[i + 1])
            i += 2
            continue
        if t.startswith("-U") and len(t) > 2:
            undefs.append(t[2:])
            i += 1
            continue
        i += 1

    def _dedup(xs: List[str]) -> List[str]:
        seen = set()
        out2 = []
        for x in xs:
            if x in seen:
                continue
            seen.add(x)
            out2.append(x)
        return out2

    return {"defines": _dedup(defs), "undefines": _dedup(undefs)}


def main() -> int:
    # Usage:
    #   core-only:
    #     config_summary.py <core_dir> <out_dir>
    #   integrated:
    #     config_summary.py <core_dir> <out_dir> <root_env_path>
    if len(sys.argv) not in (3, 4):
        print("usage: config_summary.py <core_dir> <out_dir> [root_env_path]")
        return 2

    core = sys.argv[1]
    out_dir = sys.argv[2]
    root_env_path = sys.argv[3] if len(sys.argv) == 4 else ""
    core_env = os.path.join(core, "Makefile.env")
    core_kernel_env = os.path.join(core, "kernel", "Makefile.env")

    core_vars = _read_env_vars(core_env)
    core_kernel_vars = _read_env_vars(core_kernel_env)

    core_kernel_cflags = core_kernel_vars.get("CFLAGS", "")

    summary: Dict = {
        "paths": {
            "core_makefile_env": core_env,
            "core_kernel_makefile_env": core_kernel_env,
        },
        "core": {
            "makefile_env": core_vars,
            "kernel_makefile_env": core_kernel_vars,
            "kernel_cflags_raw": core_kernel_cflags,
            "kernel_cflags_macros": _extract_macros(core_kernel_cflags),
        },
    }

    if root_env_path:
        root_vars = _read_env_vars(root_env_path)
        root_extra = root_vars.get("ROOT_EXTRA_CFLAGS", "")
        core_override = root_vars.get("CORE_OVERRIDE_CFLAGS", "")
        summary["paths"]["root_makefile_env"] = root_env_path
        summary["root"] = {
            "makefile_env": root_vars,
            "root_extra_cflags_raw": root_extra,
            "root_extra_cflags_macros": _extract_macros(root_extra),
        }
        summary["integrated_overrides"] = {
            "core_override_cflags_raw": core_override,
            "core_override_cflags_macros": _extract_macros(core_override),
        }

    os.makedirs(out_dir, exist_ok=True)
    out_json = os.path.join(out_dir, "config_summary.json")
    out_txt = os.path.join(out_dir, "config_summary.txt")

    with open(out_json, "w") as f:
        json.dump(summary, f, indent=2, sort_keys=True)
        f.write("\n")

    def fmt_list(xs: List[str], indent: str = "    ", wrap: int = 6) -> str:
        """
        Pretty-print a macro list as multiple lines:
          (N total)
            - A
            - B
        """
        if not xs:
            return "(0 total)\n" + indent + "- (none)"
        lines = [f"({len(xs)} total)"]
        for x in xs:
            lines.append(f"{indent}- {x}")
        return "\n".join(lines)

    with open(out_txt, "w") as f:
        f.write("[core/kernel CFLAGS macros]\n")
        f.write("  defines:\n")
        f.write(f"    {fmt_list(summary['core']['kernel_cflags_macros']['defines'], indent='    ')}\n")
        f.write("  undefines:\n")
        f.write(f"    {fmt_list(summary['core']['kernel_cflags_macros']['undefines'], indent='    ')}\n")

        if "root" in summary:
            f.write("\n[root extra CFLAGS macros]\n")
            f.write("  defines:\n")
            f.write(f"    {fmt_list(summary['root']['root_extra_cflags_macros']['defines'], indent='    ')}\n")
            f.write("  undefines:\n")
            f.write(f"    {fmt_list(summary['root']['root_extra_cflags_macros']['undefines'], indent='    ')}\n")

        if "integrated_overrides" in summary:
            f.write("\n[core overrides (integrated build)]\n")
            f.write("  defines:\n")
            f.write(f"    {fmt_list(summary['integrated_overrides']['core_override_cflags_macros']['defines'], indent='    ')}\n")
            f.write("  undefines:\n")
            f.write(f"    {fmt_list(summary['integrated_overrides']['core_override_cflags_macros']['undefines'], indent='    ')}\n")

        f.write("\n[paths]\n")
        for k, v in summary["paths"].items():
            f.write(f"  {k}: {v}\n")

    print(f"Wrote {out_json}")
    print(f"Wrote {out_txt}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

