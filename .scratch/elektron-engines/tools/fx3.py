"""CPU #1's mix and effects (section 3, OS 1.41) on oracle3's rig, with the eDMA the delay and reverb lines move by.

    rig = FxRig()
    rig.boot()                                  # the SRAM image and the two FX inits, as the OS starts
    res = rig.call(0x4008F1CA, [0, 0, P], ...)

What it adds to oracle3.Rig:
- Memory: the peripherals 0xFC040000-0xFC060000 as RAM (the eDMA's TCDs), the DDR the effects keep: the reverb's
  lines 0x4D570000-0x4D610000 (9 x 64 KB, then 8 x 4 KB at 0x4D600000) and the delay's ring 0x4FC00000 (4 MB).
  They are cleared before each call that does not keep the memory.
- The eDMA of channels 30 and 42, modelled at the CSR write that starts one (a 16-bit write to 0xFC04501E + 32 n, START):
  the channel's TCD as the code wrote it runs at once, before the next instruction, then each descriptor its
  scatter-gather address names (ESG, CSR bit 4), loaded from memory as the hardware loads it, while the loaded
  descriptor's START is set. A major loop is CITER minor loops (9 bits when ELINK, bit 15, is set); a minor loop moves
  NBYTES in SSIZE reads (SADDR += SOFF, modulo 2^SMOD) and DSIZE writes (DADDR += DOFF, modulo 2^DMOD). At the end the
  channel's TCD holds the last descriptor with START clear and DONE (bit 7) set, so that a poll of ESG passes. The
  bytes it reads (descriptors included) enter the call's read set, with their value before the call, and the bytes it
  writes its write set, as the CPU's do. Assumed: no transfer of a chain overlaps another's, and the hardware runs the
  chain to its end before the code reads what it moved (0x4008f1ca polls ESG on channel 30 before the delay; the write
  chain of 0x40096802 ends the routine's use of its buffers).
- The SRAM image the OS copies at boot (0x4000045e: 0x40339000-0x40340330 to 0x80000000, 0x40340330-0x40347b90 to
  0x80008000, the rest zero): the delay and reverb lines' lengths and the pointer 0x8000d6b0 live there.

The results are firmware-derived: keep them out of the repository.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import oracle3  # noqa: E402

PERIPHERALS = (0xFC04_0000, 0x2_0000)
TCD = 0xFC04_5000
LINES = (0x4D57_0000, 0xA_0000)
RING = (0x4FC0_0000, 0x40_0000)
EXTRA = [PERIPHERALS, LINES, RING]
SIZES = {0: 1, 1: 2, 2: 4, 4: 16, 5: 32}
CHANNELS = {30, 42}         # the effects' lines; channel 47's moves between the two CPUs stay the harness's

DELAY_INIT, REVERB_INIT = 0x4009_6668, 0x4009_6B86


def sram_image(blob: bytes) -> bytes:
    """CPU #1's SRAM as 0x4000045e leaves it."""
    def rom(a, n):
        o = a - oracle3.LOAD
        part = blob[o:o + n]
        return part + bytes(n - len(part))
    out = bytearray(0x1_0000)
    out[0:0x7330] = rom(0x4033_9000, 0x7330)
    out[0x8000:0x8000 + 0x7860] = rom(0x4034_0330, 0x7860)
    return bytes(out)


def mod_add(a: int, off: int, mod: int) -> int:
    if not mod:
        return (a + off) & 0xFFFF_FFFF
    m = (1 << mod) - 1
    return (a & ~m & 0xFFFF_FFFF) | ((a + off) & m)


class FxRig(oracle3.Rig):
    def __init__(self, roots: list[int]):
        super().__init__(roots)
        for base, size in EXTRA:
            self.uc.mem_map(base, size)

        self._pending: int | None = None
        self.chains: list[list[tuple[int, int, int]]] = []    # per started chain: (descriptor src, dst, bytes)
        self.sram = sram_image(self.blob)

    # -- the eDMA

    def _on_write(self, uc, access, address, size, value, ud):
        super()._on_write(uc, access, address, size, value, ud)
        ch = (address - TCD) // 32
        if ch in CHANNELS and (address - TCD) % 32 == 0x1E and size == 2 and value & 1:
            self._pending = ch

    def _on_code(self, uc, address, size, ud):
        if self._pending is not None:
            ch, self._pending = self._pending, None
            self._run(ch)
        super()._on_code(uc, address, size, ud)

    def _dma_read(self, a: int, n: int) -> bytes:
        t = self._trace
        if t is not None:
            self._note_reads(a, n)
        return bytes(self.uc.mem_read(a, n))

    def _dma_write(self, a: int, data: bytes) -> None:
        t = self._trace
        for k in range(len(data)):
            x = a + k
            if t is not None and not self._in_stack(x):
                if x not in t["old"]:
                    t["old"][x] = self._byte(x)
                t["written"].add(x)
        self.uc.mem_write(a, data)

    def _run(self, ch: int) -> None:
        reg = TCD + 32 * ch
        tcd = bytes(self.uc.mem_read(reg, 32))
        moves = []
        while True:
            (saddr, attr, soff, nbytes, _slast, daddr, citer, doff, dlast, _biter, csr) = \
                struct.unpack(">IHhIiIHhIHH", tcd)
            smod, ssize, dmod, dsize = attr >> 11, (attr >> 8) & 7, (attr >> 3) & 31, attr & 7
            count = citer & 0x1FF if citer & 0x8000 else citer & 0x7FFF
            sb, db = SIZES[ssize], SIZES[dsize]
            moves.append((saddr, daddr, count * nbytes))
            for _ in range(count):
                data = bytearray()
                for _ in range(nbytes // sb):
                    data += self._dma_read(saddr, sb)
                    saddr = mod_add(saddr, soff, smod)
                for k in range(nbytes // db):
                    self._dma_write(daddr, bytes(data[k * db:(k + 1) * db]))
                    daddr = mod_add(daddr, doff, dmod)
            if not csr & 0x10:
                break
            tcd = self._dma_read(dlast, 32)
            if not struct.unpack_from(">H", tcd, 30)[0] & 1:
                break
        last = bytearray(tcd)
        struct.pack_into(">H", last, 30, (struct.unpack_from(">H", last, 30)[0] & ~1) | 0x80)
        self.uc.mem_write(reg, bytes(last))
        self.chains.append(moves)

    # -- memory

    def _reset(self) -> None:
        super()._reset()
        for base, size in EXTRA:
            self.uc.mem_write(base, bytes(size))

    def boot(self, keep: bool = False) -> None:
        """The SRAM image, then the delay's and the reverb's inits, recorded nowhere. Their memsets (0x401159a4) run
        here as one write each: the ring's alone is 4 MB, which the emulator would take a million instructions on."""
        if not keep:
            self._reset()
        self.uc.mem_write(0x8000_0000, self.sram)
        for a, n in ((0x8000_DA1C, 0x10), (0x8000_D8F0, 0x18), (0x8000_D91C, 0x100), (0x8000_C608, 8),
                     (0x8000_C5BC, 0x40), (0x8000_C310, 0x200)):
            self.uc.mem_write(a, bytes(n))
        for a, n in ((0x4FC0_0000, 0x40_0000), (0x4D60_0000, 0x8000), (0x4D57_0000, 0x9_0000)):
            self.uc.mem_write(a, bytes(n))
        memset = 0x4011_59A4
        saved = bytes(self.uc.mem_read(memset, 2))
        self.uc.mem_write(memset, b"\x4e\x75")                 # rts
        try:
            self.call(DELAY_INIT, [], 0x20, None, keep=True)
            self.call(REVERB_INIT, [], 0x20, None, keep=True)
        finally:
            self.uc.mem_write(memset, saved)

    def state(self, base: int, size: int) -> bytes:
        return bytes(self.uc.mem_read(base, size))
