"""Prints a vector file's per-block levels (in, outL, outR) and gain in dB, and the compressor's state words.

    python show_vectors.py DIR/CASE   (reads CASE.f32 and CASE.json)
"""
import json
import math
import struct
import sys

base = sys.argv[1]
meta = json.load(open(base + ".json"))
raw = open(base + ".f32", "rb").read()
frames = [struct.unpack_from("<3f", raw, 12 * k) for k in range(len(raw) // 12)]
block = meta["block"]


def db(x):
    return 20 * math.log10(x) if x > 0 else -999.0


every = int(sys.argv[2]) if len(sys.argv) > 2 else 1
for b in range(0, len(frames) // block, every):
    f = frames[b * block:(b + 1) * block]
    rin = math.sqrt(sum(x[0] ** 2 for x in f) / block)
    rl = math.sqrt(sum(x[1] ** 2 for x in f) / block)
    pk = max(abs(x[1]) for x in f)
    s = meta["comp_state_0x54_0x5c_per_block"][b]
    print("%4d in %7.2f  outL %7.2f  peakL %7.2f  gain %7.2f  state %s" % (
        b, db(rin), db(rl), db(pk), db(rl) - db(rin), " ".join("%.4g" % v for v in s)))
