"""The original's engine output, captured where the port's ends: voice 1's 32 samples right after 0x40004324
returns, before the track's stages, in SyntHack's `cli.py render` scenario. Compares it with a WAV of the port.

    python engine_out.py MACHINE NOTE SECONDS PORT.wav [ORIGINAL.wav]
"""

import struct
import sys
import wave

sys.path.insert(0, r"C:\dev\Synthack-OS\tools\emu")
import cli  # noqa: E402
import engine as eng  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402

AFTER_VOICES = 0x40000A3A                                   # in the audio interrupt, after jsr 0x40004324

machine, note, seconds, port_path = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
e = eng.Engine()
e.boot()
captured = []
e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: captured.extend(struct.unpack(">32i", uc.mem_read(0x80003840, 128))),
              begin=AFTER_VOICES, end=AFTER_VOICES)
params = cli.make_params(machine, note)
for b in range(int(seconds * 48000 / 32)):
    q = bytearray(params)
    if b in (cli.TRIG_LOAD, cli.TRIG_NOTE):
        q[0x180] = 0x01
    e.render_block(bytes(q))

with wave.open(port_path) as w:
    port = list(struct.unpack(f"<{w.getnframes()}i", w.readframes(w.getnframes())))
n = min(len(port), len(captured))
differ = [i for i in range(n) if port[i] != captured[i]]
peak = max(abs(x) for x in captured) or 1
print(f"{n} samples, {len(differ)} differ; original peak {20 * __import__('math').log10(peak / 2**31):.1f} dBFS")
if differ:
    i = differ[0]
    print(f"first at sample {i} (block {i // 32}): port {port[i]}, original {captured[i]}")
if len(sys.argv) > 5:
    with wave.open(sys.argv[5], "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(4)
        w.setframerate(48000)
        w.writeframes(struct.pack(f"<{len(captured)}i", *captured))
