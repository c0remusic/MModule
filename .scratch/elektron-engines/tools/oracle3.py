"""Routine-level oracle for the Syntakt's main CPU (CPU #1, section 3, OS 1.41).

Runs one routine of section 3 alone on Unicorn, with the memory the caller gives it, and returns what it did:

    rig = Rig()
    res = rig.call(0x4008ED48, args=[level, vel, vol, mode], macsr=0x20, memory={0x80000000: b"..."})
    res.d0, res.d1, res.reads, res.writes, res.instructions, res.edges

- Memory: section 3 at 0x40000400 (DDR from 0x40000000, 4 MB: code, tables, initialized data), the DDR BSS pages
  cpu1.md lists, CPU #1's SRAM 0x80000000 (64 KB), a stack. Everything else is unmapped: an access there stops the
  call and is reported as a fault (address and pc), as are an exception, a budget run out, a callee-saved register
  changed, or the stack pointer not restored.
- EMAC: SyntHack's UnicornEmac (tools/emu/emac.py) on the EMAC instructions of the routines run and their callees,
  found by cfdis's recursive descent (cfdis.is_emac); a site that is missed hits Unicorn's line-A exception, which is
  reported. Accumulators start cleared, as the audio interrupt leaves them (0x400A38B2: movclr of all four) and every
  routine returns them (movclr); MACSR is the caller's.
- Sets: every byte the routine reads (its value before the call) and every byte it writes (its value after), through
  memory hooks and the EMAC's own loads, outside the call's stack frame. A byte of section 3 read with the file's
  value is left out of the read set: the port reads section 3 itself.

The routines' results are firmware-derived: keep them out of the repository.
"""

from __future__ import annotations

import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

TOOLS = Path(__file__).parent
ROOT = TOOLS.parent
SECTION3 = ROOT / "syntakt-1.41" / "section_3_MAIN_OS.bin"
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, r"C:\dev\Synthack-OS\tools\emu")

import cfdis  # noqa: E402
from capstone import CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_040, Cs  # noqa: E402
from emac import UnicornEmac  # noqa: E402
from unicorn import (UC_ARCH_M68K, UC_HOOK_CODE, UC_HOOK_INTR, UC_HOOK_MEM_INVALID, UC_HOOK_MEM_READ,  # noqa: E402
                     UC_HOOK_MEM_WRITE, UC_MODE_BIG_ENDIAN, Uc, UcError)
from unicorn import m68k_const as m68k  # noqa: E402

LOAD = 0x4000_0400
STACK_BASE, STACK_SIZE = 0x47F0_0000, 0x1_0000
STACK = STACK_BASE + STACK_SIZE - 0x100
RETURN = 0x47FF_FF00                                        # a return address no code uses: the call stops there
REGIONS = [                                                 # (base, size): what the rig maps, besides the stack
    (0x4000_0000, 0x0040_0000),                             # section 3 at 0x40000400
    (0x41B9_0000, 0x0001_0000),                             # BSS: global FX blocks, objects
    (0x43BD_0000, 0x0001_0000),                             # BSS: filter envelopes, kill and reload state
    (0x444D_0000, 0x0002_0000),                             # BSS: LFO state, last pitches
    (0x454E_0000, 0x0009_0000),                             # BSS: voice state, lock bits 0x4550E1CC, glide
    (0x8000_0000, 0x0001_0000),                             # SRAM
]
DATA = REGIONS[1:]
D = [m68k.UC_M68K_REG_D0 + i for i in range(8)]
A = [m68k.UC_M68K_REG_A0 + i for i in range(8)]
SAVED = D[2:8] + A[2:7]                                     # callee-saved: d2-d7, a2-a6
MARK = 0x5A5A_0000

# Callees reached through a register (jsr (a2)), which the recursive descent cannot follow
INDIRECT = {0x400A171A: [0x400A16BC], 0x40118C74: [0x40115EC8],
            0x40119266: [0x40118D8E, 0x40118DA4, 0x40118D70, 0x40118D82, 0x40118DFE, 0x40118DEE]}


def section3() -> bytes:
    return SECTION3.read_bytes()


def reach(blob: bytes, roots: list[int]) -> dict[int, list[str]]:
    """Each routine ROOTS reach, by bsr, jsr and the INDIRECT calls: its cfdis listing."""
    cs = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_040)
    out, todo = {}, list(roots)
    while todo:
        r = todo.pop()
        if r in out or not (LOAD <= r < LOAD + len(blob)):
            continue
        lines, calls = cfdis.routine(blob, r, 4000, cs)
        out[r] = lines
        todo += sorted(calls) + INDIRECT.get(r, [])
    return out


def emac_sites(blob: bytes, roots: list[int]) -> list[int]:
    sites = set()
    for lines in reach(blob, roots).values():
        for line in lines:
            a = int(line[:8], 16)
            if cfdis.is_emac(cfdis.u16(blob, a)) and cfdis.coldfire(blob, a) is None:
                sites.add(a)
    return sorted(sites)


class Fault(RuntimeError):
    pass


@dataclass
class Result:
    d0: int
    d1: int
    reads: dict[int, int]                                   # address: byte before the call
    writes: dict[int, int]                                  # address: byte after the call
    instructions: int
    edges: set[int] = field(default_factory=set)            # (previous pc << 32 | pc) of each step taken


class Rig:
    def __init__(self, roots: list[int], blob: bytes | None = None):
        self.blob = blob or section3()
        uc = self.uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, cpu=m68k.UC_CPU_M68K_ANY)
        for base, size in REGIONS + [(STACK_BASE, STACK_SIZE)]:
            uc.mem_map(base, size)
        uc.mem_write(LOAD, self.blob)
        self.emac = UnicornEmac(uc, emac_sites(self.blob, roots))
        self.image = bytes(uc.mem_read(0x4000_0000, 0x0040_0000))  # DDR's first 4 MB with the EMAC patches
        self._read32 = self.emac.fast.read32
        self.emac.fast.read32 = self._emac_load             # the EMAC's loads go through uc_mem_read, unhooked
        self._dirty: set[int] = set()                        # section-3 bytes a previous call wrote
        self._trace: dict | None = None
        uc.hook_add(UC_HOOK_MEM_READ, self._on_read)
        uc.hook_add(UC_HOOK_MEM_WRITE, self._on_write)
        uc.hook_add(UC_HOOK_MEM_INVALID, self._on_invalid)
        uc.hook_add(UC_HOOK_INTR, self._on_intr)
        uc.hook_add(UC_HOOK_CODE, self._on_code)

    # -- hooks

    def _in_stack(self, a: int) -> bool:
        return STACK_BASE <= a < STACK_BASE + STACK_SIZE

    def _byte(self, a: int) -> int:
        return self.uc.mem_read(a, 1)[0]

    def _note_reads(self, address: int, size: int) -> None:
        t = self._trace
        for a in range(address, address + size):
            if self._in_stack(a) or a in t["reads"]:
                continue
            t["reads"][a] = t["old"][a] if a in t["old"] else self._byte(a)

    def _emac_load(self, addr: int) -> int:
        if self._trace is not None:
            self._note_reads(addr, 4)
        return self._read32(addr)

    def _on_read(self, uc, _access, address, size, _value, _ud):
        if self._trace is not None:
            self._note_reads(address, size)

    def _on_write(self, uc, _access, address, size, _value, _ud):
        t = self._trace
        if t is None:
            return
        for a in range(address, address + size):
            if self._in_stack(a):
                continue
            if a not in t["old"]:
                t["old"][a] = self._byte(a)                 # the hook runs before the write
            t["written"].add(a)

    def _on_invalid(self, uc, access, address, size, _value, _ud):
        if self._trace is not None and self._trace["fault"] is None:
            pc = uc.reg_read(m68k.UC_M68K_REG_PC)
            self._trace["fault"] = f"unmapped access ({access}) at {address:08x}, {size} B, pc {pc:08x}"
        return False

    def _on_intr(self, uc, intno, _ud):
        if intno == 4:                                      # ILLEGAL: an EMAC site, or UnicornEmac reports it
            return
        if self._trace is not None and self._trace["fault"] is None and self.emac.error is None:
            pc = uc.reg_read(m68k.UC_M68K_REG_PC)
            self._trace["fault"] = f"exception {intno} at pc {pc:08x}"
            uc.emu_stop()

    def _on_code(self, uc, address, _size, _ud):
        t = self._trace
        if t is None:
            return
        t["count"] += 1
        if t["edges"] is not None:
            t["edges"].add(t["prev"] << 32 | address)
        t["prev"] = address

    # -- a call

    def _reset(self) -> None:
        for base, size in DATA:
            self.uc.mem_write(base, bytes(size))
        for a in self._dirty:
            self.uc.mem_write(a, self.image[a - 0x4000_0000:a - 0x4000_0000 + 1])
        self._dirty.clear()

    def call(self, routine: int, args: list[int], macsr: int = 0x20, memory: dict[int, bytes] | None = None,
             budget: int = 2_000_000, edges: bool = False, until: int | None = None,
             regs: dict[int, int] | None = None, keep: bool = False) -> Result:
        """UNTIL: run a segment of a routine from ROUTINE to UNTIL instead, with the registers REGS (Unicorn ids)
        and the stack arguments ARGS below whatever the segment pops; the registers are not checked after it.
        KEEP: the memory stays as the last call left it, for a sequence of calls."""
        uc = self.uc
        if not keep:
            self._reset()
        for a, data in (memory or {}).items():
            uc.mem_write(a, bytes(data))
        st = self.emac.state
        st.acc = [0, 0, 0, 0]
        st.load_macsr(macsr)
        self.emac.error = None
        uc.reg_write(m68k.UC_M68K_REG_SR, 0x2700)            # before A7: writing SR switches stacks
        marks = [MARK + i for i in range(16)]
        for r, v in zip(D + A[:7], marks):
            uc.reg_write(r, v)
        sp = STACK - 4 * (len(args) + 1)
        uc.mem_write(sp, struct.pack(f">{len(args) + 1}I", RETURN, *[x & 0xFFFF_FFFF for x in args]))
        uc.reg_write(m68k.UC_M68K_REG_A7, sp)
        for r, v in (regs or {}).items():
            uc.reg_write(r, v & 0xFFFF_FFFF)
        stop = RETURN if until is None else until
        self._trace = t = dict(reads={}, old={}, written=set(), fault=None, count=0, prev=0,
                               edges=set() if edges else None)
        try:
            uc.emu_start(routine, stop, count=budget)
        except UcError as e:
            if t["fault"] is None:
                t["fault"] = f"{e} at pc {uc.reg_read(m68k.UC_M68K_REG_PC):08x}"
        finally:
            self._trace = None
        pc = uc.reg_read(m68k.UC_M68K_REG_PC)
        fault = t["fault"] or (f"EMAC: {self.emac.error}" if self.emac.error else None)
        if fault is None and pc != stop:
            fault = f"no return within {budget} instructions (pc {pc:08x})"
        if fault is None and until is None:
            for i, r in enumerate(D + A[:7]):
                if r in SAVED and uc.reg_read(r) & 0xFFFF_FFFF != marks[i]:
                    fault = f"register {i} not preserved"
            if uc.reg_read(m68k.UC_M68K_REG_A7) != sp + 4:
                fault = "stack pointer not restored"
        writes = {a: self._byte(a) for a in sorted(t["written"])}
        self._dirty |= {a for a in writes if a < 0x4040_0000}
        if fault:
            raise Fault(f"{routine:08x}: {fault}")
        reads = {}
        for a, v in sorted(t["reads"].items()):
            if LOAD <= a < LOAD + len(self.blob) and v == self.blob[a - LOAD]:
                continue
            reads[a] = v
        return Result(uc.reg_read(D[0]) & 0xFFFF_FFFF, uc.reg_read(D[1]) & 0xFFFF_FFFF, reads, writes,
                      t["count"], t["edges"] or set())


def runs(d: dict[int, int]) -> list[tuple[int, bytes]]:
    """A byte map as runs of consecutive addresses."""
    out, start, buf, prev = [], None, bytearray(), None
    for a in sorted(d):
        if prev is not None and a == prev + 1:
            buf.append(d[a])
        else:
            if start is not None:
                out.append((start, bytes(buf)))
            start, buf = a, bytearray([d[a]])
        prev = a
    if start is not None:
        out.append((start, bytes(buf)))
    return out
