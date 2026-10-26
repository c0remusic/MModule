"""Finds the lag (samples) that best aligns a vector's outL with its input, and the gain at that lag.

    python lag.py DIR/CASE [skip_samples]
"""
import math
import struct
import sys

raw = open(sys.argv[1] + ".f32", "rb").read()
skip = int(sys.argv[2]) if len(sys.argv) > 2 else 128
fr = [struct.unpack_from("<3f", raw, 12 * k) for k in range(len(raw) // 12)]
x = [f[0] for f in fr]
y = [f[1] for f in fr]
best = None
for lag in range(0, 200):
    num = sum(y[k] * x[k - lag] for k in range(skip + lag, len(x)))
    den = sum(x[k - lag] ** 2 for k in range(skip + lag, len(x)))
    err = sum((y[k] - num / den * x[k - lag]) ** 2 for k in range(skip + lag, len(x)))
    if best is None or err < best[0]:
        best = (err, lag, num / den)
err, lag, g = best
print("lag %d samples, gain %.5f (%.2f dB), residual rms %.3g" % (
    lag, g, 20 * math.log10(abs(g)), math.sqrt(err / (len(x) - skip - lag))))
