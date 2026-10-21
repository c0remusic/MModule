"""Routine-level oracle for the Syntakt's voice CPU (section 7, OS 1.41).

Runs SyntHack's harness (the original code on Unicorn with its EMAC model), stops at the
entry of a routine during a chosen block, and keeps the machine's memory there. A routine can
then be called alone on that memory with any arguments, and what it changed is returned:
the reference a C++ port of that routine is compared with.

    snap = Snapshot.capture(machine=4, note=60, block=3, at=0x40007214)
    snap.save(path)                       # firmware-derived: keep it outside the repository
    res = Snapshot.load(path).call(0x40003c0e, args=[voice, 2])
    res.d0, res.changes                   # {address: (before, after)} in 4-byte words
"""

from __future__ import annotations

import pickle
import struct
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path

SYNTHACK_EMU = Path(r"C:\dev\Synthack-OS\tools\emu")
sys.path.insert(0, str(SYNTHACK_EMU))

import cli  # noqa: E402
import engine as eng  # noqa: E402
import memmap  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.m68k_const import (  # noqa: E402
    UC_M68K_REG_A0, UC_M68K_REG_A7, UC_M68K_REG_D0, UC_M68K_REG_PC, UC_M68K_REG_SR)

PAGE = 0x1000
RAM = ("shared", "ddr", "sram")
RETURN = 0x47FF_FF00          # a return address no code uses: the call stops there
STACK = 0x47F0_0000
REGS = [UC_M68K_REG_D0 + i for i in range(8)] + [UC_M68K_REG_A0 + i for i in range(8)]


def _pages(uc) -> dict[int, bytes]:
    """Every non-zero page of RAM."""
    out = {}
    for r in memmap.REGIONS:
        if r.name not in RAM:
            continue
        for base in range(r.base, r.base + r.size, 1 << 20):
            n = min(1 << 20, r.base + r.size - base)
            chunk = bytes(uc.mem_read(base, n))
            if not any(chunk):
                continue
            for off in range(0, n, PAGE):
                page = chunk[off:off + PAGE]
                if any(page):
                    out[base + off] = page
    return out


@dataclass
class Result:
    d0: int
    changes: dict[int, tuple[int, int]]
    instructions: int


@dataclass
class Snapshot:
    pages: dict[int, bytes]
    regs: list[int]
    sr: int
    stack_args: list[int] = field(default_factory=list)

    @staticmethod
    def capture(machine: int, note: int, block: int, at: int, nargs: int = 2) -> "Snapshot":
        e = eng.Engine()
        e.boot()
        params = cli.make_params(machine, note)
        found: list[Snapshot] = []

        def hit(uc, addr, size, _ud):
            if found or e.current_block != block:
                return
            sp = uc.reg_read(UC_M68K_REG_A7)
            args = [int.from_bytes(uc.mem_read(sp + 4 + 4 * i, 4), "big") for i in range(nargs)]
            regs = [uc.reg_read(r) for r in REGS]
            found.append(Snapshot(_pages(uc), regs, uc.reg_read(UC_M68K_REG_SR), args))

        e.uc.hook_add(UC_HOOK_CODE, hit, begin=at, end=at)
        for b in range(block + 1):
            e.current_block = b
            q = bytearray(params)
            if b in (cli.TRIG_LOAD, cli.TRIG_NOTE):
                q[0x180] = 0x01
            e.render_block(bytes(q))
        if not found:
            raise RuntimeError(f"{at:08x} not reached in block {block}")
        return found[0]

    def save(self, path: Path) -> None:
        Path(path).write_bytes(zlib.compress(pickle.dumps(self), 6))

    @staticmethod
    def load(path: Path) -> "Snapshot":
        # Only snapshots this module saved on this computer are loaded: pickle is never fed
        # a file from elsewhere.
        return pickle.loads(zlib.decompress(Path(path).read_bytes()))

    def machine(self) -> eng.Engine:
        e = eng.Engine()
        for addr, page in self.pages.items():
            e.uc.mem_write(addr, page)
        return e

    def read(self, addr: int, size: int) -> bytes:
        out = bytearray()
        while size > 0:
            base = addr & ~(PAGE - 1)
            off = addr - base
            n = min(size, PAGE - off)
            out += self.pages.get(base, bytes(PAGE))[off:off + n]
            addr, size = addr + n, size - n
        return bytes(out)

    def call(self, routine: int, args: list[int] | None = None, regs: dict[int, int] | None = None,
             watch: list[tuple[int, int]] | None = None, budget: int = 5_000_000) -> Result:
        """Call ROUTINE with ARGS on the stack (C convention), from this memory and registers."""
        e = self.machine()
        uc = e.uc
        uc.reg_write(UC_M68K_REG_SR, self.sr)              # before A7: writing SR switches stacks
        for r, v in zip(REGS, self.regs):
            uc.reg_write(r, v)
        for r, v in (regs or {}).items():
            uc.reg_write(r, v)
        args = self.stack_args if args is None else args
        sp = STACK - 4 * (len(args) + 1)
        uc.mem_write(sp, struct.pack(f">{len(args) + 1}I", RETURN, *args))
        uc.reg_write(UC_M68K_REG_A7, sp)
        before = _pages(uc)
        count = [0]
        uc.hook_add(UC_HOOK_CODE, lambda *_: count.__setitem__(0, count[0] + 1))
        uc.emu_start(routine, RETURN, count=budget)
        if uc.reg_read(UC_M68K_REG_PC) != RETURN:
            raise RuntimeError(f"{routine:08x} did not return within {budget} instructions")
        if e.emac.error:
            raise RuntimeError(f"EMAC: {e.emac.error}")
        after = _pages(uc)
        changes = {}
        for base in sorted(set(before) | set(after)):
            if STACK - 0x10000 <= base < STACK + PAGE:
                continue                                    # the call's own stack frame
            b0, b1 = before.get(base, bytes(PAGE)), after.get(base, bytes(PAGE))
            if b0 == b1:
                continue
            for off in range(0, PAGE, 4):
                w0, w1 = b0[off:off + 4], b1[off:off + 4]
                if w0 != w1:
                    changes[base + off] = (int.from_bytes(w0, "big"), int.from_bytes(w1, "big"))
        return Result(uc.reg_read(UC_M68K_REG_D0), changes, count[0])
