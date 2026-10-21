"""Fuzz vectors: the original routines called on random voice states and blocks.

    python fuzz.py OUT.stv [--cases N] [--seed S]

The recorded vectors (vectors.py) follow what the engines do in a few scenarios, which leaves cases out: clipped
samples, every envelope stage, trigs in every routine. Here each routine is called from a captured snapshot whose
fields it reads were set at random (stages, levels, coefficients, flags, a loud block), and the SRAM before and after
is written in vectors.py's format.
"""

from __future__ import annotations

import argparse
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from oracle import RETURN, STACK, Snapshot  # noqa: E402
from vectors import rng_state, write_cases  # noqa: E402
from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_D0, UC_M68K_REG_SR  # noqa: E402

SNAP = Path(r"C:\dev\elektron-work\oracle\sytone_b3_render.snap")
SRAM, SIZE = 0x80000000, 0x10000
S = 0x80000000 + 0x708 * 2                                  # a voice the snapshot leaves idle
OUT = 0x80003840 + 128 * 2
ROM_TABLES = [0x40021404, 0x40021604, 0x40021804, 0x40021A04, 0x40021C04, 0x40021E04]


def rnd32(rng: random.Random) -> int:
    pick = rng.random()
    if pick < 0.15:
        return rng.choice([0, 1, 0x7FFFFFFF, 0x80000000, 0x80000001, 0xFFFFFFFF, 0x40000000, 0xC0000000])
    if pick < 0.5:
        return rng.randrange(1 << 32)
    return rng.randrange(-(1 << 31), 1 << 31) & 0xFFFFFFFF >> rng.randrange(8)


def put(mem: bytearray, addr: int, v: int) -> None:
    struct.pack_into(">I", mem, addr - SRAM, v & 0xFFFFFFFF)


def coefficient(rng):                                       # a decay coefficient, negative, close to -1 or not
    return (-rng.randrange(1, 1 << 31)) & 0xFFFFFFFF if rng.random() < 0.7 else rnd32(rng)


def block(rng, mem):
    loud = rng.random() < 0.5
    for n in range(32):
        v = rng.randrange(-(1 << 31), 1 << 31) if loud else rng.randrange(-(1 << 28), 1 << 28)
        put(mem, OUT + 4 * n, v)


def flags(rng, mem):
    for off in (0x34, 0x38, 0x3C):
        put(mem, S + off, int(rng.random() < 0.3))


def env_case(rng, mem):
    flags(rng, mem)
    n = rng.randrange(1, 8)
    for k in range(n):
        u = S + 0x80 * k
        put(mem, u + 0x54, rng.choice([0x80000000, rnd32(rng), -rng.randrange(1, 1 << 28)]))
        put(mem, u + 0x58, coefficient(rng))
        put(mem, u + 0x5C, rnd32(rng))
        put(mem, u + 0x60, rng.choice([0, 0, rng.randrange(1, 8)]))
        put(mem, u + 0x7C, rng.choice([0x80000000, 0, rnd32(rng)]))
        put(mem, u + 0x80, rnd32(rng))
        put(mem, u + 0x84, rng.choice([0, 1, 2, 3, 4, rng.randrange(5, 9)]))
        put(mem, u + 0x88, rng.randrange(0, 9))
    return [S, n]


def amp_case(rng, mem):
    flags(rng, mem)
    put(mem, S + 0x3D0, rng.choice([0x80000000, 0, rnd32(rng)]))
    put(mem, S + 0x3D8, rng.randrange(2))
    put(mem, S + 0x3DC, rng.choice([0, 1, 2, rng.randrange(3, 6)]))
    put(mem, S + 0x3E0, rng.randrange(100))
    put(mem, S + 0x3E4, rng.randrange(2))
    put(mem, S + 0x3E8, rng.choice([0x80000000, rnd32(rng), -rng.randrange(1, 1 << 28)]))
    put(mem, S + 0x3EC, rng.choice([0, 0x1234, rnd32(rng)]))
    put(mem, S + 0x3F0, coefficient(rng))
    return [S]


def vca_case(rng, mem):
    flags(rng, mem)
    put(mem, S + 0x3D0, rng.choice([0x80000000, 0, rnd32(rng)]))
    put(mem, S + 0x3D4, rng.choice([0x80000000, 0, rnd32(rng)]))
    put(mem, S + 0x3E8, rng.choice([0x80000000, rnd32(rng)]))
    block(rng, mem)
    return [OUT, S]


def drive_case(rng, mem):
    flags(rng, mem)
    put(mem, S + 0x48, int(rng.random() < 0.7))
    for off in (0x414, 0x418, 0x420, 0x424):
        put(mem, S + off, rng.choice([0x012C0F26, 0x001F7C5C, rnd32(rng) >> 4]))
    for off in (0x41C, 0x428):
        put(mem, S + off, rng.choice([0x82581E4B, 0x803EF8B8, coefficient(rng)]))
    for off in (0x430, 0x434, 0x43C, 0x440, 0x444):
        put(mem, S + off, rng.choice([0x7FFFFFFF, 0x20000000, 0x60000000, rnd32(rng)]))
    put(mem, S + 0x438, rng.choice([1, 5, rng.randrange(0, 8)]))
    block(rng, mem)
    return [OUT, S]


def interp_case(rng, mem):
    # Words come from bytes: never above 0xffff, where the original would read far past its table
    w = rng.choice([rng.randrange(0x8000), rng.randrange(0x10000), -rng.randrange(1, 1 << 31) & 0xFFFFFFFF])
    return [rng.choice(ROM_TABLES), w]


def any_voice(rng, mem):
    """Every word of the voice random: init must overwrite what it owns and leave the rest."""
    for off in range(0, 0x708, 4):
        if off != 0x56C:                                    # the history pointer the boot sets
            put(mem, S + off, rnd32(rng))
    put(mem, S + 0x38, int(rng.random() < 0.5))
    return [S]


ROUTINES = {0x40003EE0: any_voice, 0x4000255E: any_voice, 0x40003C0E: env_case, 0x40003D42: amp_case, 0x400045A0: vca_case, 0x400047EA: drive_case,
            0x40004DC6: interp_case}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=Path)
    ap.add_argument("--cases", type=int, default=400, help="per routine")
    ap.add_argument("--seed", type=int, default=7)
    a = ap.parse_args()
    rng = random.Random(a.seed)
    snap = Snapshot.load(SNAP)
    e = snap.machine()
    uc = e.uc
    base = bytes(uc.mem_read(SRAM, SIZE))
    records = []
    for routine, make in ROUTINES.items():
        for _ in range(a.cases):
            mem = bytearray(base)
            args = make(rng, mem)
            uc.mem_write(SRAM, bytes(mem))
            uc.mem_write(0x4404F954, struct.pack(">II", rng.randrange(1 << 32), rng.randrange(1 << 32)))
            rng0 = rng_state(uc)
            uc.reg_write(UC_M68K_REG_SR, snap.sr)
            sp = STACK - 4 * (len(args) + 1)
            uc.mem_write(sp, struct.pack(f">{len(args) + 1}I", RETURN, *args))
            uc.reg_write(UC_M68K_REG_A7, sp)
            e.emac.state.acc = [0, 0, 0, 0]
            e.emac.state.macsr = 0xA0
            uc.emu_start(routine, RETURN, count=2_000_000)
            if e.emac.error:
                raise RuntimeError(e.emac.error)
            records.append((routine, args, uc.reg_read(UC_M68K_REG_D0), rng0, rng_state(uc), bytes(mem),
                            bytes(uc.mem_read(SRAM, SIZE))))
        print(f"{routine:08x}: {a.cases} cases", flush=True)
    write_cases(a.out, records)
    return 0


if __name__ == "__main__":
    sys.exit(main())
