"""The original firmware's outputs for a scenario of syntakt::Syntakt's calls (OS 1.41, CPU #1 and CPU #2).

    python syntakt3.py CALLS.txt OUT.sto --samples twinshot_samples.bin [--blocks B]

CALLS.txt is what `stSyntaktTest --log` writes: one call per line, the sample it is made at, its name, its
arguments (tempo BPMx120; machine t m; param t id value; fx id value; level t value; lock t id value; on t note
velocity; off t), and a last line "<samples> end". A call takes effect in block ceil(at / 32), as in the module.

The harness applies each call to the original CPU #1's memory as the module does (the seam of cpu1.md 1.1: the
engine copy's words, the machine bytes, the notes and velocities, the trig's locks through the original 0x40097760
and 0x400978b4, the tempo), from the module's initial state built here on its own rules, then for each block: the
masks, the gate words, the voice CPU's last output where the eDMA brings it, the original block segment
0x400a49a6-0x400a4db8 (block3), the original mix 0x4008f1ca (fx3's rig, the eDMA of the delay's and reverb's lines),
and the original voice CPU (SyntHack's harness, section 7) on the block the segment built.

The initial state (the module's, stSyntakt.cpp): CPU #1's SRAM image and the delay's and reverb's inits (fx3.boot);
each track's default machine (0x4022c8d8, the FX track's 0) and every internal id of tracks 0-11 below 54, of the FX
track below 72, at its descriptor's default (the first of 0x4022d59c naming it: the machine's group then 45, 47, 48,
59, 60; for the FX track 49, 50, 52, 53, 54, 59, 60; 0 where none does); each track's LEV (word t) at descriptor
10's default; the smoothing state at its targets; the settings 0; the kit pointer 0x800030b8 at 0x41b9c000 (its bytes
0); byte 0x80003145 = 1; 0x43bdc824 = 1; the tempo 120 BPM.

OUT ("ST1E"): u32 blocks, u32 channels (14), then per block each channel's 32 samples, little-endian int32: the
Overbridge frame's channels 2-9 (the stems) and 16-17 (the returns), the direct bus 0x8000dad0 L, R and the FX bus
0x8000dfd0 L, R as the mix leaves them. They hold firmware-derived data: keep them out of the repository.
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import block3  # noqa: E402
import mix3  # noqa: E402
import vectors3 as v  # noqa: E402

BLOCK = 32
TRACKS, ENGINE_TRACKS, FX_TRACK = 8, 13, 12
TRACK_IDS, FX_IDS = 54, 72
TUNE, OVERDRIVE = 18, 26
ENGINE, SETTINGS, MACHINES, STATE = v.ENGINE, v.SETTINGS, v.MACHINES, v.STATE
NOTES, VELOCITY = v.NOTES, v.VELOCITY
GATES, HELD = 0x8000_3152, 0x8000_3154
TEMPO, DMA, VOICE_OUT = block3.TEMPO, block3.DMA, block3.VOICE_OUT
KIT_POINTER, KIT_BYTES, LEVEL_TO_STEMS = 0x8000_30B8, 0x41B9_C000, 0x8000_3145
LOCK_LIST, KIT_SOUND = 0x8000_FC00, 0x8000_FD00
RESTORE_LOCKS, LOCK = 0x4009_7760, 0x4009_78B4
DESCRIPTORS, DEFAULT_MACHINES, LEVEL_DESCRIPTOR = 0x4022_D59C, 0x4022_C8D8, 10
TRACK_GROUPS, FX_GROUPS = (45, 47, 48, 59, 60), (49, 50, 52, 53, 54, 59, 60)
OVERBRIDGE, DIRECT, FX_BUS = 0x8000_4760, 0x8000_DAD0, 0x8000_DFD0
P = v.SMOOTHED


def track_word(t: int) -> int:
    return (0x8E * t + 0x1C) >> 1


def engine_word(t: int, ident: int) -> int:
    return ENGINE + 2 * (track_word(t) + ident)


class Original:
    def __init__(self, samples: Path):
        sys.path.insert(0, r"C:\dev\Synthack-OS\tools\emu")
        import engine as eng
        import vectors as v7
        self.r = mix3.rig()
        self.blob = self.r.blob
        self.e = eng.Engine()
        self.e.boot()
        v7.load_samples(self.e.uc, samples)
        self.kit = [[0] * TRACK_IDS for _ in range(TRACKS)]
        self.locks: list[list[tuple[int, int]]] = [[] for _ in range(TRACKS)]
        self.trigs = self.offs = self.next_offs = self.held = 0
        self.voices = bytes(0x400)
        self.descriptors = [struct.unpack_from(">5i", self.blob, DESCRIPTORS + 52 * i - 0x4000_0400)
                            for i in range(505)]
        self.initialize()

    # -- memory

    def w8(self, a, x):
        self.r.uc.mem_write(a, bytes([x & 0xFF]))

    def w16(self, a, x):
        self.r.uc.mem_write(a, struct.pack(">H", x & 0xFFFF))

    def w32(self, a, x):
        self.r.uc.mem_write(a, struct.pack(">I", x & 0xFFFF_FFFF))

    def rd(self, a, n) -> bytes:
        return bytes(self.r.uc.mem_read(a, n))

    def rom32(self, a) -> int:
        return struct.unpack_from(">I", self.blob, a - 0x4000_0400)[0]

    # -- the kit

    def default(self, group: int, ident: int):
        for g, i, _mn, _mx, df in self.descriptors:
            if g == group and i == ident:
                return df & 0xFFFF
        return None

    def default_of(self, machine: int, t: int, ident: int) -> int:
        groups = FX_GROUPS if t == FX_TRACK else (machine,) + TRACK_GROUPS
        for g in groups:
            d = self.default(g, ident)
            if d is not None:
                return d
        return 0

    def set_kit(self, t, ident, value):
        self.w16(engine_word(t, ident), value)
        if t < TRACKS:
            self.kit[t][ident] = value & 0xFFFF

    def initialize(self):
        self.r.boot()
        level = self.descriptors[LEVEL_DESCRIPTOR][4]
        for t in range(ENGINE_TRACKS):
            machine = 0 if t == FX_TRACK else self.rom32(DEFAULT_MACHINES + 4 * t)
            self.w8(MACHINES + t, machine)
            for ident in range(FX_IDS if t == FX_TRACK else TRACK_IDS):
                self.set_kit(t, ident, self.default_of(machine, t, ident))
            self.w16(ENGINE + 2 * t, level)
        words = struct.unpack(f">{v.WORDS}h", self.rd(ENGINE, 2 * v.WORDS))
        self.r.uc.mem_write(STATE, b"".join(struct.pack(">i", w << 16) for w in words))
        self.w32(KIT_POINTER, KIT_BYTES)
        self.w8(LEVEL_TO_STEMS, 1)
        self.w32(DMA, 1)
        self.w32(TEMPO, 120 * 120)

    # -- the calls

    def apply(self, name: str, a: list[int]) -> None:
        if name == "tempo":
            self.w32(TEMPO, a[0])
        elif name == "machine":
            t, m = a[0], a[1]
            self.w8(MACHINES + t, m)
            for ident in range(TUNE, OVERDRIVE + 1):
                self.set_kit(t, ident, self.default_of(m, t, ident))
        elif name == "param":
            self.set_kit(a[0], a[1], a[2])
        elif name == "fx":
            self.set_kit(FX_TRACK, a[0], a[1])
        elif name == "level":
            self.w16(ENGINE + 2 * a[0], a[1])
        elif name == "lock":
            if len(self.locks[a[0]]) < 16:
                self.locks[a[0]].append((a[1], a[2]))
        elif name == "on":
            self.note_on(a[0], a[1], a[2])
        elif name == "off":
            bit = 1 << a[0]
            if self.trigs & bit:
                self.next_offs |= bit
            else:
                self.offs |= bit
        else:
            raise ValueError(name)

    def note_on(self, t, note, velocity):
        self.w32(NOTES + 4 * t, max(0, min(127, note)) << 16)
        self.w16(VELOCITY + 2 * t, max(1, min(127, velocity)) << 8)
        self.r.uc.mem_write(KIT_SOUND + 0x14, b"".join(struct.pack(">H", x) for x in self.kit[t]))
        self.r.call(RESTORE_LOCKS, [t, ENGINE, KIT_SOUND], 0x20, None, keep=True)
        locks = self.locks[t]
        if locks:
            self.w32(LOCK_LIST + 8, len(locks))
            for i, (ident, value) in enumerate(locks):
                self.w16(LOCK_LIST + 0x14 + 8 * i, ident)
                self.w16(LOCK_LIST + 0x16 + 8 * i, value)
            self.r.call(LOCK, [t, LOCK_LIST, ENGINE], 0x20, None, keep=True)
            locks.clear()
        self.trigs |= 1 << t
        self.next_offs &= ~(1 << t)

    # -- a block

    def block(self) -> bytes:
        trigs, offs = self.trigs, self.offs & ~self.trigs
        open_ = self.held & ~self.offs
        self.w16(GATES, open_)
        self.w16(HELD, open_)
        self.held = open_ | self.trigs
        self.trigs, self.offs, self.next_offs = 0, self.next_offs, 0

        self.r.uc.mem_write(VOICE_OUT, self.voices)
        block3.segment(self.r, [trigs, offs, trigs, trigs, offs], None, keep=True, edges=False)
        self.r.call(mix3.MIX, [0x8000_0000, 0x8000_1800, P], 0x20, None, keep=True)

        ob = self.rd(OVERBRIDGE, 0x50 * BLOCK)
        direct, fx = self.rd(DIRECT, 8 * BLOCK), self.rd(FX_BUS, 8 * BLOCK)
        channels = [[struct.unpack_from(">i", ob, 0x50 * s + 8 + 4 * t)[0] for s in range(BLOCK)] for t in range(8)]
        for off in (0x40, 0x44):
            channels.append([struct.unpack_from(">i", ob, 0x50 * s + off)[0] for s in range(BLOCK)])
        for bus in (direct, fx):
            for side in (0, 4):
                channels.append([struct.unpack_from(">i", bus, 8 * s + side)[0] for s in range(BLOCK)])

        voices = self.e.render_block(block3.block_bytes(self.r))
        self.voices = b"".join(struct.pack(f">{len(x)}i", *x) for x in voices)
        return b"".join(struct.pack(f"<{BLOCK}i", *c) for c in channels)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("calls", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--samples", type=Path, required=True)
    ap.add_argument("--blocks", type=int, default=0, help="stop after this many blocks (a quick look)")
    a = ap.parse_args()

    calls, total = [], 0
    for text in a.calls.read_text().splitlines():
        parts = text.split()
        if parts[1] == "end":
            total = int(parts[0])
            continue
        calls.append((int(parts[0]), parts[1], [int(x) for x in parts[2:]]))
    blocks = (total + BLOCK - 1) // BLOCK
    if a.blocks:
        blocks = min(blocks, a.blocks)

    o = Original(a.samples)
    out, k, start = [], 0, time.time()
    for b in range(blocks):
        while k < len(calls) and (calls[k][0] + BLOCK - 1) // BLOCK <= b:
            o.apply(calls[k][1], calls[k][2])
            k += 1
        out.append(o.block())
        if b % 100 == 99:
            print(f"block {b + 1}/{blocks}, {time.time() - start:.0f} s", flush=True)
    with open(a.out, "wb") as f:
        f.write(b"ST1E" + struct.pack("<II", len(out), 14) + b"".join(out))
    print(f"{len(out)} blocks, {len(calls)} calls -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
