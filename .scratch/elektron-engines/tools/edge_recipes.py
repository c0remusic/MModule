"""The edge recordings (vectors.py --edge): their recipes, for coverage_insn.py --set after, and their command lines.

    python edge_recipes.py        prints a vectors.py command per file
"""

from coverage_insn import RENDER, RENDER_VOICES, STAGES, UPDATE, run

REV = 6                                                     # vectors.py's edge draws, in coverage_insn.py's cache key
# Machine bytes an engine compares with one value, drawn from those values half the time (hexadecimal):
# - BD MODERN: TUNE (bytes 2-3) high, where a high note passes the pitch clamp at 127 (its pitch is note - 88 + TUNE).
# - SY CHORD: byte 6 (the chord) on 1, 17 and 18; byte 4 above 100 and 120.
# - SD VINTAGE: byte 10 at 0x7f; bytes 12-13 (the drive word) at 0.
# - SY BITS: the word at bytes 4-5 on 0x3400, 0x4000 and 0x4c00.
# - SY SWARM: byte 12 on 0 and 1.
# - SP TWINSHOT: its two sample bytes (4 and 12) on entries 7 (empty), 9 (no data, twice as often on byte 12, whose
#   player loops) and 10 (beyond the clamp), each from above 0x40 and from below; byte 16 at 0x7f.
BIAS = {
    0: "2=70,7f",
    5: "4=70,7f;6=1,11,12",
    6: "10=7f;12=0;13=0",
    9: "4=34,40,4c;5=0",
    10: "12=0,1",
    11: "4=47,38,49,36,4a,35;12=47,38,49,36,49,36,4a,35;16=7f",
}


def edge(r: dict, **extra) -> dict:
    r["extra"] = {"edge": True, **extra}
    r["rev"] = REV
    return r


RUNS: dict[str, list[dict]] = {}
for n in range(12):
    s = n == 11
    bias = {"bias": BIAS[n]} if n in BIAS else {}
    RUNS[f"engine {n}"] = [
        edge(run(f"eng{n}_edge.stv", [UPDATE[n], RENDER[n]], [n] * 8, 120, 0.26, 250, 100 + n, track=True,
                 samples=s), **bias),
        edge(run(f"eng{n}_edge_cont.stv", [RENDER_VOICES], [n], 500, 1.0, 500, 200 + n, track=True, samples=s),
             **bias)]
# The filter runs 8 times a block, the other stages once: a file of its own, so that each records as many blocks
FILTER = STAGES["filter"]
RUNS["stages"] = [
    edge(run("stages_edge.stv", [a for a in STAGES.values() if a != FILTER], [0, 4] * 4, 200, 0.12, 175, 300,
             track=True), others=True),
    edge(run("stages_filter_edge.stv", [FILTER], [0, 4] * 4, 200, 0.05, 500, 301, track=True), others=True)]


def command(r: dict) -> str:
    routines = ",".join(f"{a:08x}" for a in r["routines"])
    engines = ",".join(str(e) for e in r["engines"])
    extra = r["extra"]
    flags = (" --track" if r["track"] else "") + (" --samples references/twinshot_samples.bin" if r["samples"] else "")
    flags += (" --others" if extra.get("others") else "") + (f" --bias '{extra['bias']}'" if "bias" in extra else "")
    return (f"tools/vectors.py vectors/{r['file']} {routines} --engines {engines} --blocks {r['blocks']} "
            f"--prob {r['prob']} --limit {r['limit']} --seed {r['seed']} --edge{flags}")


if __name__ == "__main__":
    for runs in RUNS.values():
        for r in runs:
            print(command(r))
