"""What the voice CPU's boot leaves in SRAM beyond the copies of initialized data from section 7.

Prints the SRAM ranges whose words differ between a booted machine and a fresh one where only the two
DDR-to-SRAM copies were made, and saves the booted SRAM for the render tool.
"""

import sys
from pathlib import Path

sys.path.insert(0, r"C:\dev\Synthack-OS\tools\emu")
import engine as eng  # noqa: E402
import memmap  # noqa: E402

SRAM, SIZE = 0x80000000, 0x10000

booted = eng.Engine()
booted.boot()
b = bytes(booted.uc.mem_read(SRAM, SIZE))
Path(r"C:\dev\elektron-work\oracle\boot_sram.bin").write_bytes(b)
print("rng", bytes(booted.uc.mem_read(0x4404F954, 8)).hex())

fresh = eng.Engine()
memmap.copy_data_to_sram(fresh.uc)
f = bytes(fresh.uc.mem_read(SRAM, SIZE))

start = None
for off in range(0, SIZE + 4, 4):
    differ = off < SIZE and b[off:off + 4] != f[off:off + 4]
    if differ and start is None:
        start = off
    elif not differ and start is not None:
        print(f"{SRAM + start:08x}..{SRAM + off:08x} ({(off - start) // 4} words)")
        start = None
