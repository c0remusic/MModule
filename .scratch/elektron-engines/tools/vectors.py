"""Record test vectors for the C++ port: each call of chosen routines, with SRAM before and after.

    python vectors.py OUT.stv ROUTINE[,ROUTINE...] [--engines 0,1,...] [--blocks N] [--seed S] [--samples FILE]

Runs the original voice CPU (SyntHack's harness) on scenarios with random machine bytes, notes,
trigs and gate-offs, and records every call of the routines: their stack arguments and the 64 KB
of SRAM (voice states, scratch buffers, RAM tables) at entry and at return. The C++ test loads
SRAM before, runs the port, and compares with SRAM after. --samples writes a sample set
(twinshot_samples.py's format: the directory at 0x4404f754, the data at their base) into DDR
after the boot, as CPU #1 would load them; the port must be given the same file.

File format, little-endian: "STV3", u32 count; per case: u32 routine, u32 nargs, nargs x u32,
u32 d0 at return, 2 x u32 generator state at 0x4404f954 before, 2 x u32 after, 65536 B before,
65536 B after. The vectors hold firmware-derived data: keep them out of the
repository.
"""

from __future__ import annotations

import argparse
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, r"C:\dev\Synthack-OS\tools\emu")
import cli  # noqa: E402
import engine as eng  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_D0  # noqa: E402

SRAM, SRAM_SIZE = 0x8000_0000, 0x1_0000
RNG = 0x4404_F954
SAMPLE_DIRECTORY = 0x4404_F754


def load_samples(uc, path: Path) -> None:
    """Writes a twinshot_samples.py file's directory and data where the firmware reads them."""
    blob = path.read_bytes()
    if blob[:4] != b"STS1":
        raise ValueError(f"{path} is not a sample set")
    base, size = struct.unpack_from(">II", blob, 516)
    uc.mem_write(SAMPLE_DIRECTORY, blob[4:516])
    uc.mem_write(base, blob[524:524 + size])


def rng_state(uc) -> tuple[int, int]:
    return struct.unpack(">II", uc.mem_read(RNG, 8))


def write_cases(path, cases) -> None:
    with open(path, "wb") as f:
        f.write(b"STV3" + struct.pack("<I", len(cases)))
        for routine, args, d0, rng0, rng1, before, after in cases:
            f.write(struct.pack(f"<II{len(args)}II4I", routine, len(args), *args, d0, *rng0, *rng1))
            f.write(before)
            f.write(after)
NARGS = 6                                                   # 0x400019d6 takes six; extra words are harmless
TRIG_BYTE, GATEOFF_BYTE = 0x180, 0x181


def track_settings(rng: random.Random, params: bytearray) -> None:
    """Track 1's settings after the voice, as CPU #1 sends them: filter, BASE/WIDTH, amp envelope, drive, cutoff."""
    for k in (18, 19):                                      # drive word, P+0x50
        params[k] = rng.randrange(256) if rng.random() < 0.7 else 0
    params[0xA0] = rng.randrange(8)                         # filter TYPE
    params[0xA1] = rng.randrange(256)
    params[0xA2] = rng.randrange(128)                       # RESO
    params[0xA3] = rng.randrange(256)
    for k in range(0xA4, 0xA8):                             # BASE, WIDTH
        params[k] = rng.randrange(256) if k % 2 else rng.randrange(128)
    for k in range(0xE0, 0xEE):                             # ATK HOLD DEC SUS REL MODE RST, high bytes then fine
        params[k] = rng.randrange(128) if k % 2 == 0 else rng.randrange(256)
    params[0xEA] = rng.randrange(2)                         # MODE AHD / ADSR
    params[0xEC] = rng.randrange(2)                         # RST
    struct.pack_into(">i", params, 0x188, rng.randrange(0, 1 << 31))   # modulated cutoff, Q31
    params[0x1A8] = 1 if rng.random() < 0.05 else 0         # kill


def scenario(rng: random.Random, machine: int, blocks: int, track: bool = False):
    params = cli.make_params(machine, 0)
    for k in range(1, 20):
        params[k] = rng.randrange(256)
    if track:
        track_settings(rng, params)
    note = rng.randrange(24, 96)
    struct.pack_into(">i", params, 0x150, note << 16)
    trigs = {cli.TRIG_LOAD, cli.TRIG_NOTE} | {rng.randrange(4, blocks) for _ in range(3)}
    offs = {rng.randrange(4, blocks) for _ in range(2)}
    for b in range(blocks):
        q = bytearray(params)
        if b in trigs:
            q[TRIG_BYTE] = 0x01
        if b in offs:
            q[GATEOFF_BYTE] = 0x01
        if rng.random() < 0.15:                             # a knob moves between hits
            q[rng.randrange(2, 20)] = rng.randrange(256)
            params[:20] = q[:20]
        if track and rng.random() < 0.1:                    # the track's settings move too
            track_settings(rng, params)
            q[0xA0:0x1B0] = params[0xA0:0x1B0]
            q[18:20] = params[18:20]
        yield bytes(q)


def record(routines: set[int], engines: list[int], blocks: int, seed: int, limit: int, prob: float,
           track: bool = False, samples: Path | None = None):
    rng = random.Random(seed)
    m2e = cli.machine_to_engine()
    cases = []
    for engine in engines:
        machine = m2e.index(engine)
        e = eng.Engine()
        e.boot()
        uc = e.uc
        if samples:
            load_samples(uc, samples)
        pending = {}

        def on_entry(uc, addr, size, _ud):
            if rng.random() > prob or sum(1 for c in cases if c[0] == addr) >= limit:
                return
            sp = uc.reg_read(UC_M68K_REG_A7)
            ret = int.from_bytes(uc.mem_read(sp, 4), "big")
            args = [int.from_bytes(uc.mem_read(sp + 4 + 4 * i, 4), "big") for i in range(NARGS)]
            before = bytes(uc.mem_read(SRAM, SRAM_SIZE))
            rng0 = rng_state(uc)

            def on_return(uc, a, s, _u, key=(addr, sp)):
                if uc.reg_read(UC_M68K_REG_A7) != key[1] + 4:
                    return                                  # a deeper frame returning to the same address
                routine, args_, rng0_, before_, h = pending.pop(key)
                cases.append((routine, args_, uc.reg_read(UC_M68K_REG_D0), rng0_, rng_state(uc), before_,
                              bytes(uc.mem_read(SRAM, SRAM_SIZE))))
                uc.hook_del(h)

            h = uc.hook_add(UC_HOOK_CODE, on_return, begin=ret, end=ret)
            pending[(addr, sp)] = (addr, args, rng0, before, h)

        for r in routines:
            uc.hook_add(UC_HOOK_CODE, on_entry, begin=r, end=r)
        for q in scenario(rng, machine, blocks, track):
            e.render_block(q)
        print(f"engine {engine}: {len(cases)} cases so far", flush=True)
    return cases


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=Path)
    ap.add_argument("routines", type=lambda s: [int(x, 16) for x in s.split(",")])
    ap.add_argument("--engines", type=lambda s: [int(x) for x in s.split(",")], default=[0])
    ap.add_argument("--blocks", type=int, default=60)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--limit", type=int, default=400, help="cases per routine")
    ap.add_argument("--track", action="store_true", help="vary track 1's settings after the voice too")
    ap.add_argument("--samples", type=Path, help="a sample set to load into DDR after the boot")
    ap.add_argument("--prob", type=float, default=1.0, help="chance that a call is recorded")
    a = ap.parse_args()
    cases = record(set(a.routines), a.engines, a.blocks, a.seed, a.limit, a.prob, a.track, a.samples)
    write_cases(a.out, cases)
    counts = {}
    for c in cases:
        counts[c[0]] = counts.get(c[0], 0) + 1
    print({f"{k:08x}": v for k, v in sorted(counts.items())})
    return 0


if __name__ == "__main__":
    sys.exit(main())
