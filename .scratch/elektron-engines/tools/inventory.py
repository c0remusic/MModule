"""The routines each engine's update and render reach, and which the port already has.

    python inventory.py SECTION7 PORT_DIR

A routine counts as ported when its address (0x4000xxxx) appears in the port's sources as a comment or a
registry address. Calls through a register (jsr (a0)) are reported, since the graph cannot follow them.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ENGINES = {
    "syTone": (0x4000709c, 0x40007214),
    "syChord": (0x40007770, 0x40007b38),
    "syToy": (0x40008ae8, 0x40008d58),
    "syBits": (0x40008e0c, 0x400091e0),
    "sySwarm": (0x400092ec, 0x400096e8),
    "spTwinshot": (0x400097dc, 0x40009a8a),
}
CALL = re.compile(r"\b(?:jsr|jmp|bsr(?:\.[bwl])?)\s+\$([0-9a-f]{8})")
# A code address loaded into an address register, which a later jsr (aN) calls: the compiler's way to call a
# routine several times. Code ends before the tables at 0x40014000
LOADED = re.compile(r"\b(?:lea\.l|movea\.l)\s+#?\$(4000[0-9a-f]{4})(?:\.l|\(pc\))?,\s*a\d")
INDIRECT = re.compile(r"\bjsr\s+\(")
END = re.compile(r"\b(rts|rte)\b")
TOOLS = Path(__file__).parent


def disasm(section7: str, addr: int) -> str:
    out = subprocess.run([sys.executable, str(TOOLS / "cfdis.py"), section7, f"{addr:08x}", "--max", "4000"],
                         capture_output=True, text=True, stdin=subprocess.DEVNULL)
    return out.stdout


def callees(text: str) -> tuple[set[int], bool]:
    calls, indirect = set(), False
    for line in text.splitlines():
        m = CALL.search(line)
        if m:
            calls.add(int(m.group(1), 16))
        m = LOADED.search(line)
        if m and int(m.group(1), 16) < 0x40014000:
            calls.add(int(m.group(1), 16))
        if INDIRECT.search(line):
            indirect = True
    return calls, indirect


def main() -> int:
    section7, port = sys.argv[1], Path(sys.argv[2])
    # Shared when a shared header names it; else private to the engine files that name it
    ported, private = set(), {}
    for f in port.glob("*.[ch]*"):
        found = {int(a, 16) for a in re.findall(r"0x(4000[0-9a-fA-F]{4})\b", f.read_text(encoding="utf-8"))}
        if f.name in ("stStages.h", "stVoice.h"):
            ported |= found
        else:
            for a in found:
                private.setdefault(a, []).append(f.stem)

    cache: dict[int, tuple[set[int], bool]] = {}

    def reach(root: int) -> dict[int, bool]:
        seen: dict[int, bool] = {}
        todo = [root]
        while todo:
            a = todo.pop()
            if a in seen:
                continue
            if a not in cache:
                cache[a] = callees(disasm(section7, a))
            calls, indirect = cache[a]
            seen[a] = indirect
            todo.extend(calls)
        return seen

    users: dict[int, set[str]] = {}
    for name, roots in ENGINES.items():
        for root in roots:
            for a, indirect in reach(root).items():
                users.setdefault(a, set()).add(name)
                if indirect:
                    print(f"note: {a:08x} ({name}) calls through a register")

    print(f"\n{'routine':10} {'ported':7} engines")
    for a in sorted(users):
        roots = {r for v in ENGINES.values() for r in v}
        tag = "root" if a in roots else ("yes" if a in ported else
                                         "private:" + ",".join(private[a]) if a in private else "NO")
        print(f"{a:08x}   {tag:7} {', '.join(sorted(users[a]))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
