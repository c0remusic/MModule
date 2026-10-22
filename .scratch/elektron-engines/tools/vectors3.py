"""Record test vectors for the C++ port of CPU #1's routines (section 3, OS 1.41), run alone on oracle3's rig.

    python vectors3.py OUT.stv [--routines 4008ed48,...] [--cases N] [--seed S] [--coverage REPORT.txt]

Each case is one call of one routine on a state drawn for it: random but plausible values for the structures the
routine reads (cpu1.md and the disassembly), a quarter of them at edges. The rig records the bytes the call read and
wrote; the C++ test loads the read set into the port's memory, runs the port, and compares the write set and d0/d1.

File format "ST1V", little-endian integers, bytes in address order as the big-endian CPU sees them:
    "ST1V", u32 count; per case:
        u32 routine, u32 macsr, u32 nargs, nargs x u32 stack arguments (the C convention, first at 4(a7)),
        u32 d0 at return, u32 d1 at return,
        u32 nread runs, per run: u32 address, u32 length, length bytes, the value before the call of each byte read,
        u32 nwrite runs, the same with the value after the call of each byte written.
    The call's own stack frame is in neither set, nor are section 3's bytes read with the file's value.

With --coverage, every instruction step the calls take is noted, and the report gives, for each routine and what it
calls, the instructions executed and every conditional branch of which a side was never taken.
The vectors hold firmware-derived data: keep them out of the repository.
"""

from __future__ import annotations

import argparse
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import oracle3  # noqa: E402

ENGINE = 0x8000_21B0                                        # engine copy, 0x754 bytes (938 words), then settings
SETTINGS = ENGINE + 0x754                                   # 0x80002904, 13 x 124 bytes
MACHINES = 0x8000_2F50                                      # machine id byte per track
STATE = 0x8000_E9B0                                         # smoothing state, a long per engine word
LOCKS = 0x4550_E1CC                                         # lock bits, 3 longs per track
FX_LOCKS = LOCKS + 12 * 12                                  # 0x4550E25C, the FX track's
SCRATCH = 0x8000_FC00                                       # a lock list or a sound the rig places in free SRAM
WORDS = 938

WORD_EDGES = (0, 1, 0xFF, 0x100, 0x3FFF, 0x4000, 0x7EFF, 0x7F00, 0x7F01, 0x7FFF, 0x8000, 0x8001, 0xFFFF)
LONG_EDGES = (0, 1, 0x7FFF_FFFF, 0x8000_0000, 0x8000_0001, 0xFFFF_FFFF, 0x4000_0000, 0xC000_0000, 0x7FFF_8000,
              0x7FFF_7FFF, 0x0000_8000, 0xFFFF_8000)


def word(rng: random.Random, edge: bool) -> int:
    """A parameter word: value << 8 | fine within 0..0x7F00 mostly; an edge, or any 16 bits."""
    if edge and rng.random() < 0.5:
        return rng.choice(WORD_EDGES)
    if rng.random() < 0.8:
        return min(rng.randrange(128) << 8 | rng.randrange(256), 0x7F00)
    return rng.randrange(1 << 16)


def long(rng: random.Random, edge: bool) -> int:
    if edge and rng.random() < 0.5:
        return rng.choice(LONG_EDGES)
    return rng.randrange(1 << 32)


def upper(rng: random.Random, low: int, bits: int) -> int:
    """A stack argument whose low BITS bits are LOW and the rest random half the time: the routine reads the low part."""
    return low | (rng.randrange(1 << (32 - bits)) << bits if rng.random() < 0.5 else 0)


def sparse(rng: random.Random, edge: bool) -> int:
    """Lock bits: none, a few, or any."""
    r = rng.random()
    if r < 0.2:
        return 0
    if r < 0.7:
        v = 0
        for _ in range(rng.randrange(1, 5)):
            v |= 1 << rng.randrange(32)
        return v
    return 0xFFFF_FFFF if edge and rng.random() < 0.5 else rng.randrange(1 << 32)


def be16(values) -> bytes:
    return b"".join(struct.pack(">H", v & 0xFFFF) for v in values)


def be32(values) -> bytes:
    return b"".join(struct.pack(">I", v & 0xFFFF_FFFF) for v in values)


# -- one generator per routine: (args, macsr, memory)

def gen_gain(rng, edge):
    vel = rng.choice((0, 1, 0x7F, 0x80, 0xFF)) if edge and rng.random() < 0.5 else (
        rng.randrange(128) if rng.random() < 0.8 else rng.randrange(256))
    # The velocity is the high byte of the track's velocity word (0x80004670), which the mix passes sign-extended
    vel = vel << 8 | rng.randrange(256)
    args = [upper(rng, word(rng, edge), 16), upper(rng, vel, 16), upper(rng, word(rng, edge), 16), rng.randrange(1 << 32)]
    return args, rng.choice((0x20, 0xA0)), {}


def gen_pan(rng, edge):
    if edge and rng.random() < 0.6:
        v = rng.choice((0, 0xFF, 0x100, 0x101, 0x4000, 0x7E00, 0x7EFF, 0x7F00, 0x7F01, 0x7FFF, 0x8000, 0xFFFF,
                        0xFFFF_FFFF, 0x8000_0000, 0x7FFF_FFFF, 0x8000_00FF, 0x8000_0100))
    elif rng.random() < 0.8:
        v = word(rng, False)
    else:
        v = rng.randrange(1 << 32)
    return [v], 0x20, {}


def gen_sine(rng, edge):
    return [long(rng, edge)], 0x20, {}


def gen_smooth(rng, edge):
    targets = [word(rng, edge) for _ in range(WORDS)]
    states = []
    for t in targets:
        r = rng.random()
        if r < 0.6:                                         # near its target, as after some blocks
            s = (t << 16 if t < 0x8000 else (t - 0x10000) << 16) + rng.randrange(-(1 << 22), 1 << 22)
        elif r < 0.8 or not edge:
            s = rng.randrange(1 << 32)
        else:
            s = rng.choice(LONG_EDGES)
        states.append(s)
    machines = bytes(rng.choice((0x2C, 0x27, 0x2C, 0x27, rng.randrange(0x40), rng.randrange(256)))
                     for _ in range(13))
    mem = {
        ENGINE: be16(targets),
        SETTINGS: bytes(rng.randrange(256) for _ in range(0x64C)),
        MACHINES: machines,
        STATE: be32(states),
        0x8000_6344: be32([rng.randrange(1 << 32)]),
    }
    return [ENGINE], 0x20, mem


def lock_id(rng, edge):
    """An internal id: 0..70 in a track, up to 95 within the three lock words, a few negative ones (-14..-1 keep
    every address in SRAM on track 0)."""
    r = rng.random()
    if edge and r < 0.15:
        return rng.randrange(-14, 0) & 0xFFFF
    if r < 0.8:
        return rng.randrange(71)
    return rng.randrange(96)


def gen_lock(rng, edge):
    t = rng.randrange(13)
    count = rng.choice((0, 1, 2, rng.randrange(13), rng.randrange(40)))
    if edge and rng.random() < 0.2:
        count = rng.choice((0xFFFF_FFFF, 0x8000_0000, 0))
    n = count if count < 0x8000_0000 else 0
    ids = [lock_id(rng, edge and t == 0) for _ in range(n)]
    list_ = bytearray(0x14 + 8 * n)
    struct.pack_into(">I", list_, 8, count)
    for i, ident in enumerate(ids):
        struct.pack_into(">HH", list_, 0x14 + 8 * i, ident, word(rng, edge))
        list_[0x14 + 8 * i + 4:0x14 + 8 * i + 8] = bytes(rng.randrange(256) for _ in range(4))
    mem = {SCRATCH: bytes(list_), LOCKS + 12 * t: be32([sparse(rng, edge) for _ in range(3)])}
    return [t, SCRATCH, ENGINE], 0x20, mem


def gen_restore_fx(rng, edge):
    mem = {FX_LOCKS: be32([sparse(rng, edge) for _ in range(3)]), SCRATCH: be16([word(rng, edge) for _ in range(96)])}
    return [ENGINE, SCRATCH], 0x20, mem


def gen_set(rng, edge):
    idx = rng.choice((0, WORDS - 1, 1000)) if edge and rng.random() < 0.3 else rng.randrange(WORDS)
    return [idx, long(rng, edge), ENGINE], 0x20, {}


def gen_unlock(rng, edge):
    t = rng.randrange(13)
    return [t], 0x20, {LOCKS + 12 * t: be32([sparse(rng, edge) for _ in range(3)])}


def gen_snap_track(rng, edge):
    return [rng.randrange(13), SCRATCH], 0x20, {SCRATCH: be16([word(rng, edge) for _ in range(71)])}


def gen_snap_range(rng, edge):
    first = rng.randrange(71)
    count = rng.choice((0, 1, 71 - first, rng.randrange(72 - first)))
    return [rng.randrange(13), SCRATCH, first, count], 0x20, {SCRATCH: be16([word(rng, edge) for _ in range(count)])}


def gen_restore(rng, edge):
    t = rng.randrange(13)
    mem = {LOCKS + 12 * t: be32([sparse(rng, edge) for _ in range(3)]),
           SCRATCH: be16([word(rng, edge) for _ in range(10 + 96)])}
    return [t, ENGINE, SCRATCH], 0x20, mem


def gen_snap_one(rng, edge):
    idx = rng.choice((0, 70, 95)) if edge and rng.random() < 0.3 else rng.randrange(71)
    return [rng.randrange(13), long(rng, edge), idx], 0x20, {}


ROUTINES = {
    0x4008ED48: ("gain", gen_gain),
    0x400A171A: ("pan", gen_pan),
    0x400A16BC: ("sine", gen_sine),
    0x40097952: ("smooth", gen_smooth),
    0x400978B4: ("lock", gen_lock),
    0x40097802: ("restoreFx", gen_restore_fx),
    0x40097894: ("set", gen_set),
    0x40097874: ("unlock", gen_unlock),
    0x400976B4: ("snapTrack", gen_snap_track),
    0x400976EE: ("snapRange", gen_snap_range),
    0x40097760: ("restore", gen_restore),
    0x40097732: ("snapOne", gen_snap_one),
}


def write_cases(path: Path, cases) -> None:
    with open(path, "wb") as f:
        f.write(b"ST1V" + struct.pack("<I", len(cases)))
        for routine, macsr, args, res in cases:
            f.write(struct.pack(f"<III{len(args)}III", routine, macsr, len(args), *args, res.d0, res.d1))
            for d in (res.reads, res.writes):
                rs = oracle3.runs(d)
                f.write(struct.pack("<I", len(rs)))
                for a, data in rs:
                    f.write(struct.pack("<II", a, len(data)) + data)


def coverage(blob: bytes, edges: dict[int, set[int]]) -> list[str]:
    import coverage_insn
    from capstone import CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_040, Cs
    cs = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_040)
    out, summary = [], []
    for root, cover in edges.items():
        executed = {e & 0xFFFF_FFFF for e in cover}
        ti = te = tb = tbs = 0
        lines = []
        for r in sorted(oracle3.reach(blob, [root])):
            insns = coverage_insn.routine_insns(blob, r, cs)
            hit = sum(1 for a, *_ in insns if a in executed)
            nb = nbs = 0
            gaps = []
            for a, text, target, nxt in insns:
                if nxt is None:
                    continue
                nb += 1
                taken = target is not None and (a << 32 | target) in cover
                fall = (a << 32 | nxt) in cover
                nbs += taken + fall
                if not (taken and fall):
                    missing = "both" if not (taken or fall) else ("taken" if not taken else "fall")
                    gaps.append(f"      {a:08x}  {text:40} missing {missing}")
            unexec = [a for a, *_ in insns if a not in executed]
            ti, te, tb, tbs = ti + len(insns), te + hit, tb + nb, tbs + nbs
            lines.append(f"  {r:08x}  insns {hit:4}/{len(insns):<4} branch sides {nbs:3}/{2 * nb:<3}"
                         + (f"  unexecuted: {coverage_insn.ranges(unexec, insns)}" if unexec else ""))
            lines.extend(gaps)
        name = ROUTINES[root][0]
        summary.append(f"{root:08x} {name:10} insns {te:4}/{ti:<4} branch sides {tbs:3}/{2 * tb:<3}")
        out.append(f"== {root:08x} {name}")
        out.extend(lines)
    return summary + [""] + out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=Path)
    ap.add_argument("--routines", default=",".join(f"{r:08x}" for r in ROUTINES))
    ap.add_argument("--cases", type=int, default=400)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--coverage", type=Path)
    a = ap.parse_args()
    routines = [int(r, 16) for r in a.routines.split(",")]
    rig = oracle3.Rig(list(ROUTINES))
    cases, edges = [], {}
    for routine in routines:
        name, gen = ROUTINES[routine]
        rng = random.Random(a.seed * 1000003 + routine)
        cover = edges.setdefault(routine, set())
        for i in range(a.cases):
            args, macsr, mem = gen(rng, i % 4 == 0)
            res = rig.call(routine, args, macsr, mem, edges=a.coverage is not None)
            cover |= res.edges
            cases.append((routine, macsr, args, res))
        print(f"{routine:08x} {name:10} {a.cases} cases")
    write_cases(a.out, cases)
    print(f"{len(cases)} cases, {a.out.stat().st_size} B -> {a.out}")
    if a.coverage:
        text = coverage(rig.blob, edges)
        a.coverage.write_text("\n".join(text) + "\n", encoding="utf-8")
        print("\n".join(text[:len(edges)]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
