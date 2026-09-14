#!/usr/bin/env python3
"""Diff the oracle's target model and sky (dump_oracle_target.js) against the C++ dump (dump_cpp_target).

Usage: python tools/oracle_harness/diff_targets.py <oracle.json> <cpp.json>

Only fields the ORACLE sets are compared (the C++ struct carries every field with a default).
Keys are matched ignoring case and underscores (innerDiskAU == inner_disk_au). Numbers differ when
their relative difference exceeds REL_TOL. Differences are listed in traversal order: model header,
physical, temporal, modifiers, then components in order.
"""

import json
import math
import sys

REL_TOL = 1e-12
SKY_REL_TOL = 1e-9


def norm(key):
    return key.replace("_", "").lower()


def compare(path, a, b, out):
    if isinstance(a, bool) or isinstance(b, bool):
        if bool(a) != bool(b):
            out.append((path, a, b, None))
        return
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        if a == b:
            return
        scale = max(abs(a), abs(b))
        rel = abs(a - b) / scale if scale > 0 else math.inf
        if rel > REL_TOL:
            out.append((path, a, b, rel))
        return
    if isinstance(a, list):
        if not isinstance(b, list):
            out.append((path, a, b, None))
            return
        if len(a) != len(b):
            out.append((path + ".length", len(a), len(b), None))
        for i, (x, y) in enumerate(zip(a, b)):
            compare(f"{path}[{i}]", x, y, out)
        return
    if isinstance(a, dict):
        if not isinstance(b, dict):
            out.append((path, a, b, None))
            return
        by_norm = {norm(k): v for k, v in b.items()}
        for k, v in a.items():
            if norm(k) not in by_norm:
                out.append((f"{path}.{k}", v, "<absent in C++>", None))
                continue
            compare(f"{path}.{k}", v, by_norm[norm(k)], out)
        return
    if a != b:
        out.append((path, a, b, None))


def component_table(oracle, cpp):
    rows = max(len(oracle), len(cpp))
    print(f"components: oracle {len(oracle)}, C++ {len(cpp)}")
    for i in range(rows):
        o = oracle[i] if i < len(oracle) else {}
        c = cpp[i] if i < len(cpp) else {}
        mark = "" if (o.get("id"), o.get("type")) == (c.get("id"), c.get("type")) else "   <-- differs"
        print(f"  [{i}] oracle {o.get('id')}/{o.get('type')}  |  C++ {c.get('id')}/{c.get('type')}{mark}")


def model_diff(om, cm):
    out = []
    for key in ("family", "subtype", "rarity", "thetaObj", "fovMul"):
        if key in om and key in cm:
            compare(key, om[key], cm[key], out)
    compare("physical", om.get("physical", {}), cm.get("physical", {}), out)
    compare("temporal", om.get("temporal", {}), cm.get("temporal", {}), out)
    compare("modifiers", om.get("modifiers", []), cm.get("modifiers", []), out)
    component_table(om.get("components", []), cm.get("components", []))
    for i, (oc, cc) in enumerate(zip(om.get("components", []), cm.get("components", []))):
        compare(f"components[{i}]({oc.get('id')})", oc, cc, out)
    return out


def sky_diff(osky, csky, n):
    peak = max(max(osky), max(csky))
    bad = [(abs(a - b), i) for i, (a, b) in enumerate(zip(osky, csky)) if abs(a - b) > SKY_REL_TOL * peak]
    print(f"sky: flux oracle {sum(osky):.17g}, C++ {sum(csky):.17g}, peak {peak:.6g}")
    if not bad:
        print("sky: identical within %.0e of peak" % SKY_REL_TOL)
        return
    rows = [i // n for _, i in bad]
    cols = [i % n for _, i in bad]
    first = bad[0][1]
    print(f"sky: {len(bad)} pixels differ; bbox rows {min(rows)}..{max(rows)}, cols {min(cols)}..{max(cols)}; "
          f"first (row {first // n}, col {first % n})")
    for d, i in sorted(bad, reverse=True)[:5]:
        print(f"  (row {i // n}, col {i % n}): oracle {osky[i]:.10g}, C++ {csky[i]:.10g}, |d| {d:.3g}")


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    with open(sys.argv[1], encoding="utf-8") as f:
        oracle = json.load(f)
    with open(sys.argv[2], encoding="utf-8") as f:
        cpp = json.load(f)

    print(f"fixture {oracle['fixture']} {oracle['scenario']}")
    if "battery" in oracle:
        b = oracle["battery"]
        print(f"harness check: oracle flux {oracle['flux']!r} vs battery {b['fluxTotal']!r}; "
              f"thetaFov {oracle['thetaFovRad']!r} vs battery {b['thetaFovRad']!r}")

    diffs = model_diff(oracle["model"], cpp["model"])
    if diffs:
        print(f"model: {len(diffs)} differing fields (first listed = first in traversal order)")
        for path, a, b, rel in diffs[:40]:
            rel_s = f"  rel {rel:.3g}" if rel is not None else ""
            print(f"  {path}: oracle {a!r}  C++ {b!r}{rel_s}")
    else:
        print("model: identical on every oracle field")

    sky_diff(oracle["sky"], cpp["sky"], oracle["gridN"])


if __name__ == "__main__":
    main()
