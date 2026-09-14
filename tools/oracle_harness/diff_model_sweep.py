#!/usr/bin/env python3
"""Compare the oracle and C++ model lists produced by the sweep.

Usage: python tools/oracle_harness/diff_model_sweep.py <cases.json> <oracle_models.json> <cpp_models.json>

A case DIVERGES when some oracle field differs by more than DIVERGENCE_TOL (1e-12 relative):
that is a draw-order or logic difference. Cases that differ only below that bound (last-bit
rounding) are counted separately. Divergent cases are grouped by family / subtype / modifiers, with
the first differing field of the first example of each group. Coverage of the oracle paths is printed.
"""

import contextlib
import io
import json
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import diff_targets as dt  # noqa: E402

DIVERGENCE_TOL = 1e-12


def model_differences(oracle, cpp, rel_tol):
    dt.REL_TOL = rel_tol
    with contextlib.redirect_stdout(io.StringIO()):
        return dt.model_diff(oracle, cpp)


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(2)
    loaded = []
    for path in sys.argv[1:]:
        with open(path, encoding="utf-8") as f:
            loaded.append(json.load(f))
    cases, oracle_models, cpp_models = loaded
    if not (len(cases) == len(oracle_models) == len(cpp_models)):
        sys.exit(f"length mismatch: cases {len(cases)}, oracle {len(oracle_models)}, C++ {len(cpp_models)}")

    groups = defaultdict(list)
    rounding_only = 0
    coverage = {"subtype": Counter(), "modifier": Counter(), "type": Counter(), "spectral": Counter()}

    for case, om, cm in zip(cases, oracle_models, cpp_models):
        coverage["subtype"][f"{om['family']}/{om['subtype']}"] += 1
        coverage["modifier"].update(om.get("modifiers", []))
        coverage["type"].update(c["type"] for c in om["components"])
        coverage["spectral"].update(c.get("spectralModel", "?") for c in om["components"])

        diffs = model_differences(om, cm, DIVERGENCE_TOL)
        if diffs:
            key = (om["family"], om["subtype"], ",".join(om.get("modifiers", [])) or "-")
            groups[key].append((case, diffs))
        elif model_differences(om, cm, 0.0):
            rounding_only += 1

    divergent = sum(len(v) for v in groups.values())
    print(f"cases {len(cases)}: identical {len(cases) - divergent - rounding_only}, "
          f"last-bit rounding only {rounding_only}, DIVERGENT {divergent}")
    for key, items in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        case, diffs = items[0]
        path, a, b, rel = diffs[0]
        rel_s = f" rel {rel:.3g}" if rel is not None else ""
        print(f"  {len(items):4} x {key[0]}/{key[1]} mods[{key[2]}]  e.g. seed {case['seed']} "
              f"{case['complexity']}: {len(diffs)} fields, first {path}: oracle {a!r} C++ {b!r}{rel_s}")

    print("coverage (oracle):")
    for name, counter in coverage.items():
        print(f"  {name}: " + ", ".join(f"{k} {v}" for k, v in sorted(counter.items())))


if __name__ == "__main__":
    main()
