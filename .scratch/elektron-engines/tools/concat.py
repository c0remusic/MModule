"""Join the survey WAVs into one file, 0.3 s of silence between them, in engine order."""

import wave
from pathlib import Path

SRC = Path(r"C:\dev\elektron-work\survey")
OUT = SRC / "syntakt_engines_all.wav"

files = sorted(p for p in SRC.glob("[0-9][0-9]_*.wav") if "twinshot" not in p.name)
with wave.open(str(files[0])) as w:
    params = w.getparams()
gap = b"\x00" * int(params.framerate * 0.3) * params.sampwidth * params.nchannels
with wave.open(str(OUT), "wb") as out:
    out.setparams(params)
    for p in files:
        with wave.open(str(p)) as w:
            out.writeframes(w.readframes(w.getnframes()))
        out.writeframes(gap)
print(OUT, [p.stem for p in files])
