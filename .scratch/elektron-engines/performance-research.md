# Making the bit-exact Syntakt port faster on x86-64

Research for `source/elektron/syntakt/stEngine/` (2026-10-23). Baseline: 60 us per 32-sample block (CPU #1 control
7.6 us, voice CPU 24 us, mix/delay/reverb 28 us). Profile: sumChannels 11 %, track::filter 6.6 %, reverb 5.2 %,
eDMA model 4.3 %, mixOutput 3.6 %, smooth 3.5 %, wave lookup 3.3 %, feed 3.3 %, memmove 2.6 %, delay 2.4 %,
rampBuffer 2.2 %, drive 2.0 %, pmOscillator 2.0 %. Guesses are marked [INFER].

## 1. Saturating fixed point, bit-exact, in SIMD

- **ITU-T STL / ETSI basic operators.** `L_mac(acc, a, b) = L_add(acc, L_mult(a, b))`. Every step saturates to 32
  bits and sets a global `Overflow` flag. The 3GPP reference is `basop32.c`/`.h`
  ([3GPP EVS C code](https://3gpp.org/ftp/tsg_sa/WG4_CODEC/EVS_Testing/CR26443-0006-ANSI-C_source_code/c-code/lib_com/basop32.c)).
  3GPP TR 26.973 (the basic-operator update for IVAS) says outright that the STL2009 operators "require saturation and
  truncation after every MAC" and that "the current Basic operator saturation checks prevent use of SIMD
  parallelism". Its fix is wider accumulators with guard bits, saturating once at the end of a loop
  ([TR 26.973 §4.1](https://www.itecspec.com/3gpp/26.973/s/4.1)). The 64-bit operators (`W_mac_32_16`,
  `W_add_nosat`, then `W_sat_l` once) work the same way
  ([TR 26.973 Annex A.1](https://itecspec.com/3gpp/26.973/s/a.1)). An early G.729 contribution makes the same point
  about 40-bit DSP accumulators ([3GPP S4-000118](https://3gpp.org/FTP/tsg_sa/WG4_CODEC/TSGS4_10/Docs/PDF/s4-000118.pdf)).
- **What this means for EMAC.** Intermediate saturation is the barrier, not the products. Integer addition that
  cannot overflow is associative and commutative, so once a bound proves that no partial sum reaches the limit, the
  terms can be summed in any order, in any lane, and the result stays exact. The port's `setSum` fast paths already
  use this argument. Vectorizing generalizes it: prove the bound per block, or per lane, then reorder freely.
  Fractional mode gives each product at most 2^39 in magnitude, so a clear accumulator survives at least 255
  products. Note: `setSum`'s comments say "at most 256", but 256 x 2^39 = 2^47 trips `r >= limit` (only
  INT32_MIN x INT32_MIN products, positive, reach it). Every caller uses 3 to 10 products, so it is harmless, but
  the stated bound should be 255.
- **I found no published SIMD code for G.729, AMR or EVS on x86 that keeps STL overflow semantics.** Optimized
  codecs (vendor DSP libraries) rely on guard bits, as above. What the sources show instead is the standards body
  changing the operators so that SIMD becomes possible.
- **x86 instructions.** `_mm_mul_epi32` (SSE4.1) and `_mm256_mul_epi32` (AVX2) are `(V)PMULDQ`: signed 32x32->64 on
  the even dwords, 2 or 4 products per instruction ([felixcloutier PMULDQ](https://felixcloutier.com/x86/pmuldq),
  [Intel intrinsic](https://intel.com/content/www/us/en/docs/cpp-compiler/developer-guide-reference/2021-9/mm256-mul-epi32.html)).
  `VPMULLQ` (AVX-512DQ) keeps only the low 64 bits of a 64x64 product, which is not what EMAC needs
  ([felixcloutier PMULLQ](https://felixcloutier.com/x86/pmulld:pmullq)). x86 has saturating adds only for 8 and 16
  bits, so 64->32 saturation is `vpcmpgtq` plus a blend against INT32_MAX/MIN.
- **The AVX2 gap that matters here:** AVX2 has no 64-bit arithmetic right shift (`vpsraq` is AVX-512F only).
  `(y*x) >> 23` therefore becomes a logical shift plus a sign fix-up: `srli(p,23) | slli(cmpgt(0,p), 41)`, three
  extra µops. Another option is to shift right by 23 on the high and low dwords separately with `vpsrad`/`vpsrld`
  and recombine them. `acc >> 8` on store has the same cost.
- **What AVX-512 adds:** `vpsraq`, mask registers (per-lane overflow fallback without blends), 8 x 64-bit lanes
  (all 8 tracks in one register) and `vpmovsqd` (64->32 signed saturating narrow, which is exactly EMAC's
  saturating store).

**Which instruction set a plug-in can assume.** Steam survey, September 2026
([store.steampowered.com/hwsurvey](https://store.steampowered.com/hwsurvey/)): SSE4.1 98.24 %, AVX2 95.95 %,
BMI2 95.91 %, AVX512F 27.46 %. Adobe Audition 25 requires AVX2
([Adobe](https://helpx.adobe.com/au/audition/desktop/system-requirements/2025.html)), as does the EqualizerAPO64 build
([release notes](https://newreleases.io/project/github/TheFireKahuna/equalizerAPO64/release/1.4.2_2)). I did not find
the plug-in vendors' own minimums. **Recommendation: AVX2 kernels behind a runtime CPUID check, with today's scalar
code as the fallback.** No third path. AVX-512 is on a quarter of machines and not worth a third bit-exact path to
maintain.

## 2. Data layout for parallel voices

- **Recursion blocks vectorizing over samples. Vectorize across independent instances instead.** The JUCE
  `SIMDRegister` tutorial runs one mono processor over several channels, one channel per lane
  ([JUCE tutorial](https://docs.juce.com/master/tutorial_simd_register_optimisation.html);
  [forum: 12 channels on 4-wide registers](https://forum.juce.com/t/simd-register-tutorial-multichannel/53326)). Work
  on multichannel IIR filtering on ARM reports hand-written SIMD about 2x faster than auto-vectorized code
  ([Alonso et al., J. Supercomputing 2016](https://riunet.upv.es:443/handle/10251/84797)).
- **Block or matrix forms of IIR filters** (Burrus; Sung & Mitra; incremental block-state) compute several outputs
  per step by expanding the recursion into matrix powers
  ([TU/e, linear speedup on a vector DSP](https://research.tue.nl/en/publications/recursive-filtering-on-a-vector-dsp-with-linear-speedup/);
  [Chaurasia et al., "Compiling High Performance Recursive Filters"](https://dspace.mit.edu/handle/1721.1/137548)).
  **Not applicable bit-exactly:** expanding `s[n+2]` in terms of `s[n]` changes where products are truncated (>>23
  at every step), so the bits change. Only cross-instance SIMD keeps the firmware's arithmetic.
- **The two axes in this code:**
  - *Across the 32 frames:* valid wherever the accumulator is cleared every frame and the only per-frame state is a
    ramped gain. `gain_i = g0 + i*step` wrapping mod 2^32 equals i repeated `addWrap`s exactly, and `vpmulld`
    computes it exactly. This covers sumChannels, mixBuses, rampBuffer, scaleBus, mixOutput, the track filter's
    output mix, and the diffusers' ACC0/ACC1 terms only where they do not feed back (they do, through `d2`, so they
    are out).
  - *Across the 8 tracks:* the per-track stages (`track::filter`'s resonator and second stage, `driveClip`, the amp
    envelope and VCA, BASE/WIDTH) run the same code with per-track state. The sample loop is the same for every
    filter type, since only the per-block coefficient paths (pathA/pathB) differ, and those stay scalar. SoA: 8
    lanes of int64, which is two AVX2 registers.
- Talks: Timur Doumler, "Want fast C++? Know your hardware!" (CppCon 2016: cache locality, branch prediction, SIMD;
  [schedule](https://cppcon2016.sched.com/)). Fabian Renn-Giles and Dave Rowland, "Real-time 101" (ADC 2019; known
  from memory, my search did not find it).

## 3. Emulators that faced this

- **Gearmulator's DSP56300 JIT** (`C:\dev\MModule\source\dsp56300`, asmjit): primary evidence in this repo. Its
  "left-aligned ALU" series (commits `bb0a8463`..`4274da56`, `8e64dbfe`, 2026-08) keeps the 56-bit accumulator in
  bits 63..8 of a 64-bit register. The groundwork commit measured that the sign-extend and re-mask pairs around every
  ALU op were **9.1 % of all emitted instructions**, word-size maintenance as a whole 15.0 %, and the multiplies
  only 1.2 %. Left-aligned, the DSP's modulo 2^56 is the register's own modulo 2^64, and overflow (V) is the host's
  signed-overflow flag. Its CCR flags are also computed lazily: `ccr_dirty` in `jitops_ccr.cpp` records the last
  ALU result and works out the flags only when something reads them. The lesson for EMAC: the 48-bit range and
  overflow checks, not the multiplies, are the cost to remove.
- **Dolphin DSP-LLE:** its recompiler is "considerably faster" than the interpreter, by its developers' account, but
  the only figures are forum anecdotes ([Dolphin forum](https://forums.dolphin-emu.org/Thread-lle-recompiler-vs-lle-interpreter)).
- **Static recompilation of DSP firmware to C:** I found nothing citable. This port already is a hand-written static
  translation, with the firmware's control flow resolved at write time, so a JIT has nothing left to remove. MoDule's
  own record (ticket 31) shows a JIT compiling on the audio thread costing 1.5 to 8 ms on a first hit.

## 4. Compiler side

- **MSVC PGO** (`/GL`, `/LTCG` + `/GENPROFILE`, run, `/LTCG:PGOPTIMIZE`): Microsoft's Xbox guidance expects
  **5-30 %** ([Xbox PGO](https://devdocs.xbox.com/tools/tools-console/pgo/pgo_full);
  [MSVC docs](https://learn.microsoft.com/en-us/cpp/build/profile-guided-optimizations)). It helps most with
  branch layout and inlining, which matter here: the MACSR checks, page-table lookups and machine switches. Sample
  PGO (SPGO) skips the instrumented build
  ([C++ team blog, May 2026](https://devblogs.microsoft.com/cppblog/?p=36517)). Train it with `stSyntaktBench`.
- **clang-cl vs MSVC:** on SPEC 2017 INT, clang-cl beat cl at every optimization level, x264 most of all
  ([LLVM dev mtg 2024, Javaid](https://llvm.org/devmtg/2024-10/slides/quicktalks/Javaid-SPEC-2017.pdf); that was on
  Windows Arm). clang-cl inlines more aggressively than MSVC, even with `/Ob3`
  ([Lemire 2023](https://lemire.me/blog/2023/02/27/visual-studio-versus-clangcl/)). clang also offers
  `__builtin_add_overflow` (a `jo` branch) and `__builtin_bswap32`, both useful here. [INFER] Building only the
  stEngine static library with clang-cl is a cheap experiment, since the ABI is MSVC-compatible.
- **Aliasing:** `Sram::at()` returns `int32_t&` into a `std::array`, and every `_sram.at(Steps + …)` load inside the
  filter loop may alias the stores to `BufA`/`BufB`/`_out`. The compiler must reload the steps every sample. Hoist
  them into locals, as the coefficients already are, or use `__restrict` pointers. `Memory::word()` goes through
  `memcpy` and a byte swap, which is safe, but the compiler cannot vectorize across it.
- **Branchless saturation:** `std::clamp(v, INT32_MIN, INT32_MAX)` compiles to `cmov`. The overflow freeze can be a
  sticky mask (`frozen |= ovf; acc = frozen ? held : r`) instead of an early return.

## 5. Real-time considerations

- **Denormals:** irrelevant to integer arithmetic. They matter only where floats appear: the conversion to the host's
  float buffers, and the stack's resampler when the host is not at 48 kHz. JUCE's `ScopedNoDenormals` covers those.
- **Cache:** 5.7 MB per instance is mostly the delay ring (4 MB) and the reverb lines (672 KB). A block touches only
  the read and write heads, a few KB. The hot working set is the two 64 KB SRAMs, the section 7 tables the engines
  read, and the page table (16 KB per used chunk) [INFER: about 200 KB]. That fits a 1-2 MB L2 for one instance.
  16 instances compete for L3. Keep `reserveLines()` so no page is first touched on the audio thread.
- **Threads:** ASIO drivers and hosts register the audio thread with MMCSS "Pro Audio", priority 23-26
  ([Microsoft MMCSS](https://learn.microsoft.com/en-us/windows/win32/procthread/multimedia-class-scheduler-service)).
  A plug-in's own worker threads do not inherit it, and Windows has no equivalent of Apple's Audio Workgroups
  ([JUCE forum](https://forum.juce.com/t/audio-workgroups-equivalent-on-windows/58626)). Worst blocks spike on DPCs,
  page faults, other processes and core parking, not on the plug-in's mean cost. The voice CPU and CPU #1 run one
  block apart, so they could run on two threads, but MoDule measured that idea for the Machinedrum and removed it
  (ticket 25: 2.5 points off the audio thread for nearly twice the CPU). Stay single-threaded and measure the worst
  block.

## Recommendations, ranked

**R1. Vectorize the gain-ramp mixes across the 32 frames.** *sumChannels (`sumDirect`), mixBuses, rampBuffer,
scaleBus, mixOutput, the track filter's output mix.*
Technique: under the existing precondition (fractional, accumulator clear, ≤10 products), compute every frame's
gains in closed form with `vpmulld`, the products with `vpmuldq`, `>>23` with the emulated shift, sum across channels
in int64, then saturate or wrap the store in the vector. The big-endian loads and stores take a `vpshufb` byte swap,
1 µop. Each channel's source is contiguous across frames (stride 4), so the loads are plain vector loads.
Gain: these routines are about 17 % of the profile, about 10 us. 4 products per `vpmuldq` against about 4 scalar µops
each (imul, sar, add, bswap) [INFER: 3-4x on the inner loop] saves **about 6-7 us (10-12 %)**.
Bit risk: low. Exactness rests on integer associativity under the bound the code already proves, and the gains match
`addWrap` exactly mod 2^32. The oracle tests catch a wrong shift fix-up at once, because every negative product
would be off by one ULP. Add a randomized kernel-vs-`Emac4` test with INT32_MIN/MAX operands and a mode-0xA0
saturating store.
Effort: medium (about 300 lines of intrinsics, one kernel shape reused six times).

**R2. Give the remaining scalar loops an accumulator mode without checks.** *Reverb's shelve, readModulated, line 0
and the half sums, feed's fallback, the delay, driveClip, onePole, mix.*
Technique: these loops clear each accumulator every iteration after at most 8 products, so in fractional mode they
can never overflow. Check `fractional() && allClear()` once at routine entry, then run a template
`Emac4Proved<Fractional>` whose `mac` is a bare `acc += (y*x)>>23`: no PAV, no MACSR read, no 48-bit check. Fall back
to the current code otherwise. This is Gearmulator's finding applied to EMAC: range maintenance, not the multiply,
is the cost (9.1 %/15 % of emitted code there). Also make MACSR a template parameter wherever a routine sets it
itself (scaleBus).
Gain: [INFER] 25-40 % of reverb + feed + delay + drive (about 13 %), so **about 3-4 us**.
Bit risk: low. It is the `setSum` argument applied per loop. Oracle scenes with a saturated input (a full-scale
square into the reverb) exercise the fallback.
Effort: low to medium.

**R3. Resolve the buffers once per routine; stop calling `Memory::word()` in inner loops.** *Reverb, delay, feed,
smooth, copySide, the eDMA model.*
Technique: `bytesAt` once per buffer, as `sumDirect` does, then loop with `wordAt`/`setWordAt`. This removes the SRAM
range test and the page-table branch from every access, and lets R1/R2 vectorize. Later option: keep CPU #1's SRAM
in host order, as the voice CPU's `Sram` does with `beByte`, which removes the byte swaps too.
Gain: [INFER] 15-25 % of those routines, **about 2-3 us**. The host-order variant gains more, but is a larger change.
Bit risk: low. A wrong offset or an unhandled page boundary shows up as an oracle mismatch or a `fault()`.
Effort: low (per routine); high for host-order SRAM.

**R4. Batch the eDMA model.** *`runChannel`, and the 2.6 % memmove.*
Technique: when a minor loop's source and destination units lie in held pages and do not overlap, copy the whole
loop with one `memcpy`. Big-endian to big-endian needs no swap. Find which call site the memmove samples come from
(`/Ob` inlining, or `copy_n` of `VoiceOut`) before changing it.
Gain: [INFER] most of the 4.3 %, **about 2 us**.
Bit risk: low. Address wrap (`modulo` offsets) must split the copy, and an oracle mismatch shows a missed split.
Effort: low.

**R5. Build: AVX2 dispatch, PGO, and clang-cl tried on stEngine.**
Technique: compile the R1/R6 kernels with `/arch:AVX2` in their own translation unit and select them at
initialization by CPUID (scalar otherwise). PGO trained on `stSyntaktBench`. Then build stEngine alone with clang-cl
and compare.
Gain: PGO 5-30 % by Microsoft's guidance [INFER: 5-10 % here, about 3-6 us]. clang-cl is unknown here: measure it.
Bit risk: none for integer semantics. But the `beByte` history (a miscompile, or UB, in MSVC Release only) shows
that compiler changes can expose latent UB, which the oracle tests catch. Run the gate under both dispatch paths
(an env var that forces scalar).
Effort: low.

**R6. Run the per-track stages across the 8 tracks in SoA.** *`track::filter`'s resonator and second stage,
`driveClip`, ampEnvelope/vca, BASE/WIDTH.*
Technique: restructure the voice CPU's block from voice-by-voice into engines first, then each stage over all 8
tracks: two `__m256i` of int64 per accumulator, coefficients and state in `[8]` arrays. Keep pathA/pathB scalar per
track. Use a lane mask for the `clear()` precondition, and send any lane that fails to the scalar code. The
`addSat` of the stage gain works per lane with compare and blend.
Gain: filter + drive + envelope are about 10 % (about 6 us). Recursion keeps 2 dependent multiplies per sample, so
[INFER] 2-2.5x, **about 3 us**.
Bit risk: medium. The shared scratch (`BufA`, `BufB`, `Steps`, `Target` at fixed SRAM addresses) is overwritten
track by track. If anything, the oracle's SRAM comparison included, reads it after the stage, the last track's
contents must be written back. Each voice's `_out` buffer must still be intact when the stage runs. The oracle tests
compare every track's output, so a lane error shows on that track.
Effort: high. Do it after R1-R5.

**R7. Oscillators and wave lookup across samples.** *wave lookup, pmOscillator.*
Technique: where the phase advances by a constant increment over the block (closed form, as in R1), compute the 32
phases with vector multiplies and the interpolation's two products with `vpmuldq`. The table reads stay scalar or
use `vpgatherdd`. Gathers are slow on some AMD cores [INFER], so measure.
Gain: [INFER] about 1-2 us.
Bit risk: low to medium. Feedback oscillators (`feedbackOscillator`) are recursive and stay scalar.
Effort: medium, per engine.

**R8. Keep the hygiene.** No threads (ticket 25). `reserveLines()` before the first block. No allocation in `Memory`'s
copy on the audio thread. Report the worst block and its jitter, not just the mean, as MoDule's CLAUDE.md asks of
any change to the render path.

**Not recommended:** a JIT or recompiler (nothing left to remove, and it compiles on the audio thread); block or
matrix IIR forms (they move the truncation points, so the bits change); an AVX-512 path (27 % of machines).

**Expected total** [INFER]: R1-R5 bring 60 us to roughly 42-46 us, and R6-R7 to roughly 36-40 us, about 6 % of a core
at 48 kHz.
