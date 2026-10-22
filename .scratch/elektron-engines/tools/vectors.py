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


EDGES = (0, 1, 0x3F, 0x40, 0x7E, 0x7F, 0x80, 0x81, 0xFE, 0xFF)


def draw(rng: random.Random, n: int, edge: bool) -> int:
    """A value below N; with EDGE, three times in ten one of the edge values below N instead, or N - 1."""
    if edge and rng.random() < 0.3:
        return rng.choice([e for e in EDGES if e < n] + [n - 1])
    return rng.randrange(n)


WORD_EDGES = ((0, 0), (0, 1), (0x40, 0), (0x7F, 0xFF), (0x80, 0), (0xFF, 0xFF))


def edge_words(rng: random.Random, params: bytearray, chance: float) -> None:
    """Machine bytes 2-19 are words, high byte first: with CHANCE each, a whole word at an edge, or a coarse value
    with no fine part (an engine may compare a word with a whole step, 0x4000 the centre)."""
    for k in range(2, 20, 2):
        if rng.random() < chance:
            if rng.random() < 0.3:
                params[k:k + 2] = bytes((0, 0))
            elif rng.random() < 0.5:
                params[k:k + 2] = bytes(rng.choice(WORD_EDGES))
            else:
                params[k:k + 2] = bytes((draw(rng, 256, True), 0))


def edge_note(rng: random.Random, params: bytearray) -> None:
    """Track 1's pitch word: a note over 0-127, edges included, half the time with a fine part."""
    note = draw(rng, 128, True)
    struct.pack_into(">i", params, 0x150, note << 16 | (rng.randrange(1 << 16) if rng.random() < 0.5 else 0))


CLOCK = 0x184                                               # a long the ISR copies to 0x8000a07c: CP VINTAGE's rate


def edge_clock(rng: random.Random, params: bytearray) -> None:
    """The long at 0x184 that CP VINTAGE's clap timer adds each block (0 in make_params): 0 or up to 31 bits."""
    struct.pack_into(">I", params, CLOCK, rng.choice((0, 1, rng.randrange(1 << 8), rng.randrange(1 << 16),
                                                      rng.randrange(1 << 24), rng.randrange(1 << 31))))


def track_settings(rng: random.Random, params: bytearray, edge: bool = False, t: int = 0) -> None:
    """Track t + 1's settings after the voice, as CPU #1 sends them: filter, BASE/WIDTH, amp envelope, drive, cutoff."""
    m, f, a = 20 * t, 0xA0 + 8 * t, 0xE0 + 14 * t
    for k in (m + 18, m + 19):                              # drive word, P+0x50
        params[k] = draw(rng, 256, edge) if rng.random() < 0.7 else 0
    params[f] = rng.randrange(8)                            # filter TYPE
    params[f + 1] = draw(rng, 256, edge)
    params[f + 2] = draw(rng, 128, edge)                    # RESO
    params[f + 3] = draw(rng, 256, edge)
    for k in range(f + 4, f + 8):                           # BASE, WIDTH
        params[k] = draw(rng, 256, edge) if k % 2 else draw(rng, 128, edge)
    for k in range(a, a + 14):                              # ATK HOLD DEC SUS REL MODE RST, high bytes then fine
        params[k] = draw(rng, 128, edge) if k % 2 == 0 else draw(rng, 256, edge)
    params[a + 10] = rng.randrange(2)                       # MODE AHD / ADSR
    params[a + 12] = rng.randrange(2)                       # RST
    cutoff = rng.randrange(0, 1 << 31)
    if edge and rng.random() < 0.2:
        cutoff = rng.choice((0, 1, 1 << 30, (1 << 31) - 1))
    struct.pack_into(">i", params, 0x188 + 4 * t, cutoff)   # modulated cutoff, Q31
    kill = rng.random() < 0.05                              # kill, a bit per track
    params[0x1A8] = params[0x1A8] & ~(1 << t) | kill << t if t else int(kill)


def parse_bias(text: str) -> dict[int, list[int]]:
    """'4=47,38;12=4a' (hexadecimal): machine byte 4 drawn as 0x47 or 0x38 half the time, byte 12 as 0x4a."""
    bias = {}
    for item in filter(None, text.split(";")):
        k, values = item.split("=")
        bias[int(k)] = [int(v, 16) for v in values.split(",")]
    return bias


def scenario(rng: random.Random, machine: int, blocks: int, track: bool = False, edge: bool = False,
             bias: dict[int, list[int]] | None = None, others: bool = False):
    """With EDGE: edge values mixed into the bytes drawn, notes over 0-127 with a fine part, and trigs and gate-offs
    on any block besides the drawn ones (back to back, together, before the first note). BIAS: machine bytes drawn
    half the time from their own values. OTHERS: tracks 2-8 (their machine byte left at 0x40) get their own track
    settings, trigs and gate-offs, for the track stages, which run on every track."""
    bias = bias or {}

    def byte(k: int) -> int:
        if k in bias and rng.random() < 0.5:
            return rng.choice(bias[k])
        return draw(rng, 256, edge)

    params = cli.make_params(machine, 0)
    for k in range(1, 20):
        params[k] = byte(k)
    if edge:
        edge_words(rng, params, 0.4)
    if track:
        track_settings(rng, params, edge)
    if others:
        for t in range(1, 8):
            track_settings(rng, params, edge, t)
    if edge:
        edge_note(rng, params)
        edge_clock(rng, params)
    else:
        note = rng.randrange(24, 96)
        struct.pack_into(">i", params, 0x150, note << 16)
    trigs = {cli.TRIG_LOAD, cli.TRIG_NOTE} | {rng.randrange(4, blocks) for _ in range(3)}
    offs = {rng.randrange(4, blocks) for _ in range(2)}
    for b in range(blocks):
        q = bytearray(params)
        if b in trigs or edge and rng.random() < 0.06:
            q[TRIG_BYTE] = 0x01
            if edge and rng.random() < 0.5:                 # a new note with the hit
                edge_note(rng, params)
                q[0x150:0x154] = params[0x150:0x154]
        elif edge and rng.random() < 0.03:                  # the pitch moves between hits
            edge_note(rng, params)
            q[0x150:0x154] = params[0x150:0x154]
        if b in offs or edge and rng.random() < 0.06:
            q[GATEOFF_BYTE] = 0x01
        if edge and rng.random() < 0.02:
            edge_clock(rng, params)
            q[CLOCK:CLOCK + 4] = params[CLOCK:CLOCK + 4]
        if rng.random() < 0.15:                             # a knob moves between hits
            if bias:
                k = rng.randrange(2, 20)
                q[k] = byte(k)
            else:
                q[rng.randrange(2, 20)] = draw(rng, 256, edge)     # the value is drawn before the byte
            if edge:
                edge_words(rng, q, 0.1)
            params[:20] = q[:20]
        if edge and rng.random() < 0.15:                    # a knob turns: a byte steps from where it is
            k = rng.randrange(2, 20)
            q[k] = (q[k] + rng.choice((-4, -1, 1, 4))) & 0xFF
            params[:20] = q[:20]
        if track and rng.random() < 0.1:                   # the track's settings move too
            track_settings(rng, params, edge)
            q[0xA0:0x1B0] = params[0xA0:0x1B0]
            q[18:20] = params[18:20]
        if others:
            for t in range(1, 8):
                if rng.random() < 0.05:
                    track_settings(rng, params, edge, t)
                    q[0xA0:0x180] = params[0xA0:0x180]
                    q[0x188:0x1B0] = params[0x188:0x1B0]
                    q[20 * t + 18:20 * t + 20] = params[20 * t + 18:20 * t + 20]
                q[TRIG_BYTE] |= (rng.random() < 0.05) << t
                q[GATEOFF_BYTE] |= (rng.random() < 0.05) << t
        yield bytes(q)


CODE_START, CODE_END = 0x4000_0400, 0x4001_4000            # section 7's code, before its tables


def record(routines: set[int], engines: list[int], blocks: int, seed: int, limit: int, prob: float,
           track: bool = False, samples: Path | None = None, cover: set | None = None, edge: bool = False,
           bias: dict[int, list[int]] | None = None, others: bool = False):
    """With COVER, also adds prev << 32 | addr for each instruction executed while a recorded call runs."""
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
        if cover is not None:
            last = [0]

            def on_code(uc, addr, size, _ud):
                if pending:
                    cover.add(last[0] << 32 | addr)
                last[0] = addr

            uc.hook_add(UC_HOOK_CODE, on_code, begin=CODE_START, end=CODE_END - 1)
        for q in scenario(rng, machine, blocks, track, edge, bias, others):
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
    ap.add_argument("--edge", action="store_true", help="edge values, notes 0-127, trigs and gate-offs anywhere")
    ap.add_argument("--bias", type=parse_bias, default={}, help="machine bytes' own values, e.g. '4=47,38;12=4a'")
    ap.add_argument("--others", action="store_true", help="tracks 2-8's settings, trigs and gate-offs too")
    ap.add_argument("--prob", type=float, default=1.0, help="chance that a call is recorded")
    a = ap.parse_args()
    cases = record(set(a.routines), a.engines, a.blocks, a.seed, a.limit, a.prob, a.track, a.samples, edge=a.edge,
                   bias=a.bias, others=a.others)
    write_cases(a.out, cases)
    counts = {}
    for c in cases:
        counts[c[0]] = counts.get(c[0], 0) + 1
    print({f"{k:08x}": v for k, v in sorted(counts.items())})
    return 0


if __name__ == "__main__":
    sys.exit(main())
