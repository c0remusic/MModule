"""Writes a synthetic sample set for SP TWINSHOT (engine 11) to references/twinshot_samples.bin.

    python twinshot_samples.py [OUT]

The voice CPU reads the samples CPU #1 loads from its DDR: a directory at 0x4404f754 of 64 entries (u32 length in
samples, u32 pointer to the first sample), and the samples, signed 16-bit big-endian, where the pointers point. The
file holds both as the firmware lays them, every field big-endian:

    offset 0     "STS1"
    offset 4     512 B   the directory, 64 x (u32 length, u32 pointer), written at 0x4404f754
    offset 516   u32     BASE, where the data go in DDR
    offset 520   u32     SIZE of the data in bytes
    offset 524   SIZE B  the data, written at BASE

The harness (vectors.py --samples) writes the directory and the data into the emulated memory after the boot; the
port (stEngineOracleTest, SYNTAKT_SAMPLES) reads the same file. Everything else in DDR outside section 7 stays 0 in
both, which is what a player reads behind or beyond its sample.

The set: waveforms placed one after the other at BASE (1, 2 and 3 samples, a ramp, a sine, noise, a decaying sine, a
pulse, a short loopable sine), each entry pointing at one, some at an offset into it with a shorter length, so that
the 64 entries differ. Entry 7 is empty (length 0), entry 9 has a length and a null pointer, entry 10 a length beyond
the firmware's 0x7ffff clamp. Entry 63, which every negative byte reaches (reversed), is the short loopable sine.
"""

import math
import random
import struct
import sys
from pathlib import Path

DIRECTORY = 0x4404F754
BASE = 0x42000000
OUT = Path(r"C:\dev\elektron-work\references\twinshot_samples.bin")


def waveforms():
    rnd = random.Random(1141)
    w = {}
    w["one"] = [20000]
    w["two"] = [-30000, 30000]
    w["three"] = [12000, -25000, 7000]
    w["ramp"] = [int(-32768 + 65535 * i / 2999) for i in range(3000)]
    w["sine"] = [int(32000 * math.sin(2 * math.pi * i / 37)) for i in range(1200)]
    w["noise"] = [rnd.randrange(-32768, 32768) for _ in range(9000)]
    w["decay"] = [int(32767 * math.exp(-i / 1500) * math.sin(2 * math.pi * i * (60 + 200 * math.exp(-i / 300)) / 48000))
                  for i in range(6000)]
    w["pulse"] = [24000 if (i // 25) % 2 else -24000 for i in range(500)]
    w["loop"] = [int(30000 * math.sin(2 * math.pi * i / 70) + 2000 * math.sin(2 * math.pi * i / 7)) for i in range(700)]
    return w


def main() -> int:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else OUT
    data = bytearray()
    where = {}
    for name, s in waveforms().items():
        where[name] = (BASE + len(data), len(s))
        data += struct.pack(f">{len(s)}h", *s)

    kinds = ["one", "ramp", "two", "sine", "three", "noise", "decay", "pulse"]
    entries = []
    for i in range(64):
        name = kinds[i % 8]
        start, length = where[name]
        cut = min(length - 1, (i // 8) * 7)                 # later entries start further into the waveform
        entries.append((length - cut, start + 2 * cut))
    entries[7] = (0, 0x4004F754 + 4 * 7)                    # empty, as the boot leaves it
    entries[9] = (500, 0)                                   # a length and no data
    entries[10] = (0x100000, where["ramp"][0])              # beyond the clamp: reads past the ramp find 0
    entries[62] = (257, where["ramp"][0] + 2 * 1000)
    entries[63] = (where["loop"][1], where["loop"][0])

    blob = b"STS1" + b"".join(struct.pack(">II", n, p) for n, p in entries)
    blob += struct.pack(">II", BASE, len(data)) + bytes(data)
    out.write_bytes(blob)
    print(f"{out}: {len(entries)} entries, {len(data)} B of data at {BASE:08x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
