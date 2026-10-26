"""Digitakt II OS 1.16 master oracle: runs the DSP's master routine 0x1c207b on digikit's concrete SHARC+ core.

The routine runs in isolation, block after block (32 samples, stereo planar), with the memory it reads set up here:
the input goes into Track 1's buffer, the settings into the float words the frame unpacker (0x1c2b24) writes, and
everything else (pointer tables, lookup tables) comes from the image's own static data. Nothing of the firmware is
in this file: the image is read from digikit's sections (out/sections/dt2-1.16/).

Run with digikit's environment (no network, no new packages):

    C:\\dev\\elektron-work\\scratch-arch\\ext\\digikit\\.venv\\Scripts\\python.exe dt2_master_oracle.py --out DIR

See the research note 03-oracle-dt2.md of the elektron-drums-v1 effort for the layout and the file format.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import random
import struct
import sys
import time

DIGIKIT = os.environ.get("DIGIKIT", r"C:\dev\elektron-work\scratch-arch\ext\digikit")

MASTER = 0x1C207B  # short-word PC of the master routine
RETURN_SW = 0xB9F100  # a PC outside the image: the breakpoint the routine returns to
BLOCK = 32
RATE = 48000

# Byte addresses (L1 block 0) the master reads, as the orchestrator 0x1c2b24 writes them each block.
TRACK_BUF = 0x252DF8  # 16 Tracks x 64 words: L[32] then R[32], float
TRACK_STRIDE = 0x100
PARAMS = {
    # name: (address, kind). Compressor block 0x252518 (R8 of 0xb82d41): floats, a knob / 127 (s16 / 32512).
    "comp_scfilter": (0x252518, "f"),  # x127 -> table 0x2ca038, sidechain filter coefficients [INFER: SC filter]
    "comp_threshold": (0x25251C, "f"),  # clamp 0..1, x60 - 60 = threshold in dB (0xb82994)
    "comp_ratio": (0x252520, "f"),  # k/7, x7 -> table 0x2c9a18 (1/ratio: 2/3 ... 1/20)
    "comp_attack": (0x252524, "f"),  # x127 -> table 0x2c9a38, one-pole smoothing [INFER: attack]
    "comp_release": (0x252528, "f"),  # x127 -> table 0x2c9c38, peak-hold release [INFER: release]
    "comp_makeup": (0x25252C, "f"),  # 0xb82b09 [INFER: makeup gain]
    "comp_mix": (0x252530, "f"),  # x127 -> table 0x2c9e38 at k and 127-k: dry/wet crossfade [INFER: mix]
    "comp_sc_source": (0x252534, "i"),  # 0: own input (pointer table 0x256818); 1: bus 0x254a78; 2-17 Tracks; 18+ FX
    "comp_tracks": (0x252538, "i"),  # bit t: Track t goes through the compressor, else straight to the clip
    "fx_routing": (0x25253C, "i"),  # bits 0-2: which bus each FX return joins
    "track_mask": (0x252728, "i"),  # bit t set: Track t not mixed; bits 16, 17: FX returns not mixed
    "bw_flag": (0x252718, "i"),
    "drive": (0x2526E8, "f"),  # overdrive: 8*d clamped to 1 (mix), 0.8465*d^2 + 0.1534 (gain), saturator 0x1cb3d8
    "volume": (0x2526EC, "f"),  # squared, ramped over the block (state 0x2558c4)
}
COMP_STRUCT = 0x252AE0  # compressor state (R4 of 0xb82d41)

# Stand-ins in L2 (byte addresses), as digikit's own rigs place them (tests/support/dsp_voice_rig.py).
OUT = 0x20098000  # R4: the master's output, L[32] then R[32]
STACK = 0x2009C000
CONTEXT = {"M5": 0, "M6": 1, "M7": -1, "M13": 0, "M14": 1, "M15": -1, "MODE1": 0x1011800,
           **{f"L{n}": 0 for n in range(16)}, **{f"B{n}": 0 for n in range(16)}}

DEFAULTS = {
    "comp_scfilter": 0.0, "comp_threshold": 1.0, "comp_ratio": 1 / 7, "comp_attack": 0.0,
    "comp_release": 0.5, "comp_makeup": 0.0, "comp_mix": 1.0, "comp_sc_source": 0, "comp_tracks": 0xFFFF,
    "fx_routing": 0, "track_mask": 0x30000, "bw_flag": 0, "drive": 0.0, "volume": 1.0,
}


def fbits(x: float) -> int:
    return struct.unpack("<I", struct.pack("<f", x))[0]


def bitsf(x: int) -> float:
    return struct.unpack("<f", struct.pack("<I", x & 0xFFFFFFFF))[0]


INIT = 0x1C15E3  # the audio graph's init (called once by 0x1c7ff9): context 0x252d2c-0x252d3c, effect states
RATIO_TABLE = 0x2C9A18  # 8 floats, read by 0xb82994 at trunc(clamp(comp_ratio * 7, 0, 7))


def digikit_commit() -> str:
    """digikit's HEAD, read from its .git without running git."""
    git = os.path.join(DIGIKIT, ".git")
    head = open(os.path.join(git, "HEAD")).read().strip()
    if not head.startswith("ref: "):
        return head
    ref = head[5:]
    path = os.path.join(git, *ref.split("/"))
    if os.path.exists(path):
        return open(path).read().strip()
    for line in open(os.path.join(git, "packed-refs")):
        if line.strip().endswith(ref):
            return line.split()[0]
    raise RuntimeError("cannot read digikit's commit")


class Master:
    def __init__(self, params: dict, snapshot: str) -> None:
        """Starts from the state right after the firmware's own init 0x1c15e3, run once and kept in SNAPSHOT."""
        sys.path.insert(0, DIGIKIT)
        cwd = os.getcwd()
        os.chdir(DIGIKIT)
        try:
            from tools.sharc_dev import sharc_run
            from tools.sharc_dev import sharc_trace as st

            from tools.sharc_dev import sharc, sharcfn

            img = sharc.load("dt2-1.16")
            self.mem = img._mem()
            blob_sha = sharcfn.sha256_of(img.blob_path)
        finally:
            os.chdir(cwd)
        self.sr, self.st = sharc_run, st
        self.params = dict(DEFAULTS, **params)
        # What made the snapshot: a snapshot made from another image, digikit or init (or the harness's stack and
        # context registers) is not reused.
        record = {"dsp_section_sha256": blob_sha, "digikit_commit": digikit_commit(), "init_sw": "%#x" % INIT,
                  "regs": {k: v for k, v in sorted(self.regs().items())}, "approx_recips": True}
        record_path = snapshot + ".json"
        reason = None
        if not os.path.exists(snapshot):
            reason = "no snapshot"
        elif not os.path.exists(record_path):
            reason = "no record beside the snapshot"
        else:
            old = json.load(open(record_path))
            diff = sorted(k for k in set(old) | set(record) if old.get(k) != record.get(k))
            if diff:
                reason = "record differs: " + ", ".join("%s %r -> %r" % (k, old.get(k), record.get(k)) for k in diff)
        if reason is None:
            self.runner = sharc_run.load_snapshot(snapshot, self.mem)
        else:
            print("rebuilding the init snapshot (%s)" % reason, flush=True)
            self.runner = sharc_run.Runner(self.mem, INIT, regs=self.regs(), breakpoints=[RETURN_SW],
                                           approx_recips=True)
            self.runner.state.call_stack.append(RETURN_SW)
            self.call()
            sharc_run.save_snapshot(self.runner, snapshot)
            with open(record_path, "w") as f:
                json.dump(record, f, indent=1)
            print("init 0x1c15e3: %d instructions, %.1f s, snapshot %s" % (
                self.runner.instructions, self.seconds, snapshot), flush=True)
        self.instructions = 0
        self.seconds = 0.0
        self.ratio_reads: list[tuple[int, int]] = []

    def call(self) -> None:
        """Runs the Runner's call to its return to RETURN_SW, as the caller's CALL would have set the frame."""
        self.w(STACK - 4, RETURN_SW - 1)  # [I6 - 1]: the return address the caller's CALL pushed
        self.w(STACK, 0x20000FF0)  # [I6]: the caller's frame pointer
        t0 = time.perf_counter()
        res = self.runner.run(4_000_000)
        self.seconds = getattr(self, "seconds", 0.0) + time.perf_counter() - t0
        if res.halt.reason != "breakpoint" or res.final_pc_sw != RETURN_SW:
            raise RuntimeError("call did not return: %s" % res.halt)
        self.last_instructions = res.instructions

    def regs(self) -> dict:
        return {**CONTEXT, "R4": OUT, "I6": STACK, "I7": STACK}

    def w(self, addr: int, value: int) -> None:
        if not self.st._dm_write(self.runner.state, addr, 4, self.st.Const(value & 0xFFFFFFFF)):
            raise RuntimeError("cannot write %#x" % addr)

    def r(self, addr: int) -> int:
        v = self.st._dm_read(self.runner.state, addr, 4)
        if v is None:
            raise RuntimeError("unknown word at %#x" % addr)
        return v.value & 0xFFFFFFFF

    def set_params(self) -> None:
        for name, value in self.params.items():
            addr, kind = PARAMS[name]
            self.w(addr, fbits(value) if kind == "f" else int(value))

    def block(self, left: list[float], right: list[float]) -> tuple[list[float], list[float], list[float]]:
        """One call of 0x1c207b: Track 1 gets the input, the rest are silent. Returns L, R and the compressor's state
        words 0x54-0x5c after the call."""
        first = self.instructions == 0
        self.runner = self.runner.fresh_call(MASTER, regs=self.regs(), return_address=RETURN_SW)
        if first:  # which entry of the ratio table 0xb82994 reads, logged on the first block only
            self.runner.attach_watchpoints([self.sr.Watchpoint(RATIO_TABLE, RATIO_TABLE + 32, on_read=True,
                                                               on_write=False, stop=False, label="ratio")])
        self.set_params()
        for k in range(BLOCK):
            self.w(TRACK_BUF + 4 * k, fbits(left[k]))
            self.w(TRACK_BUF + 4 * (BLOCK + k), fbits(right[k]))
        self.call()
        if first:
            # The log gives canonical byte addresses (the loader's 0x28000000 alias of L1) one byte per event:
            # gather them back into the words read.
            words: dict[int, int] = {}
            for e in self.runner.watch_log:
                if e.access != "read":
                    continue
                for k in range(e.width):
                    a = e.address + k
                    a -= 0x28000000 if a >= 0x28000000 else 0
                    words[a & ~3] = words.get(a & ~3, 0) | (((e.new_value >> (8 * k)) & 0xFF) << (8 * (a & 3)))
            self.ratio_reads = sorted(words.items())
        self.instructions += self.last_instructions
        outl = [bitsf(self.r(OUT + 4 * k)) for k in range(BLOCK)]
        outr = [bitsf(self.r(OUT + 4 * (BLOCK + k))) for k in range(BLOCK)]
        state = [bitsf(self.r(COMP_STRUCT + 4 * k)) for k in range(0x54, 0x5D)]
        return outl, outr, state


def sine(n: int, freq: float = 220.0, amp: float = 0.25) -> list[float]:
    return [amp * math.sin(2 * math.pi * freq * k / RATE) for k in range(n)]


def hit(n: int, amp: float = 0.5) -> list[float]:
    """A synthetic kick-like Hit, generated here: a pitch-swept sine with an exponential decay and a click of noise."""
    rng = random.Random(1)
    out, phase = [], 0.0
    for k in range(n):
        t = k / RATE
        phase += 2 * math.pi * (50.0 + 150.0 * math.exp(-t / 0.03)) / RATE
        click = (rng.random() * 2 - 1) * math.exp(-t / 0.002) * 0.5
        out.append(amp * (math.sin(phase) * math.exp(-t / 0.15) + click))
    return out


def render(signal: list[float], params: dict, snapshot: str) -> dict:
    """SIGNAL (settle silence included) through the master, the same on L and R, a block per call."""
    m = Master(params, snapshot)
    left, right, states = [], [], []
    for b in range(0, len(signal), BLOCK):
        l, r, s = m.block(signal[b:b + BLOCK], signal[b:b + BLOCK])
        left += l
        right += r
        states.append(s)
    return {"left": left, "right": right, "state": states, "instructions": m.instructions, "seconds": m.seconds,
            "ratio_reads": m.ratio_reads}


def stats(x: list[float]) -> tuple[float, float]:
    rms = math.sqrt(sum(v * v for v in x) / len(x))
    return rms, max(abs(v) for v in x)


def db(x: float) -> float:
    return 20 * math.log10(x) if x > 0 else -999.0


def write_case(out_dir: str, name: str, signal: list[float], settle: int, params: dict, res: dict) -> None:
    """<name>.f32: float32 little-endian frames of three columns (in, outL, outR), settle blocks included;
    <name>.json: the settings, the levels over the signal (after the settle blocks) and the compressor's words."""
    n = len(res["left"])
    with open(os.path.join(out_dir, name + ".f32"), "wb") as f:
        for k in range(n):
            f.write(struct.pack("<3f", signal[k], res["left"][k], res["right"][k]))
    s0 = settle * BLOCK
    rms, peak = stats(res["left"][s0:])
    meta = {
        "case": name, "rate": RATE, "block": BLOCK, "frames": n, "settle_blocks": settle,
        "columns": ["in", "outL", "outR"], "params": dict(DEFAULTS, **params),
        "rms_db": db(rms), "peak_db": db(peak), "in_rms_db": db(stats(signal[s0:])[0]),
        "ratio_table_reads": [{"address": "%#x" % a, "index": (a - RATIO_TABLE) // 4, "value": bitsf(v), "ratio": 1 / bitsf(v)} for a, v in res["ratio_reads"]],
        "in_peak_db": db(stats(signal[s0:])[1]),
        "comp_state_0x54_0x5c_per_block": res["state"],
        "instructions": res["instructions"], "emulation_seconds": res["seconds"],
        "samples_per_second": n / res["seconds"],
    }
    with open(os.path.join(out_dir, name + ".json"), "w") as f:
        json.dump(meta, f, indent=1)
    print("%-22s rms %7.2f dB  peak %7.2f dB  ratio %s  (%d instr, %.1f s, %.0f samples/s)" % (
        name, db(rms), db(peak), ",".join("%.4g:1" % r["ratio"] for r in meta["ratio_table_reads"]), res["instructions"], res["seconds"], n / res["seconds"]), flush=True)


# The compressor at 16:1 (comp_ratio 6/7) and three thresholds (0, -30, -60 dB); the overdrive at three drives.
CASES = {
    # name: (signal, blocks of signal, params)
    "hit_thr_100": ("hit", 150, {"comp_threshold": 1.0, "comp_ratio": 6 / 7}),
    "hit_thr_050": ("hit", 150, {"comp_threshold": 0.5, "comp_ratio": 6 / 7}),
    "hit_thr_000": ("hit", 150, {"comp_threshold": 0.0, "comp_ratio": 6 / 7}),
    "hit_thr_050_drive_050": ("hit", 150, {"comp_threshold": 0.5, "comp_ratio": 6 / 7, "drive": 0.5}),
    "sine_thr_100": ("sine", 60, {"comp_threshold": 1.0, "comp_ratio": 6 / 7}),
    "sine_thr_050": ("sine", 60, {"comp_threshold": 0.5, "comp_ratio": 6 / 7}),
    "sine_thr_000": ("sine", 60, {"comp_threshold": 0.0, "comp_ratio": 6 / 7}),
    "sine_drive_000": ("sine", 60, {"drive": 0.0}),
    "sine_drive_050": ("sine", 60, {"drive": 0.5}),
    "sine_drive_100": ("sine", 60, {"drive": 1.0}),
}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--out", required=True, help="directory for the vectors (outside the repository)")
    p.add_argument("--settle", type=int, default=48, help="blocks of silence first, for the smoothed settings")
    p.add_argument("--case", action="append", default=[], help="a case of CASES (repeatable; default all)")
    a = p.parse_args()
    os.makedirs(a.out, exist_ok=True)
    snapshot = os.path.join(a.out, "init-0x1c15e3.snap")
    for name in a.case or CASES:
        kind, blocks, params = CASES[name]
        signal = [0.0] * (a.settle * BLOCK) + {"sine": sine, "hit": hit}[kind](blocks * BLOCK)
        write_case(a.out, name, signal, a.settle, params, render(signal, params, snapshot))
    return 0


if __name__ == "__main__":
    sys.exit(main())
