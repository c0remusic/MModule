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


def gap(r: dict, sets: str, start: int = 0, **extra) -> dict:
    """A recording for one gap of after.txt: params bytes forced (vectors.py --set), no edge draws."""
    r["extra"] = {"sets": sets, **({"start": start} if start else {}), **extra}
    r["rev"] = REV
    return r


# The four gaps after.txt left that an input reaches (gaps_*.stv, measured in after_gaps.txt):
# - The track amp's HOLD at 127 (0x40001570 fall): track 1 with ATK 0, HOLD 127, MODE AHD, hit at blocks 0 and 2,
#   then no trig and no gate-off; the hold counts table[127] = 0x75300 samples (15000 blocks) before its test.
#   Only the blocks from 15000 on are recorded.
# - baseWidth on a trig with the amp's RST word at 0 (0x4000239a, 0x400023be taken): every track's RST word is 0.
# - SP TWINSHOT: byte 12 on entry 2 (forward, two samples, looping with AMP HOLD, bytes 14-15), no trig after block
#   2, then entry 9 (a length, no data) from block 8, so that player B's next end finds no loop data (0x40006812
#   taken); then entry 7 (empty) from block 16 at every update (0x400099da taken), with a trig in every block from
#   20 (0x40009a66 taken).
# - SY BITS: a gate-off in every block, the hits' included (0x40003de8 taken: a trig keeps the release off).
RST0 = ";".join(f"{0xEC + 14 * t:x}=0;{0xED + 14 * t:x}=0" for t in range(8))
RUNS["stages"] += [
    gap(run("gaps_hold.stv", [STAGES["amp"]], [0], 15020, 1.0, 20, 1), "e0=0;e2=7f;ea=0;180=0@3;181=0@3", 15000),
    gap(run("gaps_basewidth.stv", [STAGES["baseWidth"]], [0], 40, 1.0, 40, 1, track=True), RST0, others=True)]
RUNS["engine 11"].append(gap(run("gaps_twinshot.stv", [UPDATE[11], RENDER[11]], [11], 30, 1.0, 30, 1, samples=True),
                             "c=42;e=40;180=0@3;181=0@3;c=49@8;c=47@16;180=1@20"))
RUNS["engine 9"].append(gap(run("gaps_sybits.stv", [RENDER[9]], [9], 8, 1.0, 8, 1), "181=1"))


def command(r: dict) -> str:
    routines = ",".join(f"{a:08x}" for a in r["routines"])
    engines = ",".join(str(e) for e in r["engines"])
    extra = r["extra"]
    flags = (" --edge" if extra.get("edge") else "") + (" --track" if r["track"] else "")
    flags += " --samples references/twinshot_samples.bin" if r["samples"] else ""
    flags += (" --others" if extra.get("others") else "") + (f" --bias '{extra['bias']}'" if "bias" in extra else "")
    flags += (f" --set '{extra['sets']}'" if "sets" in extra else "") + (f" --start {extra['start']}"
                                                                         if "start" in extra else "")
    return (f"tools/vectors.py vectors/{r['file']} {routines} --engines {engines} --blocks {r['blocks']} "
            f"--prob {r['prob']} --limit {r['limit']} --seed {r['seed']}{flags}")


if __name__ == "__main__":
    for runs in RUNS.values():
        for r in runs:
            print(command(r))
