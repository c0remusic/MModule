"""The original's full output of track 1, as the voice CPU sends it to CPU #1 (after the track's stages): what
SyntHack's harness returns from each block, in its `cli.py render` scenario. Compares it with a WAV of the port.

    python full_out.py MACHINE NOTE SECONDS PORT.wav [ORIGINAL.wav]
"""

import math
import struct
import sys
import wave

sys.path.insert(0, r"C:\dev\Synthack-OS\tools\emu")
import cli  # noqa: E402

machine, note, seconds, port_path = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
original = cli.render(machine, note, seconds)

with wave.open(port_path) as w:
    port = list(struct.unpack(f"<{w.getnframes()}i", w.readframes(w.getnframes())))
n = min(len(port), len(original))
differ = [i for i in range(n) if port[i] != original[i]]
peak = max(abs(x) for x in original) or 1
print(f"{n} samples, {len(differ)} differ; original peak {20 * math.log10(peak / 2**31):.1f} dBFS")
if differ:
    i = differ[0]
    print(f"first at sample {i} (block {i // 32}): port {port[i]}, original {original[i]}")
if len(sys.argv) > 5:
    with wave.open(sys.argv[5], "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(4)
        w.setframerate(48000)
        w.writeframes(struct.pack(f"<{len(original)}i", *original))
