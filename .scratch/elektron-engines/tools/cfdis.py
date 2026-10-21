"""Disassemble ColdFire code of the Syntakt's section 7, EMAC included.

    python cfdis.py SECTION7 ADDR [--max N] [--follow]

Capstone decodes the 68k instructions; the EMAC ones (line A), which it does not know,
are decoded here from the field layout of the ColdFire Programmer's Reference Manual.
With --follow, every routine reached by bsr/jsr from ADDR is disassembled too.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

from capstone import CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_040, Cs

LOAD = 0x40000400
REG = [f"d{i}" for i in range(8)] + [f"a{i}" for i in range(8)]


def u16(b: bytes, a: int) -> int:
    return struct.unpack_from(">H", b, a - LOAD)[0]


def u32(b: bytes, a: int) -> int:
    return struct.unpack_from(">I", b, a - LOAD)[0]


def is_emac(op: int) -> bool:
    if op & 0xF000 != 0xA000:
        return False
    if op & 0x0100 == 0:
        return True
    return any((op & m) == v for m, v in (
        (0xF9B0, 0xA180), (0xF9FC, 0xA110), (0xF9C0, 0xA100), (0xFFC0, 0xA900),
        (0xFFF0, 0xA980), (0xFFFF, 0xA9C0), (0xFFC0, 0xAD00), (0xFFF0, 0xAD80),
        (0xFBC0, 0xAB00), (0xFBF0, 0xAB80)))


def ea_src(b: bytes, a: int, op: int) -> tuple[str, int]:
    mode, reg = (op >> 3) & 7, op & 7
    if mode in (0, 1):
        return REG[reg | (8 if mode else 0)], 2
    if mode == 7 and reg == 4:
        return f"#0x{u32(b, a + 2):08x}", 6
    return f"<ea {mode}/{reg}>", 2


def ea(b: bytes, a: int, mode: int, reg: int, size: int) -> tuple[str, int]:
    """Effective address text and the extension bytes it uses, read at A."""
    if mode == 0:
        return REG[reg], 0
    if mode == 1:
        return REG[8 + reg], 0
    if mode == 2:
        return f"({REG[8 + reg]})", 0
    if mode == 3:
        return f"({REG[8 + reg]})+", 0
    if mode == 4:
        return f"-({REG[8 + reg]})", 0
    if mode == 5:
        d = u16(b, a)
        return f"{d - 0x10000 if d & 0x8000 else d}({REG[8 + reg]})", 2
    if mode == 6:
        x = u16(b, a)
        d = x & 0xFF
        d = d - 0x100 if d & 0x80 else d
        xi = REG[(x >> 12) & 0xF]
        return f"{d}({REG[8 + reg]},{xi}.{'l' if x & 0x800 else 'w'}*{1 << ((x >> 9) & 3)})", 2
    if reg == 0:
        return f"(0x{u16(b, a):04x}).w", 2
    if reg == 1:
        return f"(0x{u32(b, a):08x}).l", 4
    if reg == 2:
        d = u16(b, a)
        return f"0x{(a + (d - 0x10000 if d & 0x8000 else d)) & 0xFFFFFFFF:08x}(pc)", 2
    if reg == 4:
        if size == 4:
            return f"#0x{u32(b, a):08x}", 4
        return f"#0x{u16(b, a) & (0xFF if size == 1 else 0xFFFF):x}", 2
    return f"<ea 7/{reg}>", 0


def coldfire(b: bytes, a: int) -> tuple[str, int] | None:
    """ColdFire instructions the 68040 decoder does not know (ISA_B/C)."""
    op = u16(b, a)
    if op & 0xF100 == 0x7100:                                # mvs / mvz
        size = 2 if op & 0x40 else 1
        e, n = ea(b, a + 2, (op >> 3) & 7, op & 7, size)
        return f"{'mvz' if op & 0x80 else 'mvs'}.{'w' if size == 2 else 'b'} {e},{REG[(op >> 9) & 7]}", 2 + n
    if op & 0xF1C0 == 0xA140:                                # mov3q
        d = (op >> 9) & 7
        e, n = ea(b, a + 2, (op >> 3) & 7, op & 7, 4)
        return f"mov3q.l #{-1 if d == 0 else d},{e}", 2 + n
    for base, name in ((0x4C80, "sats.l"), (0x04C0, "ff1.l"), (0x00C0, "bitrev.l"), (0x02C0, "byterev.l")):
        if op & 0xFFF8 == base:
            return f"{name} {REG[op & 7]}", 2
    return None


def emac(b: bytes, a: int) -> tuple[str, int]:
    op = u16(b, a)
    if op & 0x0100 == 0:
        ext = u16(b, a + 2)
        name = "msac" if ext & 0x100 else "mac"
        size = ".l" if ext & 0x800 else ".w"
        sf = ["", " <<1", "", " >>1"][(ext >> 9) & 3]
        ux = "u" if ext & 0x80 else "l"
        uy = "u" if ext & 0x40 else "l"
        if op & 0x30 == 0:
            acc = ((op >> 7) & 1) | ((ext >> 3) & 2)
            ry, rx = REG[op & 0xF], REG[((op >> 9) & 7) | (8 if op & 0x40 else 0)]
            return f"{name}{size} {ry}.{uy},{rx}.{ux}{sf},acc{acc}", 4
        acc = (((op >> 7) & 1) ^ 1) | ((ext >> 3) & 2)
        mode, an = (op >> 3) & 7, REG[8 | (op & 7)]
        rw = REG[((op >> 9) & 7) | (8 if op & 0x40 else 0)]
        ry, rx = REG[ext & 0xF], REG[ext >> 12]
        n = 4
        ea = {2: f"({an})", 3: f"({an})+", 4: f"-({an})"}.get(mode, "")
        if mode == 5:
            d = u16(b, a + 4)
            ea, n = f"{d - 0x10000 if d & 0x8000 else d}({an})", 6
        mask = "&" if ext & 0x20 else ""
        return f"{name}{size} {ry}.{uy},{rx}.{ux}{sf},{ea}{mask},{rw},acc{acc}", n
    if (op & 0xF9B0) == 0xA180:
        return f"{'movclr' if op & 0x40 else 'move'}.l acc{(op >> 9) & 3},{REG[op & 0xF]}", 2
    if (op & 0xF9FC) == 0xA110:
        return f"move.l acc{(op >> 9) & 3},acc{op & 3}", 2
    if (op & 0xF9C0) == 0xA100:
        s, n = ea_src(b, a, op)
        return f"move.l {s},acc{(op >> 9) & 3}", n
    if (op & 0xFFC0) == 0xA900:
        s, n = ea_src(b, a, op)
        return f"move.l {s},macsr", n
    if (op & 0xFFF0) == 0xA980:
        return f"move.l macsr,{REG[op & 0xF]}", 2
    if op == 0xA9C0:
        return "move.l macsr,ccr", 2
    if (op & 0xFFC0) == 0xAD00:
        s, n = ea_src(b, a, op)
        return f"move.l {s},mask", n
    if (op & 0xFFF0) == 0xAD80:
        return f"move.l mask,{REG[op & 0xF]}", 2
    if (op & 0xFBC0) == 0xAB00:
        s, n = ea_src(b, a, op)
        return f"move.l {s},accext{'23' if op & 0x400 else '01'}", n
    if (op & 0xFBF0) == 0xAB80:
        return f"move.l accext{'23' if op & 0x400 else '01'},{REG[op & 0xF]}", 2
    return f"dc.w 0x{op:04x} ; emac?", 2


def target(op_str: str) -> int | None:
    t = op_str.strip().lstrip("$#")
    if t.endswith((".l", ".w")):
        t = t[:-2]
    try:
        return int(t, 16)
    except ValueError:
        return None


def routine(b: bytes, start: int, limit: int, cs: Cs) -> tuple[list[str], set[int]]:
    """Recursive descent over the routine's branches; stops at rts, rte, bra and jmp."""
    out: dict[int, str] = {}
    calls: set[int] = set()
    todo, end = [start], LOAD + len(b)
    while todo and len(out) < limit:
        a = todo.pop()
        while LOAD <= a < end and a not in out and len(out) < limit:
            op = u16(b, a)
            if is_emac(op) and coldfire(b, a) is None:
                text, n = emac(b, a)
                out[a] = text
                a += n
                continue
            cf = coldfire(b, a)
            if cf:
                out[a] = cf[0]
                a += cf[1]
                continue
            ins = next(cs.disasm(b[a - LOAD:a - LOAD + 10], a), None)
            if ins is None:
                out[a] = f"dc.w 0x{op:04x}"
                a += 2
                continue
            out[a] = f"{ins.mnemonic} {ins.op_str}"
            # Capstone drops the index scale of a brief extension word when there is a displacement
            if "(" in ins.op_str and "*" not in ins.op_str and re.search(r"\(\$?[\w-]*,? ?[ad]\d\.[wl]\)", ins.op_str) \
                    or re.search(r"\(a\d, [ad]\d\.[wl]\)", ins.op_str):
                for i in range(2, ins.size, 2):
                    w = u16(b, a + i)
                    if w & 0x0100 == 0 and (w >> 9) & 3:
                        out[a] += f"   ; index *{1 << ((w >> 9) & 3)}"
                        break
            m, t = ins.mnemonic, target(ins.op_str)
            a += ins.size
            if m.startswith(("bsr", "jsr")):
                if t is not None:
                    calls.add(t)
            elif m.startswith("jmp"):
                if t is not None:
                    calls.add(t)
                    out[ins.address] += "   ; tail call"
                break
            elif m.startswith("bra"):
                if t is not None:
                    todo.append(t)
                break
            elif m.startswith(("b", "db")) and t is not None:
                todo.append(t)
            elif m in ("rts", "rte"):
                break
    return [f"{k:08x}  {v}" for k, v in sorted(out.items())], calls


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("section7", type=Path)
    ap.add_argument("addr", type=lambda s: int(s, 16))
    ap.add_argument("--max", type=int, default=2000)
    ap.add_argument("--follow", action="store_true")
    ap.add_argument("--table", type=int, default=0, help="read ADDR as a table of N pointers")
    a = ap.parse_args()
    b = a.section7.read_bytes()
    if a.table:
        for i in range(a.table):
            print(f"[{i}] {u32(b, a.addr + 4 * i):08x}")
        return 0
    cs = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_040)
    todo, seen = [a.addr], set()
    while todo:
        r = todo.pop(0)
        if r in seen or not (LOAD <= r < LOAD + len(b)):
            continue
        seen.add(r)
        lines, calls = routine(b, r, a.max, cs)
        print(f"; ---- {r:08x} ({len(lines)} instructions)")
        print("\n".join(lines))
        if a.follow:
            todo += sorted(calls)
    return 0


if __name__ == "__main__":
    sys.exit(main())
