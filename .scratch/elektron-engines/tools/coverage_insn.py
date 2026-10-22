"""Instruction and branch coverage of the vectors: which of section 7's instructions their recorded calls execute.

    python coverage_insn.py OUT.txt [--set before|after] [--jobs N]

Replays the scenarios each vector file was recorded from (vectors.record with the same routines, seeds and flags,
so the same random draws) with a code hook that notes every instruction executed while a recorded call runs, and
the instruction before it. For each engine, the routines its update and render reach (inventory.py's graph), and
for the four track stages the routines they reach, it reports the instructions executed out of the routine's, and
every conditional branch of which one side was never seen: the target (taken) or the next instruction (fall).
Each run's result is cached in coverage/cache/, keyed by its recipe.
"""

from __future__ import annotations

import argparse
import hashlib
from array import array
import re
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

TOOLS = Path(__file__).parent
ROOT = TOOLS.parent
SECTION7 = ROOT / "syntakt-1.41" / "section_7_BLOB.bin"
SAMPLES = ROOT / "references" / "twinshot_samples.bin"
CACHE = ROOT / "coverage" / "cache"
sys.path.insert(0, str(TOOLS))

UPDATE = [0x40006968, 0x40007c68, 0x40006d7c, 0x40007280, 0x4000709c, 0x40007770,
          0x40008074, 0x40008580, 0x40008ae8, 0x40008e0c, 0x400092ec, 0x400097dc]
RENDER = [0x40006c9e, 0x40007f9e, 0x40006ff2, 0x400075a8, 0x40007214, 0x40007b38,
          0x4000847a, 0x40008988, 0x40008d58, 0x400091e0, 0x400096e8, 0x40009a8a]
NAMES = ["BD MODERN", "SD BASIC", "CY ALLOY", "PC CARBON", "SY TONE", "SY CHORD",
         "SD VINTAGE", "CP VINTAGE", "SY TOY", "SY BITS", "SY SWARM", "SP TWINSHOT"]
RENDER_VOICES = 0x40004324
STAGES = {"drive": 0x4000112c, "baseWidth": 0x40002320, "filter": 0x400019d6, "amp": 0x4000168e}
BD_ROUTINES = [0x40006c9e, 0x40006968, 0x400035de, 0x4000290e, 0x40003df2, 0x400049f4, 0x40004674, 0x40004ee6,
               0x400046dc, 0x40003cda, 0x40002596, 0x40004324]
COND = re.compile(r"^(?:b(?:hi|ls|cc|cs|ne|eq|vc|vs|pl|mi|ge|lt|gt|le|hs|lo)|db\w+)(?:\.[bwls])?$")


def run(file, routines, engines, blocks, prob, limit, seed, track=False, samples=False):
    return dict(file=file, routines=tuple(routines), engines=tuple(engines), blocks=blocks, prob=prob,
                limit=limit, seed=seed, track=track, samples=samples)


def before_runs() -> dict[str, list[dict]]:
    """The recipes of the existing vector files, by the group they cover."""
    groups: dict[str, list[dict]] = {}
    groups["engine 0"] = [run("bdmodern.stv", BD_ROUTINES, [0], 200, 0.5, 250, 1)]
    for n in range(1, 12):
        s = n == 11
        name = "eng11s" if s else f"eng{n}"
        groups[f"engine {n}"] = [
            run(f"{name}.stv", [UPDATE[n], RENDER[n]], [n] * 3, 150, 0.6, 250, n, track=True, samples=s),
            run(f"{name}_cont.stv", [RENDER_VOICES], [n], 250, 1.0, 400, int(f"2{n}"), samples=s)]
    groups["stages"] = [run("stages.stv", list(STAGES.values()), [0, 4, 0, 4, 0, 4], 150, 0.25, 600, 1, track=True)]
    return groups


def after_runs() -> dict[str, list[dict]]:
    """The existing recipes and the edge recordings beside them."""
    groups = before_runs()
    import edge_recipes
    for group, runs in edge_recipes.RUNS.items():
        groups.setdefault(group, []).extend(runs)
    return groups


def key(r: dict) -> str:
    return hashlib.sha1(repr(sorted(r.items())).encode()).hexdigest()[:16]


def execute(r: dict, write: Path | None = None) -> set[int]:
    """The run's coverage, from the cache when it holds it. WRITE, a directory: the run is recorded there too, as the
    vector file the recipe names, unless the file exists and the cache holds the run."""
    path = CACHE / f"{key(r)}.bin"
    if path.exists() and (write is None or (write / r["file"]).exists()):
        return set(array("Q", path.read_bytes()))
    import vectors
    cover: set[int] = set()
    extra = dict(r.get("extra", {}))
    if "bias" in extra:
        extra["bias"] = vectors.parse_bias(extra["bias"])
    cases = vectors.record(set(r["routines"]), list(r["engines"]), r["blocks"], r["seed"], r["limit"], r["prob"],
                           r["track"], SAMPLES if r["samples"] else None, cover, **extra)
    if write is not None:
        vectors.write_cases(write / r["file"], cases)
    CACHE.mkdir(parents=True, exist_ok=True)
    path.write_bytes(array("Q", sorted(cover)).tobytes())
    return cover


def routine_insns(blob: bytes, addr: int, cs) -> list[tuple[int, str, int | None, int]]:
    """(address, text, branch target or None, next address) for each instruction cfdis finds in the routine."""
    import cfdis
    lines, _ = cfdis.routine(blob, addr, 4000, cs)
    out = []
    for line in lines:
        a = int(line[:8], 16)
        text = line[10:]
        m = text.split()[0] if text.split() else ""
        target = nxt = None
        if COND.match(m):
            ins = next(cs.disasm(blob[a - cfdis.LOAD:a - cfdis.LOAD + 10], a))
            target, nxt = cfdis.target(ins.op_str.split(",")[-1]), a + ins.size
        out.append((a, text, target, nxt))
    return out


CODE_END = 0x40009f88                                       # memset's end: data follows, which a lea may name
CALLEES: dict[int, set[int]] = {}


def reach(roots: list[int]) -> list[int]:
    """inventory.py's graph, without the lea of a table that it takes for a routine."""
    import inventory
    seen, todo = set(), list(roots)
    while todo:
        a = todo.pop()
        if a in seen or a >= CODE_END or a & 1:
            continue
        seen.add(a)
        if a not in CALLEES:
            CALLEES[a] = inventory.callees(inventory.disasm(str(SECTION7), a))[0]
        todo.extend(CALLEES[a])
    return sorted(seen)


def report(group: str, roots: list[int], cover: set[int], blob: bytes, cs, out: list[str]) -> tuple[int, ...]:
    executed = {p & 0xFFFFFFFF for p in cover}
    edges = cover
    ti = te = tb = tbs = 0
    lines = []
    for r in reach(roots):
        insns = routine_insns(blob, r, cs)
        hit = sum(1 for a, *_ in insns if a in executed)
        gaps = []
        nb = nbs = 0
        for a, text, target, nxt in insns:
            if nxt is None:
                continue
            nb += 1
            taken = target is not None and (a << 32 | target) in edges
            fall = (a << 32 | nxt) in edges
            nbs += taken + fall
            if not (taken and fall):
                missing = "both" if not (taken or fall) else ("taken" if not taken else "fall")
                gaps.append(f"      {a:08x}  {text:40} missing {missing}")
        unexec = [a for a, *_ in insns if a not in executed]
        ti, te, tb, tbs = ti + len(insns), te + hit, tb + nb, tbs + nbs
        lines.append(f"  {r:08x}  insns {hit:4}/{len(insns):<4} branch sides {nbs:3}/{2 * nb:<3}"
                     + (f"  unexecuted: {ranges(unexec, insns)}" if unexec else ""))
        lines.extend(gaps)
    pi = 100 * te / ti if ti else 100
    pb = 100 * tbs / (2 * tb) if tb else 100
    out.append(f"== {group}: instructions {te}/{ti} ({pi:.1f} %), branch sides {tbs}/{2 * tb} ({pb:.1f} %)")
    out.extend(lines)
    out.append("")
    return te, ti, tbs, 2 * tb


def ranges(unexec: list[int], insns) -> str:
    """Runs of consecutive unexecuted instructions, as first-last."""
    order = [a for a, *_ in insns]
    index = {a: i for i, a in enumerate(order)}
    runs, start, prev = [], None, None
    for a in unexec:
        if start is None:
            start = prev = a
        elif index[a] == index[prev] + 1:
            prev = a
        else:
            runs.append((start, prev))
            start = prev = a
    if start is not None:
        runs.append((start, prev))
    return " ".join(f"{s:08x}" if s == e else f"{s:08x}-{e:08x}" for s, e in runs)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=Path)
    ap.add_argument("--set", choices=("before", "after"), default="before")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--write", type=Path, help="record the edge runs' vector files into this directory too")
    ap.add_argument("--only", help="comma-separated groups, e.g. 'engine 4,stages'")
    a = ap.parse_args()
    groups = before_runs() if a.set == "before" else after_runs()
    if a.only:
        groups = {g: v for g, v in groups.items() if g in a.only.split(",")}
    todo = [r for runs in groups.values() for r in runs]
    with ProcessPoolExecutor(a.jobs) as pool:
        writes = [a.write if a.write and "rev" in r else None for r in todo]
        results = dict(zip((key(r) for r in todo), pool.map(execute, todo, writes)))
    from capstone import CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_040, Cs
    cs = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_040)
    blob = SECTION7.read_bytes()
    out, summary = [], []
    for group, runs in groups.items():
        cover = set().union(*(results[key(r)] for r in runs))
        if group == "stages":
            roots = list(STAGES.values())
        else:
            n = int(group.split()[1])
            roots = [UPDATE[n], RENDER[n]]
        files = ", ".join(r["file"] for r in runs)
        te, ti, bs, bt = report(f"{group}{'' if group == 'stages' else ' ' + NAMES[int(group.split()[1])]}"
                                f" [{files}]", roots, cover, blob, cs, out)
        summary.append(f"{group:10} insns {te:5}/{ti:<5} {100 * te / ti:5.1f} %   branch sides {bs:4}/{bt:<4} "
                       f"{100 * bs / bt if bt else 100:5.1f} %")
    text = "\n".join(["Summary"] + summary + [""] + out)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(text, encoding="utf-8")
    print("\n".join(summary))
    return 0


if __name__ == "__main__":
    sys.exit(main())
