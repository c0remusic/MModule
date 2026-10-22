"""Record sequences of CPU #1's control routines (section 3, OS 1.41) for the C++ port, block after block.

    python sequence3.py OUT.stv [--sequences N] [--blocks B] [--seed S]

Each sequence runs, for B consecutive audio blocks, the routines the audio interrupt runs on the digital tracks'
parameters, in its order, on one memory that carries over from block to block: the smoothing 0x40097952, the
modulation matrix 0x40097afc, the LFOs 0x40119266, the pitch loop 0x400a4ade-0x400a4b40, the filter envelopes
0x4009652c and the cutoff 0x400956bc of tracks 0-7. Between blocks the harness plays: trigs and gate-offs, notes, a
knob turned now and then (a word of the engine copy), the MIDI sources moving, the held and gate masks.

File format "ST1S": vectors3.py's "ST1V" layout, the cases in the order they ran, and a case of routine 0 before each
block holding the bytes the harness wrote (as its write set; its one argument, 1, starts a sequence on a cleared
memory). The C++ test keeps one memory over a sequence: it
applies a case's read set only where no earlier case of the sequence wrote, and checks the bytes earlier cases wrote
against it instead, so that a port that drifts from the original shows at the first block it does.
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
import vectors3 as v  # noqa: E402

ROUTINES = [0x40097952, 0x40097AFC, 0x40119266, 0x400A4ADE, 0x4009652C, 0x400956BC]
INPUTS = 0


def engine_targets(rng) -> list[int]:
    """The engine copy's 938 words, the tracks' LFO, filter envelope and glide parameters plausible."""
    words = [v.word(rng, False) for _ in range(v.WORDS)]
    for t in range(13):
        base = 71 * t + 14
        for j in range(2):
            words[base + 1 + 8 * j:base + 9 + 8 * j] = v.lfo_params(rng, False)
        words[base + 31] = v.value_word(rng, rng.choice((0, 0, rng.randrange(1, 40))))
        words[base + 36] = rng.choice((0, 0x7F00))
        words[base + 52] = v.value_word(rng, rng.randrange(128))
        words[base + 53] = rng.choice((0, 0x7F00, rng.randrange(0x7F01)))
    return words


def settings(rng) -> bytes:
    out = bytearray(rng.randrange(256) for _ in range(0x64C))
    for t in range(13):
        base = 0x7C * t
        for k in range(24):
            struct.pack_into(">I", out, base + 4 * k, v.mod_pair(rng, False))
        out[base + 0x64] = rng.randrange(101)
        for off, values in ((0x68, (0, 1, 2)), (0x6C, (0, 1)), (0x70, (0, 1)), (0x74, (0, 1))):
            struct.pack_into(">I", out, base + off, rng.choice(values))
        out[base + 0x79] = rng.randrange(-64, 64) & 0xFF
        out[base + 0x7A] = rng.randrange(-12, 13) & 0xFF
    return bytes(out)


def initial(rng) -> dict[int, bytes]:
    targets = engine_targets(rng)
    states = [((t if t < 0x8000 else t - 0x10000) << 16) + rng.randrange(-(1 << 22), 1 << 22) for t in targets]
    mem = {
        v.ENGINE: v.be16(targets),
        v.SETTINGS: settings(rng),
        v.MACHINES: bytes(rng.choice((0x2C, 0x27, rng.randrange(0x40))) for _ in range(13)),
        v.STATE: v.be32(states),
        v.VELOCITY: v.be16(rng.randrange(128) << 8 for _ in range(12)),
        v.NOTES: v.be32(rng.randrange(128) << 16 for _ in range(13)),
        v.GLIDE: v.be32([0x8000_0000, 0] * 13),
        v.LAST_PITCH: v.be32(rng.randrange(128) << 16 for _ in range(13)),
        v.FILTER_ENV: v.be32([0, 0, 0] * 8),
        v.HELD: v.be16([0]), v.GATES: v.be16([0]),
    }
    for s in v.SOURCES:
        mem[s] = v.be16([0] * 13)
    for t in range(13):
        for j in range(2):
            mem[v.LFO_STATE + 0x50 * t - 0x28 * (1 - j) - 0x24] = bytes(0x26)
    return mem


class Harness:
    """What happens between two blocks: the bytes it writes, and the trigs and gate-offs of the next block."""

    def __init__(self, rng):
        self.rng = rng
        self.held = 0

    def step(self) -> tuple[dict[int, bytes], int, int]:
        rng = self.rng
        mem: dict[int, bytes] = {}
        trigs = sum(1 << t for t in range(12) if rng.random() < 0.08)
        offs = sum(1 << t for t in range(12) if rng.random() < 0.05) & ~trigs
        for t in range(13):
            if trigs >> t & 1:
                mem[v.NOTES + 4 * t] = v.be32([rng.randrange(24, 100) << 16])
                if t < 12:
                    mem[v.VELOCITY + 2 * t] = v.be16([rng.randrange(1, 128) << 8])
        self.held = (self.held | trigs) & ~offs
        mem[v.HELD] = v.be16([self.held])
        mem[v.GATES] = v.be16([self.held if rng.random() < 0.9 else rng.randrange(1 << 16)])
        if rng.random() < 0.1:                               # a knob turned
            t = rng.randrange(13)
            ident = rng.choice((1, 2, 3, 5, 9, 10, 11, 13, 16, 28, 30, 31, 32, 33, 34, 35, 52, 53, rng.randrange(71)))
            mem[v.ENGINE + 2 * (71 * t + 14 + ident)] = v.be16([v.word(rng, False)])
        if rng.random() < 0.1:                               # a MIDI source moves
            s = rng.choice(v.SOURCES)
            mem[s + 2 * rng.randrange(13)] = v.be16([v.signed_word(rng, False)])
        return mem, trigs, offs


def call(rig, routine, args, cases):
    if routine in v.SEGMENTS:
        end, regs = v.SEGMENTS[routine]
        rig.uc.mem_write(v.PITCH_FRAME - 0x5C, v.be32([args[1]]))
        res = rig.call(routine, [], 0x20, None, until=end, regs=regs(args), keep=True)
    else:
        res = rig.call(routine, args, 0x20, None, keep=True)
    cases.append((routine, 0x20, args, res))
    return res


def block(rig, cases, tempo, trigs, offs):
    p = call(rig, 0x40097952, [v.ENGINE], cases).d0
    call(rig, 0x40097AFC, [p], cases)
    call(rig, 0x40119266, [p, tempo, trigs], cases)
    call(rig, 0x400A4ADE, [p, trigs], cases)
    call(rig, 0x4009652C, [p, trigs, offs, 0], cases)
    for t in range(8):
        key = struct.unpack(">b", bytes(rig.uc.mem_read(p + 0x754 + 0x7C * t + 0x79, 1)))[0]
        pitch = struct.unpack(">I", bytes(rig.uc.mem_read(0x8000_4688 + 4 * t, 4)))[0]
        call(rig, 0x400956BC, [p + 0x8E * t + 0x52, key & 0xFFFF_FFFF, pitch, t], cases)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=Path)
    ap.add_argument("--sequences", type=int, default=3)
    ap.add_argument("--blocks", type=int, default=300)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    rig = oracle3.Rig(list(v.ROUTINES))
    cases = []
    for n in range(a.sequences):
        rng = random.Random(a.seed * 7919 + n)
        tempo = rng.choice((0x3840, rng.randrange(3600, 36001)))
        mem = initial(rng)
        rig.call(0x40118CF0, [0], 0x20, None)               # a call that resets the rig's memory, recorded nowhere
        harness = Harness(rng)
        for i in range(a.blocks):
            if i:
                mem, trigs, offs = harness.step()
            else:
                trigs = offs = 0
            for addr, data in mem.items():
                rig.uc.mem_write(addr, data)
            writes = {addr + k: b for addr, data in mem.items() for k, b in enumerate(data)}
            cases.append((INPUTS, 0, [] if i else [1], oracle3.Result(0, 0, {}, writes, 0)))
            block(rig, cases, tempo, trigs, offs)
        print(f"sequence {n}: {a.blocks} blocks, tempo {tempo}")
    v.write_cases(a.out, cases, b"ST1S")
    print(f"{len(cases)} cases, {a.out.stat().st_size} B -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
