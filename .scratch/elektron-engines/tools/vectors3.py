"""Record test vectors for the C++ port of CPU #1's routines (section 3, OS 1.41), run alone on oracle3's rig.

    python vectors3.py OUT.stv [--routines 4008ed48,...] [--cases N] [--seed S] [--coverage REPORT.txt]

Each case is one call of one routine on a state drawn for it: random but plausible values for the structures the
routine reads (cpu1.md and the disassembly), a quarter of them at edges. The rig records the bytes the call read and
wrote; the C++ test loads the read set into the port's memory, runs the port, and compares the write set and d0/d1.

File format "ST1V", little-endian integers, bytes in address order as the big-endian CPU sees them:
    "ST1V", u32 count; per case:
        u32 routine, u32 macsr, u32 nargs, nargs x u32 stack arguments (the C convention, first at 4(a7)),
        u32 d0 at return, u32 d1 at return,
        u32 nread runs, per run: u32 address, u32 length, length bytes, the value before the call of each byte read,
        u32 nwrite runs, the same with the value after the call of each byte written.
    The call's own stack frame is in neither set, nor are section 3's bytes read with the file's value.

With --coverage, every instruction step the calls take is noted, and the report gives, for each routine and what it
calls, the instructions executed and every conditional branch of which a side was never taken.
The vectors hold firmware-derived data: keep them out of the repository.
"""

from __future__ import annotations

import argparse
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import oracle3  # noqa: E402

ENGINE = 0x8000_21B0                                        # engine copy, 0x754 bytes (938 words), then settings
SETTINGS = ENGINE + 0x754                                   # 0x80002904, 13 x 124 bytes
MACHINES = 0x8000_2F50                                      # machine id byte per track
STATE = 0x8000_E9B0                                         # smoothing state, a long per engine word
LOCKS = 0x4550_E1CC                                         # lock bits, 3 longs per track
FX_LOCKS = LOCKS + 12 * 12                                  # 0x4550E25C, the FX track's
SCRATCH = 0x8000_FC00                                       # a lock list or a sound the rig places in free SRAM
WORDS = 938

WORD_EDGES = (0, 1, 0xFF, 0x100, 0x3FFF, 0x4000, 0x7EFF, 0x7F00, 0x7F01, 0x7FFF, 0x8000, 0x8001, 0xFFFF)
LONG_EDGES = (0, 1, 0x7FFF_FFFF, 0x8000_0000, 0x8000_0001, 0xFFFF_FFFF, 0x4000_0000, 0xC000_0000, 0x7FFF_8000,
              0x7FFF_7FFF, 0x0000_8000, 0xFFFF_8000)


def word(rng: random.Random, edge: bool) -> int:
    """A parameter word: value << 8 | fine within 0..0x7F00 mostly; an edge, or any 16 bits."""
    if edge and rng.random() < 0.5:
        return rng.choice(WORD_EDGES)
    if rng.random() < 0.8:
        return min(rng.randrange(128) << 8 | rng.randrange(256), 0x7F00)
    return rng.randrange(1 << 16)


def long(rng: random.Random, edge: bool) -> int:
    if edge and rng.random() < 0.5:
        return rng.choice(LONG_EDGES)
    return rng.randrange(1 << 32)


def upper(rng: random.Random, low: int, bits: int) -> int:
    """A stack argument whose low BITS bits are LOW and the rest random half the time: the routine reads the low part."""
    return low | (rng.randrange(1 << (32 - bits)) << bits if rng.random() < 0.5 else 0)


def sparse(rng: random.Random, edge: bool) -> int:
    """Lock bits: none, a few, or any."""
    r = rng.random()
    if r < 0.2:
        return 0
    if r < 0.7:
        v = 0
        for _ in range(rng.randrange(1, 5)):
            v |= 1 << rng.randrange(32)
        return v
    return 0xFFFF_FFFF if edge and rng.random() < 0.5 else rng.randrange(1 << 32)


def be16(values) -> bytes:
    return b"".join(struct.pack(">H", v & 0xFFFF) for v in values)


def be32(values) -> bytes:
    return b"".join(struct.pack(">I", v & 0xFFFF_FFFF) for v in values)


# -- one generator per routine: (args, macsr, memory)

def gen_gain(rng, edge):
    vel = rng.choice((0, 1, 0x7F, 0x80, 0xFF)) if edge and rng.random() < 0.5 else (
        rng.randrange(128) if rng.random() < 0.8 else rng.randrange(256))
    # The velocity is the high byte of the track's velocity word (0x80004670), which the mix passes sign-extended
    vel = vel << 8 | rng.randrange(256)
    args = [upper(rng, word(rng, edge), 16), upper(rng, vel, 16), upper(rng, word(rng, edge), 16), rng.randrange(1 << 32)]
    return args, rng.choice((0x20, 0xA0)), {}


def gen_pan(rng, edge):
    if edge and rng.random() < 0.6:
        v = rng.choice((0, 0xFF, 0x100, 0x101, 0x4000, 0x7E00, 0x7EFF, 0x7F00, 0x7F01, 0x7FFF, 0x8000, 0xFFFF,
                        0xFFFF_FFFF, 0x8000_0000, 0x7FFF_FFFF, 0x8000_00FF, 0x8000_0100))
    elif rng.random() < 0.8:
        v = word(rng, False)
    else:
        v = rng.randrange(1 << 32)
    return [v], 0x20, {}


def gen_sine(rng, edge):
    return [long(rng, edge)], 0x20, {}


def gen_smooth(rng, edge):
    targets = [word(rng, edge) for _ in range(WORDS)]
    states = []
    for t in targets:
        r = rng.random()
        if r < 0.6:                                         # near its target, as after some blocks
            s = (t << 16 if t < 0x8000 else (t - 0x10000) << 16) + rng.randrange(-(1 << 22), 1 << 22)
        elif r < 0.8 or not edge:
            s = rng.randrange(1 << 32)
        else:
            s = rng.choice(LONG_EDGES)
        states.append(s)
    machines = bytes(rng.choice((0x2C, 0x27, 0x2C, 0x27, rng.randrange(0x40), rng.randrange(256)))
                     for _ in range(13))
    mem = {
        ENGINE: be16(targets),
        SETTINGS: bytes(rng.randrange(256) for _ in range(0x64C)),
        MACHINES: machines,
        STATE: be32(states),
        0x8000_6344: be32([rng.randrange(1 << 32)]),
    }
    return [ENGINE], 0x20, mem


def lock_id(rng, edge):
    """An internal id: 0..70 in a track, up to 95 within the three lock words, a few negative ones (-14..-1 keep
    every address in SRAM on track 0)."""
    r = rng.random()
    if edge and r < 0.15:
        return rng.randrange(-14, 0) & 0xFFFF
    if r < 0.8:
        return rng.randrange(71)
    return rng.randrange(96)


def gen_lock(rng, edge):
    t = rng.randrange(13)
    count = rng.choice((0, 1, 2, rng.randrange(13), rng.randrange(40)))
    if edge and rng.random() < 0.2:
        count = rng.choice((0xFFFF_FFFF, 0x8000_0000, 0))
    n = count if count < 0x8000_0000 else 0
    ids = [lock_id(rng, edge and t == 0) for _ in range(n)]
    list_ = bytearray(0x14 + 8 * n)
    struct.pack_into(">I", list_, 8, count)
    for i, ident in enumerate(ids):
        struct.pack_into(">HH", list_, 0x14 + 8 * i, ident, word(rng, edge))
        list_[0x14 + 8 * i + 4:0x14 + 8 * i + 8] = bytes(rng.randrange(256) for _ in range(4))
    mem = {SCRATCH: bytes(list_), LOCKS + 12 * t: be32([sparse(rng, edge) for _ in range(3)])}
    return [t, SCRATCH, ENGINE], 0x20, mem


def gen_restore_fx(rng, edge):
    mem = {FX_LOCKS: be32([sparse(rng, edge) for _ in range(3)]), SCRATCH: be16([word(rng, edge) for _ in range(96)])}
    return [ENGINE, SCRATCH], 0x20, mem


def gen_set(rng, edge):
    idx = rng.choice((0, WORDS - 1, 1000)) if edge and rng.random() < 0.3 else rng.randrange(WORDS)
    return [idx, long(rng, edge), ENGINE], 0x20, {}


def gen_unlock(rng, edge):
    t = rng.randrange(13)
    return [t], 0x20, {LOCKS + 12 * t: be32([sparse(rng, edge) for _ in range(3)])}


def gen_snap_track(rng, edge):
    return [rng.randrange(13), SCRATCH], 0x20, {SCRATCH: be16([word(rng, edge) for _ in range(71)])}


def gen_snap_range(rng, edge):
    first = rng.randrange(71)
    count = rng.choice((0, 1, 71 - first, rng.randrange(72 - first)))
    return [rng.randrange(13), SCRATCH, first, count], 0x20, {SCRATCH: be16([word(rng, edge) for _ in range(count)])}


def gen_restore(rng, edge):
    t = rng.randrange(13)
    mem = {LOCKS + 12 * t: be32([sparse(rng, edge) for _ in range(3)]),
           SCRATCH: be16([word(rng, edge) for _ in range(10 + 96)])}
    return [t, ENGINE, SCRATCH], 0x20, mem


def gen_snap_one(rng, edge):
    idx = rng.choice((0, 70, 95)) if edge and rng.random() < 0.3 else rng.randrange(71)
    return [rng.randrange(13), long(rng, edge), idx], 0x20, {}


SMOOTHED = 0x8000_6348                                      # the smoothed copy, then the settings copy
SETTINGS_COPY = SMOOTHED + 0x754
SOURCES = (0x8000_4746, 0x8000_472C, 0x8000_4712, 0x8000_46F8)   # 13 words each
VELOCITY = 0x8000_4670                                      # 12 words
NOTES = 0x8000_46B8                                         # 13 longs, note << 16


def signed_word(rng, edge):
    if edge and rng.random() < 0.5:
        return rng.choice((0, 1, 0x7F00, 0x7FFF, 0x8000, 0x8001, 0xFFFF, 0xC000, 0x4000, 0x8100))
    return rng.randrange(1 << 16) if rng.random() < 0.5 else (rng.randrange(-0x4000, 0x4000) & 0xFFFF)


def mod_pair(rng, edge):
    """A (depth, internal id) long: ids within the track mostly, a few just outside it (the rig and the port see
    the same SRAM)."""
    r = rng.random()
    if r < 0.15:
        return 0
    ident = rng.randrange(71) if rng.random() < 0.85 or not edge else rng.randrange(-14, 80)
    return signed_word(rng, edge) << 16 | (ident & 0xFFFF)


def mod_state(rng, edge):
    """The smoothed copy and its settings copy with modulation lists, the sources, velocities and notes."""
    params = [word(rng, edge) for _ in range(WORDS)]
    settings = bytearray(rng.randrange(256) for _ in range(0x64C))
    for t in range(13):
        for k in range(24):
            struct.pack_into(">I", settings, 0x7C * t + 4 * k, mod_pair(rng, edge))
    notes = []
    for _ in range(13):
        if edge and rng.random() < 0.3:
            notes.append(rng.choice((0, 0x7F << 16, 0x7FFF << 16, 0xFFFF_FFFF, 0x8000_0000, 60 << 16)))
        else:
            notes.append(rng.randrange(128) << 16 if rng.random() < 0.8 else rng.randrange(1 << 32))
    mem = {SMOOTHED: be16(params), SETTINGS_COPY: bytes(settings), VELOCITY: be16(signed_word(rng, edge)
           for _ in range(12)), NOTES: be32(notes)}
    for s in SOURCES:
        mem[s] = be16(signed_word(rng, edge) for _ in range(13))
    return mem


def gen_mod_matrix(rng, edge):
    return [SMOOTHED], 0x20, mod_state(rng, edge)


def gen_modulate(rng, edge):
    t = rng.randrange(13)
    mem = mod_state(rng, edge)
    return [SMOOTHED + 0x1C + 0x8E * t, signed_word(rng, edge) | (0xFFFF0000 if rng.random() < 0.3 else 0),
            SETTINGS_COPY + 0x7C * t + 0x10 * rng.randrange(6)], 0x20, mem


LFO_STATE = 0x444D_A778                                     # + 0x50 t: LFO2's fields from -0x24 to +1, LFO1's 0x28 below
RANDOM = 0x402D_701C                                        # the generator's two longs, in section 3's data
PERIOD = 0x5265_C000
TEMPOS = (0x3840, 30 * 120, 300 * 120, 90 * 120, 133 * 120, 1, 0, 0x7FFF_FFFF, 0xFFFF_FFFF, 0x8000_0000)


def value_word(rng, value):
    """A parameter word for a byte value: the value in the high byte, a fine part below."""
    return (value & 0xFF) << 8 | rng.randrange(256)


def lfo_params(rng, edge):
    """SPD, MULT, FADE, DEST, WAVE, SPH, MODE, DEP: the eight words from SPD."""
    def pick(common, edges):
        return rng.choice(edges) if edge and rng.random() < 0.3 else rng.choice(common)
    mult = pick(range(24), (-1, 24, 25, 0x7F, -0x80, 11, 12, 23, 0))
    wave = pick(range(7), (-1, 7, 0x7F, -0x80, 6, 0))
    mode = pick(range(5), (-1, 5, 0x7F, -0x80, 2, 3, 4, 0xFD, 0xFE))
    dest = pick(range(71), (0x46, 0x47, -1, 0x7F, -0x80, 1, 9, 0))
    return [word(rng, edge), value_word(rng, mult), word(rng, edge), value_word(rng, dest), value_word(rng, wave),
            word(rng, edge), value_word(rng, mode), word(rng, edge)]


def lfo_fields(rng, edge):
    """An LFO's state, -0x24 to +1: nine longs then the two flags."""
    def phase():
        r = rng.random()
        if r < 0.6:
            return rng.randrange(PERIOD)
        if r < 0.85 or not edge:
            return rng.choice((PERIOD - 1, PERIOD - rng.randrange(1, 1 << 24), PERIOD // 2 - rng.randrange(1, 1 << 24),
                               PERIOD // 2 + rng.randrange(1 << 24), rng.randrange(1 << 24), 0, PERIOD // 2))
        return rng.randrange(1 << 32)
    def fade():
        if edge and rng.random() < 0.3:
            return rng.choice((0, 0x3FFF_FFFF, 0x4000_0000, 0xFFFF_FFFF, 0x7FFF_FFFF, 0x8000_0000, 1))
        return rng.randrange(0x4000_0000)
    def flag():
        r = rng.random()
        return 0 if r < 0.5 else 1 if r < 0.9 or not edge else rng.randrange(256)
    longs = [phase(), long(rng, edge), long(rng, edge), long(rng, edge), fade(), phase(), rng.randrange(1 << 32),
             rng.randrange(1 << 32), rng.randrange(1 << 32)]
    return be32(longs) + bytes([flag(), flag()])


def lfo_state(rng, edge):
    """The smoothed copy with LFO parameters, the 26 LFOs' state and the random generator's."""
    params = [word(rng, edge) for _ in range(WORDS)]
    for t in range(13):
        for j in range(2):
            first = 0x8E * t // 2 + 14 + 1 + 8 * j          # SPD's word: internal id 1 (LFO1) or 9 (LFO2)
            params[first:first + 8] = lfo_params(rng, edge)
    mem = {SMOOTHED: be16(params), RANDOM: be32([rng.randrange(1 << 32), rng.randrange(1 << 32)])}
    for t in range(13):
        for j in range(2):
            mem[LFO_STATE + 0x50 * t - 0x28 * (1 - j) - 0x24] = lfo_fields(rng, edge)
    return mem


def gen_lfo(rng, edge):
    tempo = rng.choice(TEMPOS) if edge and rng.random() < 0.5 else rng.choice((0x3840, rng.randrange(3600, 36001)))
    r = rng.random()
    trigs = 0 if r < 0.3 else rng.randrange(1 << 13) if r < 0.8 else rng.randrange(1 << 32)
    return [SMOOTHED, tempo, trigs], 0x20, lfo_state(rng, edge)


def gen_exp2(rng, edge):
    if edge and rng.random() < 0.5:
        x = rng.choice((0, 0xFF00_0000, 0xFF00_0001, 0x00FF_FFFF, 0x0100_0000, 0x8000_0000, 0x7FFF_FFFF,
                        0xFFFF_FFFF, 0xDF00_0000, 0xE000_0000, rng.randrange(1 << 32)))
    else:
        x = rng.randrange(-(32 << 24), 1 << 24) & 0xFFFF_FFFF
    return [x], 0x20, {}


def gen_fade_step(rng, edge):
    w = rng.choice((0x4000, 0x3FFF, 0x4001, 0, 0x7F00, 0x7FFF, 0x8000, 0xFFFF)) if edge and rng.random() < 0.5 \
        else word(rng, edge)
    return [upper(rng, w, 16)], 0x20, {}


def gen_wave(rng, edge):
    return [long(rng, edge)], 0x20, {}


def gen_random(rng, edge):
    return [], 0x20, {RANDOM: be32([long(rng, edge), long(rng, edge)])}


GLIDE = 0x4556_5328                                         # 8 bytes per track: progress, distance
LAST_PITCH = 0x444D_B6F8                                    # a long per track
HELD, GATES = 0x8000_3154, 0x8000_3152


def pitch_long(rng, edge):
    """A pitch in semitones Q16: a note, a fine pitch, or any long."""
    r = rng.random()
    if edge and r < 0.2:
        return rng.choice((0, 0x7F << 16, 0x7FFF_FFFF, 0x8000_0000, 0xFFFF_FFFF, 0x0000_8000, 0x0000_7FFF, 0xFFFF_8000))
    if r < 0.6:
        return rng.randrange(128) << 16
    if r < 0.9:
        return rng.randrange(128 << 16)
    return rng.randrange(1 << 32)


def porta_state(rng, edge):
    """The smoothed copy with PORT and PTIM, the settings with the glide's fields, the masks, glides and pitches."""
    params = [word(rng, edge) for _ in range(WORDS)]
    settings = bytearray(rng.randrange(256) for _ in range(0x64C))
    for t in range(13):
        base = 0x7C * t
        r = rng.random()
        settings[base + 0x64] = rng.randrange(101) if r < 0.7 else rng.choice((0, 100, 0x7F, 0x80, 0xFF, 0x9C))
        legato = rng.choice((0, 0, 1, 1, 2, 2, rng.randrange(1 << 32)))
        struct.pack_into(">I", settings, base + 0x68, legato)
        for off in (0x6C, 0x70, 0x74):
            struct.pack_into(">I", settings, base + off, rng.choice((0, 0, 1, rng.randrange(1 << 32))))
        settings[base + 0x7A] = rng.randrange(-24, 25) & 0xFF if rng.random() < 0.8 else rng.randrange(256)
        port = rng.choice((0x3FFF, 0x4000, 0x3F00, 0x4100, 0, 0x7F00)) if edge and rng.random() < 0.3 else \
            (rng.randrange(0x4000, 0x7F01) if rng.random() < 0.7 else rng.randrange(0x4000))
        params[71 * t + 14 + 53] = port
        params[71 * t + 14 + 52] = value_word(rng, rng.randrange(128) if rng.random() < 0.85 else rng.randrange(256))
    glides = []
    for _ in range(13):
        r = rng.random()
        progress = 0 if r < 0.15 else 0x8000_0000 if r < 0.3 else (-rng.randrange(1 << 31)) & 0xFFFF_FFFF \
            if r < 0.9 or not edge else rng.randrange(1 << 32)
        distance = (rng.randrange(-48, 49) << 16 | rng.randrange(1 << 16)) & 0xFFFF_FFFF if rng.random() < 0.8 \
            else pitch_long(rng, edge)
        glides += [progress, distance]
    mem = {SMOOTHED: be16(params), SETTINGS_COPY: bytes(settings), GLIDE: be32(glides),
           LAST_PITCH: be32(pitch_long(rng, edge) for _ in range(13)), NOTES: be32(pitch_long(rng, edge)
                                                                            for _ in range(13)),
           GATES: be16([rng.randrange(1 << 16)]), HELD: be16([rng.randrange(1 << 16)]),
           SOURCES[0]: be16(signed_word(rng, edge) for _ in range(13))}
    return mem


def gen_porta(rng, edge):
    t = rng.randrange(12)
    r = rng.random()
    source = t if r < 0.6 else 0xFFFF_FFFF if r < 0.8 else rng.randrange(13)
    trig = rng.choice((0, 1)) if rng.random() < 0.8 else rng.choice((0x100, 0x101, 0xFF, 0xFFFF_FF00))
    return [t, source, trig, pitch_long(rng, edge), SMOOTHED], 0x20, porta_state(rng, edge)


PITCH_FRAME = oracle3.STACK + 0x80                          # the interrupt's a6, above the segment's stack


def gen_pitch_loop(rng, edge):
    trigs = rng.randrange(1 << 12) if rng.random() < 0.8 else rng.randrange(1 << 32)
    mem = porta_state(rng, edge)
    mem[PITCH_FRAME - 0x5C] = be32([trigs])
    return [SMOOTHED, trigs], 0x20, mem


def pitch_loop_regs(args):
    from unicorn import m68k_const as m
    return {m.UC_M68K_REG_A2: args[0], m.UC_M68K_REG_A6: PITCH_FRAME, m.UC_M68K_REG_D6: NOTES}


# Segments of the audio interrupt run alone: start: (end, the registers from the case's arguments). The stack
# arguments are not pushed: the case's arguments are the port's
SEGMENTS = {0x400A_4ADE: (0x400A_4B40, pitch_loop_regs)}


FILTER_ENV = 0x43BD_C09C                                    # 12 bytes per track: stage, value, counter
SECTION3 = oracle3.section3()


def rom_long(address: int) -> int:
    return struct.unpack_from(">i", SECTION3, address - oracle3.LOAD)[0]


def env_value(rng, edge):
    """An envelope value: 0 to INT32_MIN, the ends, or any long."""
    r = rng.random()
    if edge and r < 0.3:
        return rng.choice((0, 0x8000_0000, 0x8000_0001, 0xFFFF_FFFF, 0x7FFF_FFFF, 1, 0xC000_0000))
    if r < 0.9:
        return (-rng.randrange(1 << 31)) & 0xFFFF_FFFF
    return rng.randrange(1 << 32)


def filter_state(rng, edge):
    """The smoothed copy with the filter envelopes' parameters, and the eight envelopes' state."""
    params = [word(rng, edge) for _ in range(WORDS)]
    states = []
    for t in range(8):
        first = 71 * t + 14 + 31                            # DEL, ATK, DEC, SUS, REL, RST: ids 31-36
        dly = rng.choice((0, 0, rng.randrange(1, 0x80))) if rng.random() < 0.6 else rng.randrange(256)
        params[first] = value_word(rng, dly) if dly or rng.random() < 0.5 else 0
        params[first + 5] = rng.choice((0, 0, word(rng, edge)))
        r = rng.random()
        stage = rng.randrange(4) if r < 0.85 or not edge else rng.choice((4, 0xFFFF_FFFF, rng.randrange(1 << 32)))
        r = rng.random()
        threshold = rom_long(0x401D_EAC0 + 4 * ((dly ^ 0x80) - 0x80))
        counter = rng.randrange(0x24000) if r < 0.5 else (threshold - 32 * rng.randrange(0, 3)) & 0xFFFF_FFFF \
            if r < 0.9 or not edge else rng.randrange(1 << 32)
        value = env_value(rng, edge)
        if stage == 2 and rng.random() < 0.3:                # an attack about to end
            value = (-(1 << 31) + rng.randrange(1 << 26)) & 0xFFFF_FFFF
        states += [stage, value, counter]
    return {SMOOTHED: be16(params), FILTER_ENV: be32(states)}


def gen_filter_env(rng, edge):
    def mask():
        r = rng.random()
        return 0 if r < 0.3 else rng.randrange(256) if r < 0.85 else rng.randrange(1 << 32)
    return [SMOOTHED, mask(), mask(), mask()], 0x20, filter_state(rng, edge)


def gen_envelope(rng, edge):
    return [rng.randrange(8)], 0x20, filter_state(rng, edge)


def gen_cutoff(rng, edge):
    t = rng.randrange(8)
    key = rng.randrange(-128, 128) if rng.random() < 0.8 else rng.choice((0x7F, -0x80, 0, 0x40, rng.randrange(1 << 32)))
    mem = filter_state(rng, edge)
    return [SMOOTHED + 0x8E * t + 0x52, key & 0xFFFF_FFFF, pitch_long(rng, edge), t], 0x20, mem


ROUTINES = {
    0x4008ED48: ("gain", gen_gain),
    0x400A171A: ("pan", gen_pan),
    0x400A16BC: ("sine", gen_sine),
    0x40097952: ("smooth", gen_smooth),
    0x400978B4: ("lock", gen_lock),
    0x40097802: ("restoreFx", gen_restore_fx),
    0x40097894: ("set", gen_set),
    0x40097874: ("unlock", gen_unlock),
    0x400976B4: ("snapTrack", gen_snap_track),
    0x400976EE: ("snapRange", gen_snap_range),
    0x40097760: ("restore", gen_restore),
    0x40097732: ("snapOne", gen_snap_one),
    0x40097AAC: ("modulate", gen_modulate),
    0x40097AFC: ("modMatrix", gen_mod_matrix),
    0x40115EC8: ("exp2", gen_exp2),
    0x40118C74: ("fadeStep", gen_fade_step),
    0x40118CF0: ("fadeStart", gen_fade_step),
    0x40118D8E: ("tri", gen_wave),
    0x40118DA4: ("sin", gen_wave),
    0x40118D70: ("sqr", gen_wave),
    0x40118D82: ("saw", gen_wave),
    0x40118DFE: ("exp", gen_wave),
    0x40118DEE: ("rmp", gen_wave),
    0x40118EF8: ("random", gen_random),
    0x40119266: ("lfo", gen_lfo),
    0x40119FE8: ("porta", gen_porta),
    0x400A4ADE: ("pitchLoop", gen_pitch_loop),
    0x4009652C: ("filterEnv", gen_filter_env),
    0x4009664E: ("envelope", gen_envelope),
    0x400956BC: ("cutoff", gen_cutoff),
}


def write_cases(path: Path, cases, magic: bytes = b"ST1V") -> None:
    with open(path, "wb") as f:
        f.write(magic + struct.pack("<I", len(cases)))
        for routine, macsr, args, res in cases:
            f.write(struct.pack(f"<III{len(args)}III", routine, macsr, len(args), *args, res.d0, res.d1))
            for d in (res.reads, res.writes):
                rs = oracle3.runs(d)
                f.write(struct.pack("<I", len(rs)))
                for a, data in rs:
                    f.write(struct.pack("<II", a, len(data)) + data)


# The assert 0x40163334 and what it calls are reached only when MACSR is not 0x20 (0x40118c86), which the audio
# interrupt sets for its whole length (0x400a38c2): the report leaves them out. Branch sides proved dead, by reason
DEAD_ROOTS = [0x4016_3334]
DEAD_SIDES = {
    0x4011_8C86: "fall: MACSR & 0xf0 is 0x20 throughout the audio interrupt (0x400a38c2), so the assert never runs",
    0x4011_A1CA: "taken: d2 is -1 only if btst.l d2,d4 tested bit 31 of a zero-extended word (mvz.w 0x80003154), "
                 "which is clear",
    0x4011_A068: "taken (in the pitch loop only): the loop passes the track, never -1, as the source",
}


def segment_insns(blob: bytes, start: int, end: int, cs) -> list[tuple[int, str, int | None, int | None]]:
    """coverage_insn.routine_insns over the instructions of [START, END)."""
    import cfdis
    import coverage_insn
    out = []
    for ins in cs.disasm(blob[start - oracle3.LOAD:end - oracle3.LOAD], start):
        text = f"{ins.mnemonic} {ins.op_str}"
        target = nxt = None
        if coverage_insn.COND.match(ins.mnemonic):
            target, nxt = cfdis.target(ins.op_str.split(",")[-1]), ins.address + ins.size
        out.append((ins.address, text, target, nxt))
    return out


def coverage(blob: bytes, edges: dict[int, set[int]]) -> list[str]:
    import coverage_insn
    from capstone import CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_040, Cs
    cs = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_040)
    out, summary = [], []
    for root, cover in edges.items():
        executed = {e & 0xFFFF_FFFF for e in cover}
        ti = te = tb = tbs = 0
        lines = []
        dead = set(oracle3.reach(blob, DEAD_ROOTS))
        if root in SEGMENTS:
            seg = segment_insns(blob, root, SEGMENTS[root][0], cs)
            import cfdis
            calls = [c for i in seg if i[1].startswith("jsr") for c in [cfdis.target(i[1].split(" ", 1)[1])] if c]
            parts = [root] + sorted(set(oracle3.reach(blob, calls)) - dead)
        else:
            parts = sorted(set(oracle3.reach(blob, [root])) - dead)
        for r in parts:
            insns = seg if root in SEGMENTS and r == root else coverage_insn.routine_insns(blob, r, cs)
            hit = sum(1 for a, *_ in insns if a in executed)
            nb = nbs = 0
            gaps = []
            for a, text, target, nxt in insns:
                if nxt is None:
                    continue
                nb += 1
                taken = target is not None and (a << 32 | target) in cover
                fall = (a << 32 | nxt) in cover
                nbs += taken + fall
                if not (taken and fall):
                    missing = "both" if not (taken or fall) else ("taken" if not taken else "fall")
                    why = f"  (dead {DEAD_SIDES[a]})" if a in DEAD_SIDES else ""
                    gaps.append(f"      {a:08x}  {text:40} missing {missing}{why}")
            unexec = [a for a, *_ in insns if a not in executed]
            ti, te, tb, tbs = ti + len(insns), te + hit, tb + nb, tbs + nbs
            lines.append(f"  {r:08x}  insns {hit:4}/{len(insns):<4} branch sides {nbs:3}/{2 * nb:<3}"
                         + (f"  unexecuted: {coverage_insn.ranges(unexec, insns)}" if unexec else ""))
            lines.extend(gaps)
        name = ROUTINES[root][0]
        summary.append(f"{root:08x} {name:10} insns {te:4}/{ti:<4} branch sides {tbs:3}/{2 * tb:<3}")
        out.append(f"== {root:08x} {name}")
        out.extend(lines)
    return summary + [""] + out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=Path)
    ap.add_argument("--routines", default=",".join(f"{r:08x}" for r in ROUTINES))
    ap.add_argument("--cases", type=int, default=400)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--coverage", type=Path)
    a = ap.parse_args()
    routines = [int(r, 16) for r in a.routines.split(",")]
    rig = oracle3.Rig(list(ROUTINES))
    cases, edges = [], {}
    for routine in routines:
        name, gen = ROUTINES[routine]
        rng = random.Random(a.seed * 1000003 + routine)
        cover = edges.setdefault(routine, set())
        for i in range(a.cases):
            args, macsr, mem = gen(rng, i % 4 == 0)
            if routine in SEGMENTS:
                end, regs = SEGMENTS[routine]
                res = rig.call(routine, [], macsr, mem, edges=a.coverage is not None, until=end, regs=regs(args))
            else:
                res = rig.call(routine, args, macsr, mem, edges=a.coverage is not None)
            cover |= res.edges
            cases.append((routine, macsr, args, res))
        print(f"{routine:08x} {name:10} {a.cases} cases")
    write_cases(a.out, cases)
    print(f"{len(cases)} cases, {a.out.stat().st_size} B -> {a.out}")
    if a.coverage:
        text = coverage(rig.blob, edges)
        a.coverage.write_text("\n".join(text) + "\n", encoding="utf-8")
        print("\n".join(text[:len(edges)]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
