"""Forced-state vectors: each routine with coverage gaps called alone, on states that force its unreached sides.

    python fuzz_states.py AREA OUT.stv [--cases N] [--attempts N] [--seed S] [--cover FILE]

AREA is shared, engines or stages. Base states are captured at the routines' entries while the original runs edge
scenarios (vectors.scenario, every engine, tracks 2-8 too): SRAM, arguments, generator, registers and EMAC state.
Each case takes a base of its routine, forces the voice fields, parameter bytes or buffers its gaps need
(coverage/unreachable.txt) with random values around them, and calls the routine alone on a booted machine with the
TWINSHOT sample set loaded. A call is bounded in instructions and skipped, and reported, when it faults, does not
return, raises an EMAC error, writes outside SRAM, its stack and the generator, or reads memory the port is not
given (anything but SRAM, section 7, the sample set and the generator). Cases that add coverage are kept
first, then random ones up to --cases per routine. --cover writes prev << 32 | addr of every instruction executed
in the kept cases (coverage_insn.py's format) for fuzz_report.py.
"""

from __future__ import annotations

import argparse
import copy
import random
import struct
import sys
from array import array
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import vectors  # noqa: E402  (puts SyntHack on the path)
import cli  # noqa: E402
import engine as eng  # noqa: E402
from fuzz import coefficient, rnd32  # noqa: E402
from oracle import REGS, RETURN, STACK  # noqa: E402
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UcError  # noqa: E402
from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_D0, UC_M68K_REG_PC, UC_M68K_REG_SR  # noqa: E402

SRAM, SIZE = vectors.SRAM, vectors.SRAM_SIZE
SAMPLES = Path(r"C:\dev\elektron-work\references\twinshot_samples.bin")
SECTION7 = Path(r"C:\dev\elektron-work\syntakt-1.41\section_7_BLOB.bin")
SECTION7_LOAD = 0x40000400
INT_MIN = 0x80000000
BUDGET = 3_000_000
NARGS = vectors.NARGS

SHARED = {0x40003C0E: "unitEnvelopes", 0x40003CDA: "sweepEnvelope", 0x40003D42: "ampEnvelope",
          0x40003DF2: "modulatorEnvelope", 0x400045A0: "vca", 0x40004674: "modulatorVca"}
ENGINES = {0x40008988: ("CP VINTAGE render", 7), 0x400091E0: ("SY BITS render", 9),
           0x40009A8A: ("SP TWINSHOT render", 11), 0x400097DC: ("SP TWINSHOT update", 11)}
STAGES = {0x40002320: "trackBaseWidth", 0x400019D6: "trackFilter", 0x4000168E: "trackAmp"}


# ---------------------------------------------------------------- memory helpers

def get(mem, addr):
    return struct.unpack_from(">I", mem, addr - SRAM)[0]


def put(mem, addr, v):
    struct.pack_into(">I", mem, addr - SRAM, v & 0xFFFFFFFF)


def putb(mem, addr, v):
    mem[addr - SRAM] = v & 0xFF


def putw(mem, addr, v):
    struct.pack_into(">H", mem, addr - SRAM, v & 0xFFFF)


def maybe(rng, p=0.6):
    return rng.random() < p


def flags(rng, mem, v):
    for off in (0x34, 0x38, 0x3C):
        if maybe(rng):
            put(mem, v + off, int(rng.random() < 0.4))


def block(rng, mem, addr):
    loud = rng.random() < 0.5
    for n in range(32):
        put(mem, addr + 4 * n, rng.randrange(-(1 << 31), 1 << 31) if loud else rng.randrange(-(1 << 28), 1 << 28))


def level(rng):
    return rng.choice([INT_MIN, 0, 0x7FFFFFFF, rnd32(rng), -rng.randrange(1, 1 << 28)])


# ---------------------------------------------------------------- shared routines: voice fields

def m_units(rng, mem, args):                               # 40003c0e (voice, units)
    v = args[0]
    flags(rng, mem, v)
    if maybe(rng, 0.2):
        args[1] = rng.randrange(1, 4)
    for k in range(args[1]):
        u = v + 0x80 * k
        if maybe(rng):
            put(mem, u + 0x84, rng.choice([0, 1, 2, 3, 3, 4, 4, 5, rnd32(rng)]))
        if maybe(rng):
            put(mem, u + 0x60, rng.choice([0, 1, 2, rng.randrange(1, 9), rnd32(rng) >> 20]))
        if maybe(rng):
            put(mem, u + 0x88, rng.choice([0, get(mem, u + 0x60), get(mem, u + 0x60) - 1, rng.randrange(16)]))
        if maybe(rng):
            put(mem, u + 0x54, rng.choice([INT_MIN, INT_MIN, rnd32(rng), rng.randrange(1, 1 << 28)]))
        if maybe(rng, 0.3):
            put(mem, u + 0x58, coefficient(rng))
        if maybe(rng, 0.3):
            put(mem, u + 0x5C, coefficient(rng))
        if maybe(rng):
            put(mem, u + 0x7C, level(rng))


def m_sweep(rng, mem, args):                               # 40003cda (voice)
    v = args[0]
    flags(rng, mem, v)
    if maybe(rng):
        put(mem, v + 0x40C, rng.choice([INT_MIN, rnd32(rng), rng.randrange(1, 1 << 28), 0x7FFFFFFF]))
    if maybe(rng):
        put(mem, v + 0x3FC, rng.choice([0, 0, 1, rnd32(rng)]))
    if maybe(rng):
        put(mem, v + 0x400, rng.choice([0x7FFFFFFF, rnd32(rng), 0]))
    if maybe(rng):
        put(mem, v + 0x408, rng.choice([0, 1]))
    if maybe(rng, 0.3):
        put(mem, v + 0x410, coefficient(rng))


def m_amp(rng, mem, args):                                 # 40003d42 (voice)
    v = args[0]
    flags(rng, mem, v)
    if maybe(rng):
        put(mem, v + 0x3E8, rng.choice([INT_MIN, INT_MIN, rnd32(rng), rng.randrange(1, 1 << 28)]))
    if maybe(rng):
        put(mem, v + 0x3D8, rng.choice([0, 0, 1, rnd32(rng)]))
    if maybe(rng):
        put(mem, v + 0x3DC, rng.choice([0, 1, 2, 2, 3, rnd32(rng)]))
    if maybe(rng):
        put(mem, v + 0x3D0, level(rng))
    if maybe(rng):
        put(mem, v + 0x3E4, rng.choice([0, 1]))
    if maybe(rng):
        put(mem, v + 0x3EC, rng.choice([0, 1, rnd32(rng)]))
    if maybe(rng, 0.3):
        put(mem, v + 0x3E0, rng.randrange(1000))
    if maybe(rng, 0.3):
        put(mem, v + 0x3F0, coefficient(rng))


def m_modenv(rng, mem, args):                              # 40003df2 (voice)
    v = args[0]
    flags(rng, mem, v)
    if maybe(rng):
        put(mem, v + 0x450, rng.choice([0, 1, 2, 3, 3, 4, 4, 5, rnd32(rng)]))
    if maybe(rng):
        put(mem, v + 0x460, rng.choice([0, 1, 2, rng.randrange(1, 9), rnd32(rng) >> 20]))
    if maybe(rng):
        put(mem, v + 0x454, rng.choice([0, get(mem, v + 0x460), get(mem, v + 0x460) - 1, rng.randrange(16)]))
    if maybe(rng):
        put(mem, v + 0x458, rng.choice([INT_MIN, INT_MIN, rnd32(rng), rng.randrange(1, 1 << 28)]))
    if maybe(rng):
        put(mem, v + 0x448, level(rng))
    if maybe(rng, 0.3):
        put(mem, v + 0x45C, coefficient(rng))
    if maybe(rng, 0.3):
        put(mem, v + 0x464, coefficient(rng))


def m_vca(rng, mem, args, lvl=0x3D0, prev=0x3D4, atk=0x3E8):    # 400045a0 (block, voice)
    v = args[1]
    flags(rng, mem, v)
    if maybe(rng):
        put(mem, v + atk, rng.choice([INT_MIN, INT_MIN, rnd32(rng)]))
    if maybe(rng):
        put(mem, v + lvl, level(rng))
    if maybe(rng):
        put(mem, v + prev, level(rng))
    if maybe(rng, 0.5):
        block(rng, mem, args[0])


def m_modvca(rng, mem, args):                              # 40004674 (block, voice)
    m_vca(rng, mem, args, 0x448, 0x44C, 0x458)


# ---------------------------------------------------------------- engines: through their update and render

def m_cp_render(rng, mem, args):                           # 40008988 (block, voice): the clap units' 0x5ac
    v = args[1]
    flags(rng, mem, v)
    for k in range(3):
        u = v + 0x34 * k
        if maybe(rng):
            put(mem, u + 0x5AC, rng.choice([0, 0, 1, rnd32(rng)]))
        if maybe(rng, 0.4):
            put(mem, u + 0x5BC, rng.choice([INT_MIN, rnd32(rng), rng.randrange(1, 1 << 28)]))


def m_bits_render(rng, mem, args):                         # 400091e0 (block, voice): 0x5a0's integer part >= 30
    v = args[1]
    if maybe(rng, 0.8):
        put(mem, v + 0x5A0, rng.choice([0x1E0000, 0x1EFFFF, 0x1F0000, 0x7FFF0000, rng.randrange(0x1E0000, 1 << 31),
                                        rng.randrange(0, 0x1E0000), -rng.randrange(1, 1 << 31)]))


DIRECTORY = [struct.unpack_from(">II", SAMPLES.read_bytes(), 4 + 8 * i) for i in range(64)]


def m_tw_render(rng, mem, args):                           # 40009a8a (block, voice): the players at 0x6b0, 0x6d4
    v = args[1]
    for p in (v + 0x6B0, v + 0x6D4):
        if not maybe(rng, 0.7):
            continue
        n, ptr = rng.choice([e for e in DIRECTORY if e[0] and e[1]])
        length = min(n, 0x7FFFF) << 12
        rate = rng.choice([0x1000, 0x800, 0x2000, rng.randrange(0x100, 0x8000)])
        kind = rng.randrange(5)
        put(mem, p + 0x14, 1)                               # playing
        put(mem, p + 0x00, ptr)
        put(mem, p + 0x0C, length)
        put(mem, p + 0x18, rate)
        put(mem, p + 0x1C, rng.choice([0, 0, 1]))          # reverse
        put(mem, p + 0x08, max(0, length - rng.randrange(0, 40 * rate)))
        put(mem, p + 0x20, rng.choice([0, 1, 1]))          # loop (AMP HOLD)
        put(mem, p + 0x04, ptr)
        put(mem, p + 0x10, length)
        if kind == 0:                                       # 79e fall: playing with no length
            put(mem, p + 0x0C, rng.choice([0, -rng.randrange(1, 1 << 20), INT_MIN]))
        elif kind == 1:                                     # 80e taken: looping with no loop length
            put(mem, p + 0x10, rng.choice([0, -rng.randrange(1, 1 << 20)]))
        elif kind == 2:                                     # 812 taken: looping with no loop start
            put(mem, p + 0x04, 0)
        elif kind == 3:                                     # a loop shorter than the sample
            put(mem, p + 0x10, rng.randrange(1, length + 1))


def m_tw_update(rng, mem, args):                           # 400097dc (note, voice, params): sample slots and TUNE
    v, q = args[1], args[2]
    flags(rng, mem, v)
    for off in (38, 46):                                    # SMPA / SMPB bytes: 0x47 and 0x38 pick entry 7, empty
        if maybe(rng):
            putb(mem, q + off, rng.choice([0x47, 0x38, 0x49, 0x36, rng.randrange(256)]))
    if maybe(rng, 0.4):
        putw(mem, q + 40, rng.choice([0x4000, 0x3FFF, 0x4001, 0, 0x7FFF, 0x8000, 0xFFFF, rng.randrange(1 << 16)]))


# ---------------------------------------------------------------- track stages

def m_basewidth(rng, mem, args):                           # 40002320 (block, params)
    q = args[1]
    for i in range(4):
        for c in (0, 1):
            if maybe(rng):
                putb(mem, 0x80008550 + 2 * i + c, rng.choice([0, 1, 1, rng.randrange(256)]))
        if maybe(rng):
            putw(mem, q + 0x11C * i + 0x82, rng.choice([0, 0, 1, rng.randrange(1 << 16)]))


def m_filter(rng, mem, args):                              # 400019d6 (params, block, cutoff, track, ...)
    t = args[3]
    if not 0 <= t < 8:
        return
    if maybe(rng):
        put(mem, 0x80008560 + 4 * t, rng.choice([8, 9, 15, 0xFFFFFFFF, 0x80000000, rng.randrange(8, 256),
                                                 rng.randrange(8), rnd32(rng)]))
    if maybe(rng):
        putb(mem, args[0], rng.choice([0x80, 0xFF, 8, 0x7F, rng.randrange(256), rng.randrange(8)]))


HOLD_127 = 0x75300                                          # table 0x40013c70[127]


def m_amp_stage(rng, mem, args):                           # 4000168e (block, params, trigs, gate-offs, kills)
    for t in range(8):
        if not maybe(rng, 0.5):
            continue
        a = args[1] + 0x6A + 0x8E * t
        s = 0x800049E0 + 0xC * t
        putb(mem, a + 4, rng.choice([127, 127, 126, rng.randrange(128)]))
        put(mem, s, rng.choice([0, 0, 0, 1, 2]))
        put(mem, s + 4, rng.choice([HOLD_127, HOLD_127 - 1, HOLD_127 + rng.randrange(1, 64),
                                    HOLD_127 - rng.randrange(1, 64), rng.randrange(1 << 20)]))


MUTATORS = {0x40003C0E: m_units, 0x40003CDA: m_sweep, 0x40003D42: m_amp, 0x40003DF2: m_modenv,
            0x400045A0: m_vca, 0x40004674: m_modvca,
            0x40008988: m_cp_render, 0x400091E0: m_bits_render, 0x40009A8A: m_tw_render, 0x400097DC: m_tw_update,
            0x40002320: m_basewidth, 0x400019D6: m_filter, 0x4000168E: m_amp_stage}
AREAS = {"shared": SHARED, "engines": ENGINES, "stages": STAGES}


# ---------------------------------------------------------------- capture and call

class Base:
    __slots__ = ("routine", "args", "sram", "rng", "regs", "sr", "emac")


def capture(routines: set[int], engines: list[int], blocks: int, per: int, seed: int) -> dict[int, list[Base]]:
    """Up to PER base states per routine and engine, taken at the routines' entries in edge scenarios."""
    rng = random.Random(seed)
    m2e = cli.machine_to_engine()
    bases: dict[int, list[Base]] = {r: [] for r in routines}
    for n in engines:
        e = eng.Engine()
        e.boot()
        vectors.load_samples(e.uc, SAMPLES)
        got = {r: 0 for r in routines}

        def hit(uc, addr, size, _ud):
            if got[addr] >= per or rng.random() > 0.3:
                return
            got[addr] += 1
            sp = uc.reg_read(UC_M68K_REG_A7)
            b = Base()
            b.routine = addr
            b.args = [int.from_bytes(uc.mem_read(sp + 4 + 4 * i, 4), "big") for i in range(NARGS)]
            b.sram = bytes(uc.mem_read(SRAM, SIZE))
            b.rng = vectors.rng_state(uc)
            b.regs = [uc.reg_read(r) for r in REGS]
            b.sr = uc.reg_read(UC_M68K_REG_SR)
            b.emac = copy.deepcopy(e.emac.state)
            bases[addr].append(b)

        for r in routines:
            e.uc.hook_add(UC_HOOK_CODE, hit, begin=r, end=r)
        for q in vectors.scenario(rng, m2e.index(n), blocks, track=True, edge=True, others=True):
            e.render_block(q)
            if all(v >= per for v in got.values()):
                break
        print(f"engine {n}: " + " ".join(f"{r:08x}:{c}" for r, c in got.items() if c), flush=True)
    return bases


class Caller:
    """A booted machine with the sample set, on which a routine runs alone from a base state."""

    def __init__(self):
        self.e = eng.Engine()
        self.e.boot()
        vectors.load_samples(self.e.uc, SAMPLES)
        uc = self.e.uc
        self.cover: set[int] | None = None
        self.last = 0
        self.stray: list[int] = []
        uc.hook_add(UC_HOOK_CODE, self._code, begin=vectors.CODE_START, end=vectors.CODE_END - 1)
        uc.hook_add(UC_HOOK_MEM_WRITE, self._write, begin=0, end=SRAM - 1)
        uc.hook_add(UC_HOOK_MEM_WRITE, self._write, begin=SRAM + SIZE, end=0xFFFFFFFF)
        # What the port knows besides SRAM: section 7, the sample set (directory and data), the generator. A read
        # anywhere else (address 0, by a null pointer a forced state leaves) gives a value the port cannot have.
        blob = SAMPLES.read_bytes()
        base, size = struct.unpack_from(">II", blob, 516)
        self.known = [(SECTION7_LOAD, SECTION7_LOAD + SECTION7.stat().st_size), (base, base + size),
                      (vectors.SAMPLE_DIRECTORY, vectors.SAMPLE_DIRECTORY + 512), (vectors.RNG, vectors.RNG + 8),
                      (STACK - 0x10000, STACK + 0x100)]
        self.foreign: list[int] = []
        uc.hook_add(UC_HOOK_MEM_READ, self._read, begin=0, end=SRAM - 1)
        uc.hook_add(UC_HOOK_MEM_READ, self._read, begin=SRAM + SIZE, end=0xFFFFFFFF)

    def _read(self, uc, access, addr, size, value, _ud):
        if self.cover is None or any(lo <= addr and addr + size <= hi for lo, hi in self.known):
            return
        self.foreign.append(addr)

    def _code(self, uc, addr, size, _ud):
        if self.cover is not None:
            self.cover.add(self.last << 32 | addr)
        self.last = addr

    def _write(self, uc, access, addr, size, value, _ud):
        if STACK - 0x10000 <= addr < STACK + 0x100 or vectors.RNG <= addr < vectors.RNG + 8:
            return
        self.stray.append(addr)

    def call(self, b: Base, mem: bytes, args: list[int]):
        """(d0, rng after, SRAM after, cover) or a string saying why the call was skipped."""
        uc, e = self.e.uc, self.e
        uc.mem_write(SRAM, mem)
        uc.mem_write(vectors.RNG, struct.pack(">II", *b.rng))
        uc.reg_write(UC_M68K_REG_SR, b.sr)
        for r, v in zip(REGS, b.regs):
            uc.reg_write(r, v)
        sp = STACK - 4 * (len(args) + 1)
        uc.mem_write(sp, struct.pack(f">{len(args) + 1}I", RETURN, *args))
        uc.reg_write(UC_M68K_REG_A7, sp)
        e.emac.state = copy.deepcopy(b.emac)
        e.emac.error = None
        self.cover, self.last, self.stray, self.foreign = set(), 0, [], []
        try:
            uc.emu_start(b.routine, RETURN, count=BUDGET)
        except UcError as err:
            return f"fault {err} at {uc.reg_read(UC_M68K_REG_PC):08x}"
        finally:
            cover, self.cover = self.cover, None
        pc = uc.reg_read(UC_M68K_REG_PC)
        if pc != RETURN:
            return f"no return within {BUDGET} instructions (pc {pc:08x})"
        if e.emac.error:
            return f"EMAC {e.emac.error}"
        if self.stray:
            return f"writes outside SRAM at {self.stray[0]:08x} ({len(self.stray)})"
        if self.foreign:
            return f"reads outside the port's memory at {self.foreign[0]:08x} ({len(self.foreign)})"
        return uc.reg_read(UC_M68K_REG_D0), vectors.rng_state(uc), bytes(uc.mem_read(SRAM, SIZE)), cover


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("area", choices=AREAS)
    ap.add_argument("out", type=Path)
    ap.add_argument("--cases", type=int, default=120, help="kept per routine")
    ap.add_argument("--attempts", type=int, default=600, help="forced calls per routine")
    ap.add_argument("--seed", type=int, default=41)
    ap.add_argument("--blocks", type=int, default=120)
    ap.add_argument("--per", type=int, default=6, help="base states per routine and engine")
    ap.add_argument("--cover", type=Path)
    ap.add_argument("--skips", type=Path, help="where the skipped states are listed")
    a = ap.parse_args()
    targets = AREAS[a.area]
    if a.area == "engines":
        engines = sorted({n for _, n in targets.values()})
    elif a.area == "stages":
        engines = [0, 4]
    else:
        engines = list(range(12))
    rng = random.Random(a.seed)
    bases = capture(set(targets), engines, a.blocks, a.per, a.seed)
    caller = Caller()
    records, union, skips = [], set(), []
    for routine in targets:
        pool = bases[routine]
        if not pool:
            print(f"{routine:08x}: no base state", flush=True)
            continue
        kept, extra, seen = [], [], set()
        for i in range(a.attempts):
            b = rng.choice(pool)
            mem = bytearray(b.sram)
            args = list(b.args)
            MUTATORS[routine](rng, mem, args)
            res = caller.call(b, bytes(mem), args)
            if isinstance(res, str):
                skips.append(f"{routine:08x} attempt {i} args {' '.join(f'{x:08x}' for x in args[:4])}: {res}")
                caller = Caller()                           # the machine may be left inconsistent
                continue
            d0, rng1, after, cover = res
            rec = (routine, args, d0, b.rng, rng1, bytes(mem), after)
            if cover - seen:
                seen |= cover
                kept.append((rec, cover))
            else:
                extra.append((rec, cover))
        rng.shuffle(extra)
        chosen = (kept + extra)[:max(a.cases, len(kept))]
        for rec, cover in chosen:
            records.append(rec)
            union |= cover
        print(f"{routine:08x}: {len(chosen)} kept ({len(kept)} adding coverage), "
              f"{sum(s.startswith(f'{routine:08x}') for s in skips)} skipped", flush=True)
    vectors.write_cases(a.out, records)
    if a.cover:
        a.cover.write_bytes(array("Q", sorted(union)).tobytes())
    if a.skips:
        a.skips.write_text("\n".join(skips) + "\n", encoding="utf-8")
    print(f"{a.out}: {len(records)} cases, {len(skips)} skipped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
