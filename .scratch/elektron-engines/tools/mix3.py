"""Record vectors of CPU #1's mix and effects (section 3, OS 1.41): 0x4008f1ca and what it calls, on fx3's rig.

    python mix3.py cases OUT.stv [--routines a,b,...] [--cases N] [--seed S] [--coverage REPORT.txt]
    python mix3.py sequence OUT.stv OUTPUTS.sto [--sequences N] [--blocks B] [--seed S] [--samples FILE]
                   [--coverage REPORT.txt]

`cases`: each routine alone (ROUTINES), from the state the OS's start leaves (fx3.boot: the SRAM image, the delay's
and reverb's inits) with what the routine reads drawn plausible, or at edges one case in four: the smoothed copy's
words, the gains' current values, the masks and flags, the voice outputs and the ADC, the lines' contents and
positions. 0x4008f1de is the mix's segment up to the delay (its stack frame on the rig's stack: a6 = STACK - 0x10,
a7 = STACK - 0x400); 0x40096668 and 0x40096b86 run with the memsets stubbed (fx3.boot). Format ST1V (vectors3.py).

`sequence`: block3's sequence (consecutive blocks of the interrupt's segment 0x400a49a6-0x400a4db8, the locks, the
original voice CPU playing each block), with the mix 0x4008f1ca after each segment on the same memory, from the OS's
start state: the voice outputs it mixes are the original voice CPU's of the block before (zeros for the first),
written as the eDMA writes them, in the next block's inputs (a case of routine 0). The ADC channels, the masks and
the effects' settings move between blocks. OUTPUTS as block3's. Format ST1S.

The vectors hold firmware-derived data: keep them out of the repository.
"""

from __future__ import annotations

import argparse
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import block3  # noqa: E402
import fx3  # noqa: E402
import oracle3  # noqa: E402
import sequence3 as s3  # noqa: E402
import vectors3 as v  # noqa: E402
from unicorn import m68k_const as m  # noqa: E402

MIX = 0x4008_F1CA
SEGMENT, SEGMENT_END = 0x4008_F1DE, 0x4008_F80A
P = v.SMOOTHED
VOICE_OUT, ADC = 0x8000_3D50, 0x8000_3950
KIT_POINTER, KIT = 0x8000_30B8, 0x41B9_C000
CALLEES = [0x4008_EF14, 0x4008_EFAC, 0x4008_F03E, 0x4008_F1B4, 0x4008_EE80, 0x4008_EECA, 0x4008_FEBC, 0x4009_7A90,
           0x4008_ED48, 0x400A_171A, 0x400A_16BC, 0x4009_7414, 0x4009_6890, 0x4009_6E3C, 0x4009_6DBA, 0x4009_6802,
           0x4009_5580, 0x4009_5588]
FRAME_A6, FRAME_SP = oracle3.STACK - 0x10, oracle3.STACK - 0x400
SCRATCH = 0x8000_FC00                                       # free SRAM for a case's gains and steps

TRACK_SAMPLE = 1 << 27


def rig() -> fx3.FxRig:
    return fx3.FxRig([MIX] + CALLEES + [fx3.DELAY_INIT, fx3.REVERB_INIT, SEGMENT] + list(v.ROUTINES) + [block3.START])


def i32(x: int) -> int:
    return x & 0xFFFF_FFFF


def samples(rng, n, edge, scale=TRACK_SAMPLE) -> bytes:
    if edge and rng.random() < 0.3:
        return v.be32(rng.choice((0x7FFF_FFFF, -0x8000_0000, 0, -1, rng.randrange(-1 << 31, 1 << 31)))
                      for _ in range(n))
    return v.be32(int(rng.gauss(0, scale)) if rng.random() < 0.9 else rng.randrange(-scale, scale) for _ in range(n))


def gain(rng, edge) -> int:
    """A gain in the mix's negative form, mostly as a ramp leaves it."""
    if edge and rng.random() < 0.3:
        return rng.choice((0, -1, 0x8000_0000, 0x7FFF_FFFF, 1, rng.randrange(1 << 32)))
    return -rng.randrange(1 << 31) if rng.random() < 0.7 else 0


def word_or_edge(rng, edge, top=0x7F00):
    return v.word(rng, edge) if edge else rng.choice((0, top, rng.randrange(top + 1), rng.randrange(top + 1)))


# -- a state

def fx_params(rng, edge) -> dict[int, bytes]:
    """The smoothed copy's delay and reverb words (P+0x6e6..0x709), the external input's and FX track's (0x70a..)."""
    words = [word_or_edge(rng, edge) for _ in range((0x750 - 0x6E6) // 2)]
    for off, choices in ((0x6F6, (0, 0, 0x7F00, 1)), (0x708, (0, 0, 0x7F00, 1)), (0x71A, (0, 0x7F00)),
                         (0x742, (0, 0x7F00, 0x100)), (0x71C, (0, 0x100, 0x700, 0x800, 0xFF00))):
        if not edge or rng.random() < 0.5:
            words[(off - 0x6E6) // 2] = rng.choice(choices)
    return {P + 0x6E6: v.be16(words)}


def track_words(rng, edge) -> dict[int, bytes]:
    words = [word_or_edge(rng, edge) for _ in range(v.WORDS)]
    for t in range(13):
        for off, choices in ((0x7E, (0, 0, 0x7F00, 1)),):
            if not edge or rng.random() < 0.7:
                words[(0x8E * t + off) // 2] = rng.choice(choices)
    return {P: v.be16(words)}


def mix_inputs(rng, edge) -> dict[int, bytes]:
    """What the mix reads beyond the smoothed copy and the effects' state: voices, ADC, masks, flags, gains."""
    mem = {
        VOICE_OUT: samples(rng, 0x100, edge), ADC: samples(rng, 0x100, edge),
        0x8000_4150: samples(rng, 0x40, edge),
        KIT_POINTER: v.be32([KIT]),
        KIT: bytes(rng.randrange(256) for _ in range(0x123 * 12 + 0x10)),
        v.VELOCITY: v.be16(rng.randrange(256) << 8 | rng.randrange(256) if edge else rng.randrange(128) << 8
                           for _ in range(12)),
        v.MACHINES: bytes(rng.choice((32, 33, 34, 40, 0, 7, 44, rng.randrange(256))) for _ in range(13)),
        block3.TEMPO: v.be32([rng.choice(v.TEMPOS[:6]) if edge else rng.choice((0x3840, rng.randrange(3600, 36001)))]),
        0x8000_6220: v.be32(gain(rng, edge) for _ in range(0x44)),
        0x8000_E0B0: v.be32(gain(rng, edge) for _ in range(0x40)),
        0x8000_3148: v.be16(rng.choice((0, 0, 0, rng.randrange(1 << 16), 1 << 12, 1 << 13, 1 << 14, 0x8000 | rng.randrange(1 << 15)))
                            for _ in range(4)),
        0x8000_313C: bytes(rng.choice((0, 1, 2, 0xFF)) if (edge or k in (1, 0)) else rng.choice((0, 1))
                           for k in range(11)),
        0x4553_F6DC: v.be32([rng.randrange(2)]),
        0x43BD_C4D8: v.be32([rng.randrange(2)]),
        0x43BD_7684: v.be32([rng.randrange(1 << 31), rng.randrange(1 << 31)] + [rng.randrange(1 << 31) for _ in range(12)]),
        0x43BD_BF64: v.be32([gain(rng, edge)]) + samples(rng, 33, edge),
        0x8000_E1B0: samples(rng, 0x200, edge),
        0x454E_3C18: v.be32(rng.randrange(1 << 31) if rng.random() < 0.8 else rng.randrange(1 << 32) for _ in range(16)),
    }
    if not edge:
        mem[0x8000_3144] = bytes([rng.choice((0, 1, 2, 3))])    # the DAC's share: 3 - it is a shift
    mem[P + 0x740] = v.be16([rng.choice((0, 0x100, 0x7F00, rng.randrange(0x8000)))])
    return mem


def fx_state(rng, edge, r: fx3.FxRig) -> dict[int, bytes]:
    """The delay's and the reverb's state as blocks leave it: positions, filters, gains, buffers and lines."""
    sram = r.sram_booted
    def rd(a):
        return struct.unpack_from(">I", sram, a - 0x8000_0000)[0]
    mem = {
        0x8000_DAC0: v.be32([rng.randrange(0x5DC00 << 5), rng.randrange(0x5DC00), rng.randrange(0x80000)]),
        0x8000_D908: v.be32([rng.randrange(1 << 31), rng.choice((0x400_0000, rng.randrange(1 << 27))),
                             rd(0x8000_D910), rd(0x8000_D914), gain(rng, edge)]),
        0x8000_DA1C: samples(rng, 4, edge),
        0x8000_C300: v.be32(rng.randrange(1 << 31) for _ in range(4)),
        0x8000_C514: v.be32([rng.randrange(-1 << 31, 1 << 31), rng.randrange(-1 << 31, 1 << 31)]),
        0x8000_C608: samples(rng, 2, edge),
        0x8000_C5BC: samples(rng, 16, edge),
        0x8000_C63C: v.be32([rng.randrange(0x25, 0x4001)]),
        0x8000_D690: v.be32(4 * rng.randrange(0x400) for _ in range(8)),
        0x8000_C660: v.be32(4 * rng.randrange(0x4000) for _ in range(9)),
        0x8000_C310: samples(rng, 0x80, edge),
        0x8000_C53C: samples(rng, 0x20, edge),
        0x8000_CAF0: samples(rng, 0x180, edge),          # the lines' buffers, 0x8000caf0-0x8000d0f0
        0x8000_D104: samples(rng, 0x140, edge),          # the diffusers', 0x8000d104-0x8000d604
        0x8000_D6C0: samples(rng, 0x90, edge),           # the delay's taps and scratch
        0x8000_D91C: samples(rng, 0x40, edge),
        0x8000_DAD0: samples(rng, 0x178, edge),           # the buses, 0x8000dad0-0x8000e0b0
    }
    # The modulated lines' positions sit within a sample or two of their lengths (0x40096b86 sets them so)
    for at, base in ((0x8000_C528, 0x8000_C610), (0x8000_C52C, 0x8000_C614)):
        mem[at] = v.be32([rd(base) + rng.randrange(-3, 4) - (8 if at == 0x8000_C52C and rng.random() < 0.2 else 0)])
    if edge and rng.random() < 0.3:
        mem[0x8000_C510] = v.be32([rng.choice((0, 0x7FFF_FFFF, rng.randrange(1 << 28)))])
    mem[fx3.RING[0]] = rng.randbytes(fx3.RING[1]) if rng.random() < 0.8 else bytes(fx3.RING[1])
    mem[fx3.LINES[0]] = rng.randbytes(fx3.LINES[1]) if rng.random() < 0.8 else bytes(fx3.LINES[1])
    return mem


def state(rng, edge, r) -> dict[int, bytes]:
    mem = {0x8000_0000: r.sram_booted}
    for part in (track_words(rng, edge), fx_params(rng, edge), mix_inputs(rng, edge), fx_state(rng, edge, r)):
        mem.update(part)
    return mem


# -- one generator per routine: (args, macsr, memory, regs or None, until or None)

def bus(rng):
    return rng.choice((0x8000_DAD0, 0x8000_DAD4, 0x8000_DFD0, 0x8000_DBD0, 0x8000_DED4))


def gen_sum(channels):
    def gen(rng, edge, r):
        mem = state(rng, edge, r)
        mem[SCRATCH] = v.be32(gain(rng, edge) for _ in range(channels))
        mem[SCRATCH + 0x40] = v.be32((gain(rng, edge) >> 5 if not edge else rng.randrange(1 << 32)) for _ in range(channels))
        src = rng.choice((VOICE_OUT, ADC)) if channels <= 8 else VOICE_OUT
        return [bus(rng), src, SCRATCH, SCRATCH + 0x40], rng.choice((0xA0, 0xA0, 0x20)), mem, None, None
    return gen


def gen_copy(rng, edge, r):
    return [rng.choice((0x8000_4150, 0x8000_41D0)), rng.choice((0x8000_DCD0, 0x8000_DDD4))], 0xA0, state(rng, edge, r), None, None


def gen_mix_buses(rng, edge, r):
    mem = state(rng, edge, r)
    mem[SCRATCH] = v.be32([0x8000_0000, 0x8000_0000] if rng.random() < 0.6 else [gain(rng, edge), gain(rng, edge)])
    mem[SCRATCH + 8] = v.be32([0, 0] if rng.random() < 0.6 else [gain(rng, edge) >> 5, gain(rng, edge) >> 5])
    return [0x8000_DED0, 0x8000_DED0, SCRATCH, SCRATCH + 8], 0xA0, mem, None, None


def gen_scale(rng, edge, r):
    return [bus(rng), rng.choice((8, 10, 1, 0x7FFF_FFFF) if edge else (8, 10))], 0xA0, state(rng, edge, r), None, None


def gen_ramp(rng, edge, r):
    return [rng.choice((VOICE_OUT + 128 * rng.randrange(8), ADC + 128 * rng.randrange(4))), i32(gain(rng, edge)),
            i32(gain(rng, edge))], 0xA0, state(rng, edge, r), None, None


def gen_level(rng, edge, r):
    ident = rng.randrange(256) - (128 if edge else 0) if edge else rng.randrange(8, 64)
    return [i32(ident), rng.choice((0x7F00, rng.randrange(1 << 32)))], 0xA0, state(rng, edge, r), None, None


def gen_prepare(rng, edge, r):
    return [P], 0x20, state(rng, edge, r), None, None


def gen_delay(rng, edge, r):
    return [0x8000_DCD0, 0x8000_DBD0, P + 0x6E6], 0xA0, state(rng, edge, r), None, None


def gen_plain(rng, edge, r):
    return [], 0xA0, state(rng, edge, r), None, None


def gen_write_lines(rng, edge, r):
    """Channel 42's descriptors aimed as 0x40096dba leaves them, at the lines' write positions."""
    mem = state(rng, edge, r)
    small = struct.unpack(">8I", mem[0x8000_D690])
    big = struct.unpack(">9I", mem[0x8000_C660])
    for k, w in enumerate(small):
        mem[0x8000_C6B0 + 0x20 * k] = v.be32([0x4D60_0000 + 0x1000 * k + w])
    for k, w in enumerate(big):
        mem[0x8000_C7B0 + 0x20 * k] = v.be32([0x4D57_0000 + 0x1_0000 * k + w])
    return [], 0xA0, mem, None, None


def gen_reverb(rng, edge, r):
    return [0x8000_DDD0, 0x8000_DBD0, P + 0x6FA], 0xA0, state(rng, edge, r), None, None


def gen_segment(rng, edge, r):
    regs = {m.UC_M68K_REG_A2: P, m.UC_M68K_REG_A6: FRAME_A6, m.UC_M68K_REG_A7: FRAME_SP}
    return [P], 0x20, state(rng, edge, r), regs, SEGMENT_END


def gen_mix(rng, edge, r):
    return [0x8000_0000, 0x8000_1800, P], 0x20, state(rng, edge, r), None, None


ROUTINES = {
    0x4008_EF14: ("sum10", gen_sum(10)),
    0x4008_EFAC: ("sum9", gen_sum(9)),
    0x4008_F03E: ("sum8", gen_sum(8)),
    0x4008_F1B4: ("copySide", gen_copy),
    0x4008_EE80: ("mixBuses", gen_mix_buses),
    0x4008_EECA: ("scaleBus", gen_scale),
    0x4008_FEBC: ("rampBuffer", gen_ramp),
    0x4009_7A90: ("analogLevel", gen_level),
    0x4009_7414: ("prepareFx", gen_prepare),
    0x4009_6890: ("delay", gen_delay),
    0x4009_6802: ("writeLines", gen_write_lines),
    0x4009_6E3C: ("reverb", gen_reverb),
    0x4009_6DBA: ("advanceLines", gen_plain),
    SEGMENT: ("trackSends", gen_segment),
    MIX: ("mixOutput", gen_mix),
}


def run(r, routine, gen, rng, edge, edges):
    args, macsr, mem, regs, until = gen(rng, edge, r)
    if until is None:
        return args, macsr, r.call(routine, args, macsr, mem, edges=edges)
    res = r.call(routine, args, macsr, mem, edges=edges, until=until, regs=regs)
    return args, macsr, res


def strip(res):
    """The eDMA's registers are the port's arguments, not memory: they leave the write set."""
    lo, n = fx3.PERIPHERALS
    res.writes = {a: b for a, b in res.writes.items() if not lo <= a < lo + n}
    res.reads = {a: b for a, b in res.reads.items() if not lo <= a < lo + n}
    return res


def inits(r, out):
    """0x40096668 and 0x40096b86 on the SRAM image, their memsets stubbed as fx3.boot runs them."""
    memset = 0x4011_59A4
    for routine in (fx3.DELAY_INIT, fx3.REVERB_INIT):
        r._reset()
        r.uc.mem_write(0x8000_0000, r.sram)
        saved = bytes(r.uc.mem_read(memset, 2))
        r.uc.mem_write(memset, b"\x4e\x75")
        try:
            res = strip(r.call(routine, [], 0x20, None, keep=True))
        finally:
            r.uc.mem_write(memset, saved)
        out.append((routine, 0x20, [], res))


def boot_state(r) -> bytes:
    r.boot()
    return r.state(0x8000_0000, 0x1_0000)


def cases(a) -> int:
    r = rig()
    r.sram_booted = boot_state(r)
    out, edges = [], {}
    routines = [int(x, 16) for x in a.routines.split(",")] if a.routines else list(ROUTINES)
    for routine in routines:
        name, gen = ROUTINES[routine]
        rng = random.Random(a.seed * 1000003 + routine)
        cover = edges.setdefault(routine, set())
        for i in range(a.cases):
            args, macsr, res = run(r, routine, gen, rng, i % 4 == 0, a.coverage is not None)
            cover |= res.edges
            out.append((routine, macsr, args, strip(res)))
        print(f"{routine:08x} {name:12} {a.cases} cases")
    if not a.routines:
        inits(r, out)
    v.write_cases(a.out, out)
    print(f"{len(out)} cases, {a.out.stat().st_size} B -> {a.out}")
    if a.coverage:
        report(r.blob, edges, a.coverage)
    return 0


# -- a sequence

def moves(rng, mem):
    """Between two blocks: the ADC, the masks, the effects' settings (in the engine copy), the flags."""
    mem[ADC] = samples(rng, 0x100, False)
    mem[0x8000_E1B0] = samples(rng, 0x200, False)
    mem[0x43BD_BF68] = samples(rng, 32, False)
    mem[0x4553_F6DC] = v.be32([rng.randrange(2)])
    if rng.random() < 0.02:
        mem[0x8000_3148] = v.be16([rng.choice((0, 0, 1 << rng.randrange(16), 0x8000 | rng.randrange(1 << 15)))
                                   for _ in range(4)])
    if rng.random() < 0.05:
        k = rng.randrange((0x750 - 0x6E6) // 2)
        mem[v.ENGINE + 0x6E6 + 2 * k] = v.be16([rng.choice((0, 0x7F00, rng.randrange(0x7F01)))])
    if rng.random() < 0.05:
        t = rng.randrange(12)
        off = rng.choice((0x76, 0x78, 0x7A, 0x7C, 0x7E))
        mem[v.ENGINE + 0x8E * t + off] = v.be16([rng.choice((0, 0x7F00, rng.randrange(0x7F01)))])


def sequence(a) -> int:
    sys.path.insert(0, r"C:\dev\Synthack-OS\tools\emu")
    import engine as eng
    import vectors as v7
    r = rig()
    out, outputs, cover = [], [], set()
    for n in range(a.sequences):
        rng = random.Random(a.seed * 7919 + n)
        mem = block3.sequence_state(rng)
        ids = list(mem[v.MACHINES][:8])
        # The kit's effects and the mix's own state as the OS starts it, then plausible settings
        r.boot()
        boot = {0x8000_0000: r.state(0x8000_0000, 0x1_0000)}
        # The effects within reach of a few hundred blocks: a short delay (TIME, a fast tempo) and pre-delay (PRE),
        # most tracks direct; the smoothing's state at its targets, so that a word set to 0 is 0
        targets = bytearray(mem[v.ENGINE])
        for k, w in enumerate(struct.unpack(f">{(0x750 - 0x6E6) // 2}H", fx_params(rng, False)[P + 0x6E6])):
            struct.pack_into(">H", targets, 0x6E6 + 2 * k, w)
        struct.pack_into(">H", targets, 0x6E6, rng.randrange(0x400))
        struct.pack_into(">H", targets, 0x6FA, rng.randrange(0x2000))
        if n % 2:                                           # a tail that dies away rather than one that saturates
            struct.pack_into(">H", targets, 0x6EC, rng.randrange(0x2000, 0x6000))
        for off in (0x6F4, 0x706):
            struct.pack_into(">H", targets, off, rng.randrange(0x4000, 0x7F01))
        for t in range(12):
            struct.pack_into(">H", targets, 0x8E * t + 0x7E, rng.choice((0, 0, 0, 0x7F00)))
            for off in (0x76, 0x78):
                struct.pack_into(">H", targets, 0x8E * t + off, rng.randrange(0x3000, 0x7F01))
        mem[v.ENGINE] = bytes(targets)
        words = struct.unpack(f">{v.WORDS}h", targets)
        mem[v.STATE] = v.be32(w << 16 for w in words)
        mem[block3.TEMPO] = v.be32([rng.randrange(150, 301) * 120])
        extra = mix_inputs(rng, False)
        for k in (VOICE_OUT, v.VELOCITY, v.MACHINES, block3.TEMPO, P + 0x740):
            extra.pop(k, None)
        extra[VOICE_OUT] = bytes(0x400)
        extra[0x8000_3148] = v.be16([0, 0, 0, 0])
        e = eng.Engine()
        e.boot()
        if a.samples:
            v7.load_samples(e.uc, a.samples)
        sched = block3.Schedule(rng)
        previous = bytes(0x400)
        for i in range(a.blocks):
            if i:
                mem, masks, locks = sched.step()
                mem[VOICE_OUT] = previous
                moves(rng, mem)
            else:
                masks, locks = [0] * 5, []
                mem = {**boot, **mem, **extra}
            for t, lst in locks:
                mem[v.SCRATCH + 0x40 * t] = lst
            for addr, data in mem.items():
                r.uc.mem_write(addr, data)
            writes = {addr + k: b for addr, data in mem.items() for k, b in enumerate(data)}
            out.append((s3.INPUTS, 0, [] if i else [1], oracle3.Result(0, 0, {}, writes, 0)))
            for t, _ in locks:
                res = r.call(block3.LOCK, [t, v.SCRATCH + 0x40 * t, v.ENGINE], 0x20, None, keep=True)
                out.append((block3.LOCK, 0x20, [t, v.SCRATCH + 0x40 * t, v.ENGINE], res))
            res = block3.segment(r, masks, None, keep=True, edges=False)
            out.append((block3.START, 0x20, masks, res))
            block = block3.block_bytes(r)
            res = strip(r.call(MIX, [0x8000_0000, 0x8000_1800, P], 0x20, None, keep=True,
                               edges=a.coverage is not None))
            cover |= res.edges
            out.append((MIX, 0x20, [0x8000_0000, 0x8000_1800, P], res))
            voices = e.render_block(block)
            outputs.append(b"".join(struct.pack(f"<{len(x)}i", *x) for x in voices))
            previous = b"".join(struct.pack(f">{len(x)}i", *x) for x in voices)
        print(f"sequence {n}: {a.blocks} blocks, machines {ids}")
    v.write_cases(a.out, out, b"ST1S")
    with open(a.outputs, "wb") as f:
        f.write(b"ST1O" + struct.pack("<I", len(outputs)) + b"".join(outputs))
    print(f"{len(out)} cases, {len(outputs)} blocks -> {a.out} ({a.out.stat().st_size} B), {a.outputs}")
    if a.coverage:
        report(r.blob, {MIX: cover}, a.coverage)
    return 0


# -- coverage

DEAD_SIDES: dict[int, str] = {
    0x4008_F816: "taken: the poll of channel 30's ESG loops while the hardware still reads; the rig's eDMA (and the "
                 "port) has ended the chain at its start, so the first read passes",
}


def report(blob: bytes, edges: dict[int, set[int]], path: Path) -> None:
    import coverage_insn
    from capstone import CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_040, Cs
    cs = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_040)
    cover = set().union(*edges.values()) if edges else set()
    executed = {e & 0xFFFF_FFFF for e in cover}
    lines = []
    parts = [(SEGMENT, block3.linear(blob, SEGMENT, SEGMENT_END, cs))] if SEGMENT in edges else []
    for root in sorted(set(oracle3.reach(blob, [r for r in edges if r != SEGMENT] + CALLEES))):
        if root in edges or root in CALLEES or root == MIX:
            parts.append((root, coverage_insn.routine_insns(blob, root, cs)))
    seen = set()
    for root, insns in parts:
        if root in seen:
            continue
        seen.add(root)
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
        name = f"segment {SEGMENT:08x}-{SEGMENT_END:08x}" if root == SEGMENT else f"{root:08x}"
        lines.append(f"  {name:26} insns {hit:4}/{len(insns):<4} branch sides {nbs:3}/{2 * nb:<3}"
                     + (f"  unexecuted: {coverage_insn.ranges(unexec, insns)}" if unexec else ""))
        lines.extend(gaps)
    text = "\n".join(lines) + "\n"
    path.write_text(text, encoding="utf-8")
    print(text)


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="mode", required=True)
    c = sub.add_parser("cases")
    c.add_argument("out", type=Path)
    c.add_argument("--routines", default="")
    c.add_argument("--cases", type=int, default=200)
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
