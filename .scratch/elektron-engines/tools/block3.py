"""Record vectors of CPU #1's block for the voice CPU (section 3, OS 1.41): the audio interrupt from the end of its
event loop (0x400a49a6) to the 0x1b0-byte block complete (0x400a4db8), run on oracle3's rig.

    python block3.py cases OUT.stv [--cases N] [--seed S] [--coverage REPORT.txt]
    python block3.py sequence OUT.stv OUTPUTS.sto [--sequences N] [--blocks B] [--seed S] [--samples FILE]
                     [--coverage REPORT.txt]

The segment starts with five masks in registers (cpu1.md's seam): d3 trigs, d2 gate-offs, d5 LFO trigs, a5 filter
trigs, d4 filter gate-offs, and a6 a frame the segment writes before it reads. A case's arguments are the masks in
that order: trigs, gate-offs, LFO trigs, filter trigs, filter gate-offs. The eDMA registers it writes (0xfc04xxxx)
are mapped as RAM and left out of the write set: the plug-in moves the block itself.

`cases`: each case a fresh state drawn as sequence3.py's kits, with the masks, cuts, mutes, a kit reload, the legato
setting at edges. Format ST1V (vectors3.py).

`sequence`: B consecutive blocks on one memory, the masks and the memory between blocks from a random schedule of
trigs, gate-offs, notes, velocities, locks (0x400978b4, run as the event loop runs it, a case of its own), mutes,
cuts, reloads, knobs and MIDI sources. Format ST1S (sequence3.py). Each block the segment built is also played by
the original voice CPU (SyntHack's harness, section 7, booted once per sequence, the sample set loaded): OUTPUTS
holds the eight tracks' 32 samples of each block, "ST1O", u32 count, then per block 256 little-endian int32 (track
t's sample s at 32 t + s), in the order of the segment's cases in OUT. The C++ test feeds the port's own blocks to
the ported voice CPU and compares.

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
import sequence3 as s3  # noqa: E402
import vectors3 as v  # noqa: E402
from unicorn import m68k_const as m  # noqa: E402

START, END = 0x400A_49A6, 0x400A_4DB8
LOCK = 0x4009_78B4
FRAME = v.PITCH_FRAME
PERIPHERALS = (0xFC04_0000, 0x2_0000)
BLOCK = 0x8000_2000
MUTES, KILLS, RELOAD, RELOAD_LEVEL = 0x8000_3150, 0x43BD_E440, 0x43BD_C4C8, 0x43BD_C808
DMA, VOICE_OUT, CV_SOURCE, TEMPO = 0x43BD_C824, 0x8000_3D50, 0x444D_B72C, 0x402D_7010
DIGITAL = (0, 1, 2, 3, 4, 5, 6, 7, 36, 37, 38, 44)
CALLEES = [0x4011_A2FC, 0x4011_A1E4, 0x4011_A250, 0x4011_59A4, 0x4011_5F78]


def rig() -> oracle3.Rig:
    r = oracle3.Rig(list(v.ROUTINES) + [START])
    r.uc.mem_map(*PERIPHERALS)
    return r


def segment(r: oracle3.Rig, masks: list[int], mem: dict[int, bytes] | None, keep: bool, edges: bool):
    trigs, offs, lfo, ftrigs, foffs = masks
    regs = {m.UC_M68K_REG_D3: trigs, m.UC_M68K_REG_D2: offs, m.UC_M68K_REG_D5: lfo, m.UC_M68K_REG_A5: ftrigs,
            m.UC_M68K_REG_D4: foffs, m.UC_M68K_REG_A6: FRAME}
    res = r.call(START, [], 0x20, mem, until=END, regs=regs, keep=keep, edges=edges)
    res.writes = {a: b for a, b in res.writes.items() if not PERIPHERALS[0] <= a < PERIPHERALS[0] + PERIPHERALS[1]}
    return res


def block_bytes(r: oracle3.Rig) -> bytes:
    return bytes(r.uc.mem_read(BLOCK, 0x1B0))


# -- states

def mask(rng, edge, bits=13):
    x = rng.random()
    if edge and x < 0.2:
        return rng.choice((0, 0xFFFF_FFFF, 0x1FFF, 0xFF, 0x1000, 0x8000_0000, rng.randrange(1 << 32)))
    if x < 0.3:
        return 0
    return sum(1 << t for t in range(bits) if rng.random() < 0.3)


def word16(rng, edge, common):
    if edge and rng.random() < 0.3:
        return rng.choice((0, 0xFFFF, 0x8000, 0x7FFF, 0x00FF, 0xFF00, 0x1000, 0xF000))
    return rng.choice(common) if rng.random() < 0.6 else rng.randrange(1 << 16)


def machines(rng, edge) -> bytes:
    ids = [rng.choice(DIGITAL) for _ in range(8)] + [rng.randrange(8, 45) for _ in range(5)]
    if edge:
        ids = [i if rng.random() < 0.7 else rng.choice((37, 44, 0x25, 0x2C, 0x80, 0xFF, 0x7F)) for i in ids]
    return bytes(ids)


def legato(rng, settings: bytes, edge) -> bytes:
    out = bytearray(settings)
    for t in range(13):
        value = 3 if rng.random() < 0.4 else rng.choice((0, 1, 2, 4)) if not edge else rng.randrange(1 << 32)
        struct.pack_into(">I", out, 0x7C * t + 0x60, value)
    return bytes(out)


def common(rng, mem: dict[int, bytes], edge: bool) -> None:
    """The cuts, mutes, reload, eDMA flag and the rest of what the segment reads beyond sequence3's kit."""
    mem[v.SETTINGS] = legato(rng, mem[v.SETTINGS], edge)
    mem[v.MACHINES] = machines(rng, edge)
    mem[MUTES] = v.be16([word16(rng, edge, (0, 0, 0, 1 << rng.randrange(8)))])
    mem[v.GATES] = v.be16([word16(rng, edge, (0, 0xFFF, rng.randrange(1 << 13)))])
    mem[v.HELD] = v.be16([rng.randrange(1 << 16)])
    mem[KILLS] = v.be16([word16(rng, edge, (0, 0, 1 << 12, 1 << rng.randrange(16), rng.randrange(1 << 13)))])
    reload = rng.choice((0, 0, 0, 1, 2)) if not (edge and rng.random() < 0.3) else rng.randrange(1 << 32)
    mem[RELOAD] = v.be32([reload])
    mem[RELOAD_LEVEL] = v.be16([v.word(rng, edge) if rng.random() < 0.7 else rng.randrange(1 << 16)])
    mem[DMA] = v.be32([rng.choice((0, 1, 1, rng.randrange(1 << 32)))])
    mem[VOICE_OUT] = bytes(rng.randrange(256) for _ in range(0x500))
    mem[CV_SOURCE] = v.be32([v.long(rng, edge)])
    mem[TEMPO] = v.be32([rng.choice(v.TEMPOS) if edge else rng.choice((0x3840, rng.randrange(3600, 36001)))])


def case_state(rng, edge) -> dict[int, bytes]:
    mem = s3.initial(rng)
    common(rng, mem, edge)
    # The control routines' state as some blocks leave it: the LFOs, glides and filter envelopes
    for t in range(13):
        for j in range(2):
            mem[v.LFO_STATE + 0x50 * t - 0x28 * (1 - j) - 0x24] = v.lfo_fields(rng, edge)
    mem[v.FILTER_ENV] = v.filter_state(rng, edge)[v.FILTER_ENV]
    glides = []
    for _ in range(13):
        glides += [(-rng.randrange(1 << 31)) & 0xFFFF_FFFF, (rng.randrange(-48, 49) << 16) & 0xFFFF_FFFF]
    mem[v.GLIDE] = v.be32(glides)
    mem[BLOCK] = bytes(rng.randrange(256) for _ in range(0x1B0))
    return mem


def cases(a) -> int:
    r = rig()
    out, cover = [], set()
    rng = random.Random(a.seed * 1000003 + START)
    for i in range(a.cases):
        edge = i % 4 == 0
        mem = case_state(rng, edge)
        masks = [mask(rng, edge) for _ in range(5)]
        res = segment(r, masks, mem, keep=False, edges=a.coverage is not None)
        cover |= res.edges
        out.append((START, 0x20, masks, res))
    v.write_cases(a.out, out)
    print(f"{len(out)} cases, {a.out.stat().st_size} B -> {a.out}")
    if a.coverage:
        report(r.blob, cover, a.coverage)
    return 0


# -- a sequence

class Schedule(s3.Harness):
    """sequence3's harness (trigs, gate-offs, notes, velocities, held and gate words, knobs, MIDI sources), plus the
    masks of the filter and LFO trigs, locks, mutes, cuts and kit reloads."""

    def __init__(self, rng):
        super().__init__(rng)
        self.reloading = 0

    def step(self):
        rng = self.rng
        mem, trigs, offs = super().step()
        lfo = sum(1 << t for t in range(12) if trigs >> t & 1 and rng.random() < 0.5)
        ftrigs = sum(1 << t for t in range(12) if trigs >> t & 1 and rng.random() < 0.6)
        foffs = sum(1 << t for t in range(12) if rng.random() < 0.04) & ~ftrigs
        mem[KILLS] = v.be16([1 << rng.randrange(13) if rng.random() < 0.03 else 0])
        if rng.random() < 0.02:
            mem[MUTES] = v.be16([rng.randrange(1 << 8) if rng.random() < 0.5 else 0])
        if self.reloading:
            self.reloading -= 1
            if not self.reloading:
                mem[RELOAD] = v.be32([0])
        elif rng.random() < 0.01:
            mem[RELOAD] = v.be32([2])
            self.reloading = rng.randrange(2, 20)
        if rng.random() < 0.02:
            mem[CV_SOURCE] = v.be32([rng.randrange(1 << 32)])
        locks = []
        for t in range(12):
            if trigs >> t & 1 and rng.random() < 0.3:
                ids = [rng.choice((17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 28, 29, 30, 37, 40, 41, 42, 43, 44, 45,
                                   rng.randrange(71))) for _ in range(rng.randrange(1, 5))]
                lst = bytearray(0x14 + 8 * len(ids))
                struct.pack_into(">I", lst, 8, len(ids))
                for k, ident in enumerate(ids):
                    struct.pack_into(">HH", lst, 0x14 + 8 * k, ident, v.word(rng, False))
                locks.append((t, bytes(lst)))
        return mem, [trigs, offs, lfo, ftrigs, foffs], locks


def sequence_state(rng) -> dict[int, bytes]:
    mem = s3.initial(rng)
    common(rng, mem, False)
    mem[v.MACHINES] = bytes([rng.choice(DIGITAL) for _ in range(8)] + [rng.randrange(8, 45) for _ in range(5)])
    mem[MUTES] = v.be16([0])
    mem[KILLS] = v.be16([0])
    mem[RELOAD] = v.be32([0])
    mem[DMA] = v.be32([1])
    return mem


def sequence(a) -> int:
    sys.path.insert(0, r"C:\dev\Synthack-OS\tools\emu")
    import engine as eng
    import vectors as v7
    r = rig()
    out, outputs, cover = [], [], set()
    for n in range(a.sequences):
        rng = random.Random(a.seed * 7919 + n)
        mem = sequence_state(rng)
        ids = list(mem[v.MACHINES][:8])
        r.call(0x4011_8CF0, [0], 0x20, None)                # resets the rig's memory, recorded nowhere
        e = eng.Engine()
        e.boot()
        if a.samples:
            v7.load_samples(e.uc, a.samples)
        sched = Schedule(rng)
        for i in range(a.blocks):
            if i:
                mem, masks, locks = sched.step()
            else:
                masks, locks = [0] * 5, []
            for t, lst in locks:
                mem[v.SCRATCH + 0x40 * t] = lst
            for addr, data in mem.items():
                r.uc.mem_write(addr, data)
            writes = {addr + k: b for addr, data in mem.items() for k, b in enumerate(data)}
            out.append((s3.INPUTS, 0, [] if i else [1], oracle3.Result(0, 0, {}, writes, 0)))
            for t, _ in locks:
                res = r.call(LOCK, [t, v.SCRATCH + 0x40 * t, v.ENGINE], 0x20, None, keep=True)
                out.append((LOCK, 0x20, [t, v.SCRATCH + 0x40 * t, v.ENGINE], res))
            res = segment(r, masks, None, keep=True, edges=a.coverage is not None)
            cover |= res.edges
            out.append((START, 0x20, masks, res))
            voices = e.render_block(block_bytes(r))
            outputs.append(b"".join(struct.pack(f"<{len(x)}i", *x) for x in voices))
        print(f"sequence {n}: {a.blocks} blocks, machines {ids}")
    v.write_cases(a.out, out, b"ST1S")
    with open(a.outputs, "wb") as f:
        f.write(b"ST1O" + struct.pack("<I", len(outputs)) + b"".join(outputs))
    print(f"{len(out)} cases, {len(outputs)} blocks -> {a.out}, {a.outputs}")
    if a.coverage:
        report(r.blob, cover, a.coverage)
    return 0


# -- coverage

# Branch sides proved dead, by reason
DEAD_SIDES = {
    0x4011_5FAA: "fall: the block's memcpy copies 20 bytes (and the smoothing's 0x64c), multiples of 4",
    0x4011_59BE: "fall: the memset clears 0x500 bytes, a multiple of 16",
    0x4011_59C6: "both: in memset's tail",
    0x4011_59CA: "both: in memset's tail",
    0x4011_59D0: "both: in memset's tail",
    0x4011_5FB0: "both: in memcpy's byte tail",
}
DEAD_RANGES = {
    0x4011_59A4: "0x401159c0-0x401159d0: memset's tail, for a length not a multiple of 16",
    0x4011_5F78: "0x40115fac-0x40115fb0: memcpy's byte tail, for a length not a multiple of 4",
}


def linear(blob: bytes, start: int, end: int, cs) -> list[tuple[int, str, int | None, int | None]]:
    """The instructions of [START, END) decoded one after the other as cfdis decodes them (ColdFire's mvs, mvz,
    ff1, EMAC included), with each conditional branch's target and next address."""
    import cfdis
    import coverage_insn
    out, a = [], start
    while a < end:
        op = cfdis.u16(blob, a)
        target = nxt = None
        if cfdis.is_emac(op) and cfdis.coldfire(blob, a) is None:
            text, n = cfdis.emac(blob, a)
        elif cfdis.coldfire(blob, a):
            text, n = cfdis.coldfire(blob, a)
        else:
            ins = next(cs.disasm(blob[a - cfdis.LOAD:a - cfdis.LOAD + 10], a))
            text, n = f"{ins.mnemonic} {ins.op_str}", ins.size
            if coverage_insn.COND.match(ins.mnemonic):
                target, nxt = cfdis.target(ins.op_str.split(",")[-1]), a + n
        out.append((a, text, target, nxt))
        a += n
    return out


def report(blob: bytes, cover: set[int], path: Path) -> None:
    import coverage_insn
    from capstone import CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_040, Cs
    cs = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_040)
    executed = {e & 0xFFFF_FFFF for e in cover}
    lines = []
    parts = [(START, linear(blob, START, END, cs))] + [(c, coverage_insn.routine_insns(blob, c, cs)) for c in CALLEES]
    for root, insns in parts:
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
                why = f"  (dead {DEAD_SIDES[a]})" if a in DEAD_SIDES else ""
                gaps.append(f"      {a:08x}  {text:40} missing {missing}{why}")
        unexec = [a for a, *_ in insns if a not in executed]
        name = "segment 400a49a6-400a4db8" if root == START else f"{root:08x}"
        lines.append(f"  {name:26} insns {hit:4}/{len(insns):<4} branch sides {nbs:3}/{2 * nb:<3}"
                     + (f"  unexecuted: {coverage_insn.ranges(unexec, insns)}" if unexec else "")
                     + (f"  (dead {DEAD_RANGES[root]})" if unexec and root in DEAD_RANGES else ""))
        lines.extend(gaps)
    text = "\n".join(lines) + "\n"
    path.write_text(text, encoding="utf-8")
    print(text)


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="mode", required=True)
    c = sub.add_parser("cases")
    c.add_argument("out", type=Path)
    c.add_argument("--cases", type=int, default=400)
    c.add_argument("--seed", type=int, default=1)
    c.add_argument("--coverage", type=Path)
    q = sub.add_parser("sequence")
    q.add_argument("out", type=Path)
    q.add_argument("outputs", type=Path)
    q.add_argument("--sequences", type=int, default=2)
    q.add_argument("--blocks", type=int, default=300)
    q.add_argument("--seed", type=int, default=1)
    q.add_argument("--samples", type=Path)
    q.add_argument("--coverage", type=Path)
    a = ap.parse_args()
    return cases(a) if a.mode == "cases" else sequence(a)


if __name__ == "__main__":
    sys.exit(main())
