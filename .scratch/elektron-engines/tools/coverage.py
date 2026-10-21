"""What the recorded cases of the shared stages exercise: trigs, stages, drive on, saturation."""

import struct
import sys
from collections import Counter

SRAM, SIZE = 0x80000000, 0x10000


def cases(path):
    with open(path, "rb") as f:
        assert f.read(4) == b"STV3"
        (n,) = struct.unpack("<I", f.read(4))
        for _ in range(n):
            routine, nargs = struct.unpack("<II", f.read(8))
            args = struct.unpack(f"<{nargs}I", f.read(4 * nargs))
            d0, *_rng = struct.unpack("<5I", f.read(20))
            before, after = f.read(SIZE), f.read(SIZE)
            yield routine, args, d0, before, after


def w(img, addr):
    return struct.unpack_from(">i", img, addr - SRAM)[0]


c = Counter()
for routine, args, d0, before, after in cases(sys.argv[1]):
    if routine == 0x40003C0E:
        s = args[0]
        for k in range(args[1]):
            c[f"env stage {w(before, s + 0x80 * k + 0x84)}"] += 1
        c[f"env trig {w(before, s + 0x38)}"] += 1
    elif routine == 0x40003D42:
        s = args[0]
        c[f"amp stage {w(before, s + 0x3DC)}"] += 1
        c[f"amp trig {w(before, s + 0x38)} hold {int(w(before, s + 0x3EC) != 0)} off {w(before, s + 0x3C)}"] += 1
    elif routine == 0x400047EA:
        s, out = args[1], args[0]
        c[f"drive on {w(before, s + 0x48)} stage {w(before, s + 0x438)} trig {w(before, s + 0x38)}"] += 1
        clipped = sum(1 for n in range(32) if abs(w(after, out + 4 * n)) >= 0x7FFFFF00)
        c["drive clipped samples"] += clipped
    elif routine == 0x400045A0:
        s = args[1]
        c[f"vca pending {w(before, s + 0x34)} now {w(before, s + 0x38)}"] += 1
    elif routine == 0x40004DC6:
        c[f"interp index>=127 {int((args[1] >> 8) >= 127)} neg {int(args[1] >= 0x80000000)}"] += 1
for k, v in sorted(c.items()):
    print(f"{v:6d}  {k}")
