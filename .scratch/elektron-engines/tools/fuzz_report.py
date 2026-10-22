"""Coverage per routine of the forced-state vectors, alone and with the recorded ones.

    python fuzz_report.py OUT.txt COVER [COVER...]

COVER: fuzz_states.py --cover files. The recorded vectors' coverage is coverage_insn.py's cache for its "after" set
(the runs not cached yet are named and left out). Every routine the engines' update and render and the track stages
reach is reported once, whoever calls it, in three columns: recorded, forced, both; then the sides still missing.
Groups: shared (reached from two engines or more), stages, and each engine's own routines.
"""

from __future__ import annotations

import sys
from array import array
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import coverage_insn as ci  # noqa: E402
from capstone import CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_040, Cs  # noqa: E402


def load(path: Path) -> set[int]:
    return set(array("Q", path.read_bytes()))


def measure(insns, cover):
    executed = {p & 0xFFFFFFFF for p in cover}
    hit = sum(1 for a, *_ in insns if a in executed)
    nb = sides = 0
    missing = []
    for a, text, target, nxt in insns:
        if nxt is None:
            continue
        nb += 1
        taken = target is not None and (a << 32 | target) in cover
        fall = (a << 32 | nxt) in cover
        sides += taken + fall
        if not (taken and fall):
            missing.append((a, text, "both" if not (taken or fall) else ("taken" if not taken else "fall")))
    unexec = [a for a, *_ in insns if a not in executed]
    return hit, len(insns), sides, 2 * nb, missing, unexec


def main() -> int:
    out = Path(sys.argv[1])
    forced = set().union(*(load(Path(p)) for p in sys.argv[2:]))
    recorded, absent = set(), []
    for runs in ci.after_runs().values():
        for r in runs:
            p = ci.CACHE / f"{ci.key(r)}.bin"
            if p.exists():
                recorded |= load(p)
            else:
                absent.append(r["file"])
    both = recorded | forced
    cs = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_040)
    blob = ci.SECTION7.read_bytes()
    owners: dict[int, set[str]] = {}
    for n in range(12):
        for r in ci.reach([ci.UPDATE[n], ci.RENDER[n]]):
            owners.setdefault(r, set()).add(f"engine {n:2} {ci.NAMES[n]}")
    for r in ci.reach(list(ci.STAGES.values())):
        owners.setdefault(r, set()).add("stages")
    groups: dict[str, list[int]] = {}
    for r, o in owners.items():
        g = "stages" if o == {"stages"} else ("shared" if len(o) > 1 else next(iter(o)))
        groups.setdefault(g, []).append(r)
    lines, summary = [], []
    for g in sorted(groups):
        tot = [[0, 0, 0, 0] for _ in range(3)]
        body = []
        for r in sorted(groups[g]):
            insns = ci.routine_insns(blob, r, cs)
            cols = [measure(insns, c) for c in (recorded, forced, both)]
            for t, m in zip(tot, cols):
                for i in range(4):
                    t[i] += m[i]
            body.append(f"  {r:08x}  " + "   ".join(f"{m[0]:4}/{m[1]:<4} {m[2]:3}/{m[3]:<3}" for m in cols))
            hit, n, s, ns, missing, unexec = cols[2]
            if unexec:
                body.append(f"      unexecuted: {ci.ranges(unexec, insns)}")
            for a, text, side in missing:
                body.append(f"      {a:08x}  {text:40} missing {side}")
        pct = lambda t: f"{100 * t[0] / t[1]:5.1f} % insns, {100 * t[2] / t[3] if t[3] else 100:5.1f} % sides"
        summary.append(f"{g:24} recorded {pct(tot[0])} | forced {pct(tot[1])} | both {pct(tot[2])}"
                       f"  ({tot[2][0]}/{tot[2][1]}, {tot[2][2]}/{tot[2][3]})")
        lines.append(f"== {g}  (columns: recorded, forced, both; insns then branch sides)")
        lines.extend(body)
        lines.append("")
    head = ["Summary"] + summary + [""]
    if absent:
        head += ["Recorded runs not in the cache, left out: " + ", ".join(sorted(set(absent))), ""]
    out.write_text("\n".join(head + lines), encoding="utf-8")
    print("\n".join(head))
    return 0


if __name__ == "__main__":
    sys.exit(main())
