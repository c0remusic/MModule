"""Render each of the Syntakt's 12 digital engines with the original code, one WAV each."""

import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

sys.path.insert(0, r"C:\dev\Synthack-OS\tools\emu")
import cli  # noqa: E402
from wav import write_q31  # noqa: E402

OUT = Path(r"C:\dev\elektron-work\survey")
NAMES = ["BD MODERN", "SD BASIC", "CY ALLOY", "PC CARBON", "SY TONE", "SY CHORD", "SD VINTAGE",
         "CP VINTAGE", "SY TOY", "SY BITS", "SY SWARM", "SP TWINSHOT"]


def one(job):
    engine, machine, note, seconds = job
    samples = cli.render(machine, note, seconds)
    path = OUT / f"{engine:02d}_{NAMES[engine].lower().replace(' ', '_')}.wav"
    peak = write_q31(path, samples, normalize=True)
    return f"{path.name}: peak {peak}"


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    m2e = cli.machine_to_engine()
    jobs = []
    for engine in range(12):
        machine = m2e.index(engine) if engine else 0
        jobs.append((engine, machine, 48 if engine in (0, 1, 6, 7) else 60, float(sys.argv[1])))
    with ProcessPoolExecutor(6) as ex:
        for line in ex.map(one, jobs):
            print(line, flush=True)
