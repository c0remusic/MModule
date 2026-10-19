# Slide on OS 1.63: where the Machinedrum glides a Lock, and what MoDule needs to play it

Research for md-drums-seq's Slide row (a row shared by the 16 Tracks, no amount): on a slide step, each Track's locked
values glide from that step's values to those of the Track's next Hit, over the time between the two, as on the
machine. Read-only: no source file changed, nothing built or run. Repository paths are relative to `C:\dev\MModule`.

Sources and method:

- **Manual:** Elektron, *Machinedrum User's Manual* for OS 1.63 (the user's copy,
  `Downloads/Audio/Elektron/machinedrum_manual_OS1.63_1.pdf`, the file `research/06-groupes.md` of
  `.scratch/md-drums-editor` cites), section "PARAMETER LOCK SLIDE", printed pages 49-50.
- **OS code:** the user's flash image `elektron_sps1-1uw_os1.63.bin` (SHA-1 a872a2f3527063673d6ea6d3080c4c62ef0cadc1,
  checked), section 0 depacked by a Python port of `md::fw::aplibDepack` / `parseContainer`
  (`source/elektron/md/mdEngine/tools/mdfw/Firmware.cpp:70-132`) into a scratch folder outside the repository,
  disassembled with Capstone 5.0.7 in M68K mode, as `research/06-groupes.md` did. Static reading only. No OS byte is
  copied here: addresses and behaviour only.
- **Notation** as in `research/06-groupes.md`: **OS $x** is the ColdFire OS (section 0, run at $200000). Patch memory
  is seen at $700000 (`mdLib/mdmemorymap.h`, `g_patchOsAlias`): the working Kit at $70000a (its 16 × 24 parameters at
  $70001a, its machine words at $7001aa), pattern n's three blocks at $7272c0 + 2198n (main), $76bdc0 + 204n (tail) and
  $780000 + 2316n (steps 33-64), as `mdLib/mdlivepattern.h` and `mdhardware.cpp:1412-1419` lay them out from $100000.
  Internal SRAM ($1000000-) holds the arrays HostModel already pokes (`HostModel.cpp:13-25`).
- Marks: **[V]** read in the code or a document at the named place, **[M]** measured, **[I]** inferred. Nothing here is
  [M]: no slide has been played on the firmware oracle or on MoDule.

## In short

- **Nothing in this repository knew it before.** "Slide" appears only as a pattern-format field
  (`mdProtocol/mdsysexautomation.h:114-142`, `mdLib/mdlivepattern.h:30-31`, `.scratch/md-editor/README.md` line 176:
  md-mm's JOUER wrote the row, the factory patterns have none). Machinemodule's protocol notes
  (`.scratch/uc-less-engines/machinemodule/docs_PROTOCOL.md`; there is no `docs/PROTOCOL.md` in MModule) and
  `HostModel` stop at the control tick; the sequencer was left out with the OS. [V]
- **OS 1.63 has no slide routine.** The slide is four pieces of inline code: the setup and the per-clock update inside
  the sequencer's clock handler **OS $239762** (one function, a $c4-byte frame, from $239762 to $23c6d2), a hand-over
  in the same function, and the arming in the control tick's trigger path (**OS $20ad9a**, at $20b078-$20b1d0). [V]
- **Setup (OS $23b25a-$23b812)**, at each step boundary, for the step that plays at the next one [I]: for every
  parameter a Track locks on a step that has its trig and the Slide row, increment = (end − start) × 128 / clocks,
  truncated, where start is the step's Lock, end the Lock on the same parameter at the **Track's next trig**, or **the
  Kit's value** when that trig has none, and clocks the 24-ppqn clocks between the two trigs. [V]
- **Update (OS $23ac2c-$23add2)**, on every **24-ppqn clock** (the MIDI clock: 6 per step at 1x), while the sequencer
  plays: current += increment; past the end it stops, else the **smoothing target** ($1000ddc + 24t + p) takes
  current >> 7. The OS's own smoothing ($10002e0, every control tick) then chases it; the slide never writes the
  smoothed array or the voice array. [V]
- **The next trig ends it**: that trig's Lock, or the Kit's value, goes straight into the target, the smoothed array and
  the voice array (no glide), as every Lock does, and the increment it arms is 0 unless it slides itself. [V]
- **An unlocked parameter does not slide**, whatever the Slide row says; a parameter locked only on the next trig jumps
  there. [V, code and manual]
- **MoDule cannot call it** as it calls the machine, LFO and smoothing routines: the code is inline in an interrupt
  handler, reads the pattern in patch memory that `MachineRunner` does not map, and is fed one step ahead from the
  pattern. Its arithmetic is ten lines, portable bit for bit into `HostModel`. [V for the structure, I for the port]
- **Recommended contract: (a)**, a slide note with the Hit (note 89) carrying the clocks to the next Hit, and target
  notes for the next Hit's Locks; MoDule then runs the OS's own ramp on the OS's own smoothing. (b), intermediate
  Locks without a Hit, breaks on channel 1 in live play, where a Lock without a Hit cannot name its Track, and puts the
  machine's ramp in the sequencer. [I]

## 1. Where OS 1.63 implements the slide

### The manual

The manual (p. 49-50) says, paraphrased: a Lock changes its value exactly when its step plays; with slides, a value
locked on one step glides to the value on the next; for a parameter to slide it must be locked on two steps; a slide
trig on a step with a Lock makes the parameter slide "to the next parameter locked value"; the slide's speed follows the
tempo and it completes exactly when the next position with a drum trig is reached; only parameters locked on the slide
steps slide; the Slide window has one row for the whole pattern (ALL) or a row per track. [V]

The code agrees on all of it but one point: the end value is the one at the **next trig** of the track, and when that
trig has no Lock on the parameter, the **Kit's value**, not a later Lock (section 3). [V]

### Who runs it

- The sequencer's clock is the ColdFire's timer 2, run one-shot: its interrupt handler (**OS $23c83e**, rte at
  $23ca7a) reprograms the reference register for the next event ($252e98 holds the module base; +$124, +$12c and +$131
  are the reference, counter and event registers of timer 2 in the MCF5206e's map [I]) and calls **OS $239762** at
  $23ca64; when the control tick holds its lock ($262548), it sets $28d144 instead and the tick calls $239762 at its
  end ($20ba92-$20baa4). [V]
- The next event is the nearest of three countdowns (OS $23c0ca-$23c188): the step ($261a42, reloaded from $261a90),
  the swung step ($261a46, from $261a94) and the **24-ppqn clock** ($28d16a, from $261a98). One clock is
  30,000,000 / tempo word timer units (OS $23b8ea-$23b918; the tempo word $100150c is BPM × 24), so 1.25 × 10⁶ / BPM
  units, 2 µs each if the clock is 24 ppqn [I]. The step is a whole number of clocks from the table at **OS $247e4c**:
  6, 3, 8, 4, indexed by the pattern's scale byte (main +146, copied to $28cd8c and $28cd98), that is 1x, 2x, 3/4x and
  3/2x [I for the names]. The swung step comes the step's length × swing word / 16384 later, in timer units (OS
  $23ba56-$23ba70, the swing word at main +$8c). When $28cda4 is set, the clock's length is a constant
  instead (OS $23b8dc-$23b8e8). [V; I that $28cda4 means slaved to an external MIDI clock]
- $28d14c is set when the coming interrupt is a clock (OS $23c6b0-$23c6b8) [V], and the handler then sends $F8 through
  OS $213c4e, which writes the UART, at $23ca3c-$23ca56 when $100156c is set [V for the code; I that $100156c is the
  clock-out setting]. Hence: $28d14c marks the MIDI clock, 24 ppqn. [I]

### The arrays

All in main RAM, 16 tracks × 24 parameters in HostModel's order, bytes ($ff = none) or 32-bit words. [V for the uses
below; the names are mine]

| Address | Size | What it holds |
|---|---|---|
| $2ad726 | 16 × 24 bytes | the next step's Lock, staged; $fe = put the Kit's value back |
| $2ad8a6 | 16 × 24 bytes | the Lock waiting for the track's trigger (the tick applies it) |
| $2b034d | 16 × 24 bytes | "the last trig locked this parameter": its next trig puts the Kit's value back |
| $2ada26 | 16 × 24 words | the next step's slide increment, staged |
| $2af74a | 16 × 24 words | the increment waiting for the trigger |
| $2afd4a | 16 × 24 words | the running increment; 0 = no slide |
| $2ae996 | 16 × 24 words | the slide's current value, value × 128 |
| $2b05ee, $2ad126 | 16 × 24 words | the end of a rising slide, of a falling one (value × 128) |
| $2ae8c2 | 16 words | a sequencer or panel trig waiting for the control tick (set by the note handler at $20cdd2, cleared by the tick's trigger path at $20b1c4) |
| $2af136 | 1 word | clocks left until the hand-over |

### Setup: OS $23b25a-$23b812

At each step boundary ($261a42 run out), after the trigs of the step that plays now were dispatched (OS $2397b8 on,
through $206c8e and the note handler, source 0), the function computes the step at $1001f2c, which the end of the same
pass advances (OS $23be2c): the step that plays at the next boundary. The trig queue is double-buffered ($28d152
toggled at $23a908) [V]; that the computed step is the next one to play follows from that order [I]. For each track t
(the loop advances 24, 96 and 4 bytes per track, OS $23b7da-$23b810) and parameter p:

1. If the marker $2b034d says the last trig locked p, and t has a trig on this step, the staged byte becomes $fe and
   the marker $ff. [V, $23b396-$23b3be]
2. If p is in t's lock mask (main +$40 + 4t; each locked parameter takes the next lock row r, in track then parameter
   order) and row r holds a value v on this step (main +150 + 32r + step, or +$4c + 32r + step − 32 in the block of
   steps 33-64) and t has a trig on this step [V, $23b3c0-$23b45e]:
   - **Slide or not:** the Slide row is the pattern's mask (main +$84, steps 33-64 at +$44) when the tail's slide EDIT
     ALL word (+4) is set, else t's own (tail +$4c + 4t; +$88c + 4t in the long block). [V, $23b462-$23b498]
   - **Not a slide:** marker = the Kit's value, staged byte = v. [V, $23b70c-$23b768]
   - **A slide** [V, $23b49c-$23b708]:
     - start = v × 128;
     - the next trig n: the steps after this one on t's 64-bit trig mask, wrapping from the loop's end ($1001f4c) to
       its start ($1001f48); steps = the distance (the whole loop when t has one trig);
     - end = row r's value at n × 128, or the working Kit's value ($70001a + 24t + p) × 128 when n has no Lock on p;
     - clocks = clocksPerStep × steps; when exactly one of the two steps is on the swing row (the pattern's when the
       tail's swing EDIT ALL word, +8, is set, else t's own at tail +$8c + 4t), clocks −= or += floor(clocksPerStep ×
       swing word / 16384), minus when this step is the swung one; clocks = 1 if it came to 0;
     - increment = (end − start) / clocks, signed 32-bit division (`divs.l`), truncated toward zero, staged in $2ada26;
     - the end goes to $2b05ee when end > start, to $2ad126 otherwise;
     - marker = the Kit's value; staged byte = v (the slide starts from the step's own Lock).

### Hand-over: OS $23b0f8-$23b256

The step pass sets $2af136 to clocksPerStep − 1 (OS $23b264-$23b276); each clock counts it down, and at 0, one clock
before the step plays, every staged byte goes to $2ad8a6 ($fe as the Kit's value: the working Kit's, or the Kit of the
pattern queued when $28cd9c says one is, OS $23b114-$23b15e) and every staged increment to $2af74a; both staging
arrays are cleared. CTR machines' Locks go out through their own routines here. [V]

### Arming: the control tick's trigger path, OS $20b078-$20b1d0

When the tick ($20ad9a, about 120 Hz on the firmware, every 11 blocks in HostModel) processes a triggered track and
$1001544 is set (EXTENDED mode, which Locks need on the machine [I: the flag's identity is not traced]), every
parameter with a byte in $2ad8a6 takes it at once in the smoothing target $1000ddc, the smoothed array $1000a4c and the
voice array $10011cc, as HostModel's `applyLocks` does (`HostModel.cpp:143-161`); the slide's current value takes
value × 128, the running increment takes the waiting one ($2af74a, then cleared), the byte goes back to $ff; then
$2ae8c2[t] is cleared. A parameter whose byte is $ff keeps its running increment. [V]

### Update: OS $23ac2c-$23add2

In $239762, every pass that reaches $23ac1c while the transport state $1001f34 has a bit of $d set (OS $23a79c-$23a7a6:
playing; a stopped sequencer jumps to $23c192 and the slide freezes [I for the state's meaning]) runs the update when
the pass is a clock ($28d14c) or a forced one ($28d206, set by the start routines at $2390a8, $239626 and $239710), and
$1001544 is set. For each track whose $2ae8c2 is clear, for each parameter with a running increment [V]:

- next = current + increment; for a rising slide, next > its end stops the slide (increment = 0, nothing written); for
  a falling one, next < its end;
- otherwise current = next, and the parameter's new value goes out by machine (the Kit's machine word, $7001aa + 4t):
  CTR machines ($70, $71, $78-$7f on SYN1-8) through OS $237ae4, $237ba0 and $237c54; MID machines ($60-$6f) into
  $2ae062 (their MIDI values, rounded: (current + 64) >> 7) for the parameters the table at $261a25 marks; **every
  other machine: the smoothing target byte $1000ddc + 24t + p = current >> 7**.

So the voices hear the slide through the OS's smoothing ($10002e0, (3 × smoothed + target × 128) >> 2 each control
tick, `docs_PROTOCOL.md:160-163`) and the LFO apply after it, which go on as for any knob move. [V]

### The ramp it plays, worked out

From the arithmetic above [I: derived, not measured]: the trig dispatched at a boundary sets $2ae8c2 in the same pass
before the update, so the update skips the track until the tick has armed it; the first increment lands one clock
after the slide's trig, and the next trig's pass skips the track again. The target therefore takes start, start + inc,
..., start + (clocks − 1) × inc (each >> 7), and the next trig snaps it, smoothed and voice arrays too, to the end. One
step at 1x from 0 to 127: inc = 16256 / 6 = 2709, targets 0, 21, 42, 63, 84, 105, then 127 at the next trig. A clock
lasts 2.5 / BPM s: 20 ms at 125 BPM, 882 samples at 44.1 kHz, 2.5 of HostModel's ticks; the smoothing lags each stair
by about three ticks. A one-value slide over more than 128 clocks (over 21 steps at 1x) gets increment 0 and jumps at
the next trig. A slide's speed follows the tempo, as the manual says, since the clocks do.

## 2. Could MoDule's engine call it?

Not as `HostModel` calls the smoothing ($10002e0), the LFO routines ($1000088, $1000332, $10001e8, $204c94) or the
machine functions, which are functions with arguments or none, on SRAM and main RAM alone:

- **Entry and arguments:** none. The setup and the update are stretches of $239762, an interrupt-driven routine that
  also dispatches trigs, computes swing, sends MIDI clock, queues pattern and Kit changes and reprograms the timer.
  [V]
- **What it reads:** the pattern in patch memory (trig masks, lock masks, lock rows, slide and swing rows and EDIT ALL
  words, the scale and swing bytes), the transport ($1001f2c step, $1001f48 and $1001f4c loop, $28d202 pattern,
  $1001f34 state), the working Kit at $70001a and its machine words at $7001aa. `MachineRunner` maps main RAM
  $200000-$2fffff and the SRAM only (`MachineRunner.cpp:18-19, 53-58`); patch memory reads as 0 there. And the setup
  needs the next trig and its Lock in advance, which MoDule never has: Live's notes reach it as they play. [V]
- **The update alone** could be entered at OS $23ac50 with a6 on a scratch frame of $9c bytes (it sets every local it
  uses) and $23add4 patched to `rts`, as `HostModel` patches the master bodies' exits (`HostModel.cpp:54-57`); it reads
  $2ae8c2, $2afd4a, $2ae996, $2b05ee, $2ad126, the mutes ($1001520, $1001530) and $7001aa (0 there, so every track
  takes the audio-machine branch), and writes $1000ddc. But `HostModel::tick` rewrites $1000ddc from `target()` at
  every tick (`HostModel.cpp:376-378`), and the OS loop costs a 16 × 24 sweep of emulated 68k per clock for an add, two
  compares and a shift. [V for the code, I for the cost]
- **How often:** once per 24-ppqn clock while the sequencer plays (44100 × 2.5 / BPM samples: 882 at 125 BPM, about
  27.6 blocks of 32), not per control tick (11 blocks), not per block. The setup runs once per step boundary, one step
  ahead; the hand-over one clock before the step. [V]
- **Output:** the smoothing targets only (audio machines), never the smoothed or the voice arrays; the trigger path
  writes those, for the start and for the next trig's value. [V]

A port into `HostModel` is the faithful way [I]: per track and parameter a current value (× 128), an increment and an
end, armed where `applyLocks` applies the Hit's Locks (the trigger path's place), advanced on a clock schedule of
110250 / BPM samples from the Hit, `target()` returning current >> 7 while it runs, and the Track's next Hit disarming
it as `applyLocks` already snaps its Locks or the Kit's values. The tempo is already there (`Processor::processBpm` →
`messages::tempo` → `HostModel::setTempo`, `mdDrumsDevice.cpp:327-329`). Like `mdDrumsLfo.*`, the port can be checked
against the OS's code run on the firmware oracle.

## 3. After the slide, and parameters without a Lock

- **Until the next trig:** with the clock count the setup computed, the slide never passes its end before the next
  trig (truncation keeps start + clocks × inc on the near side of the end), so there is no hold: the target climbs to
  about one increment short and the next trig puts the end in at once. A hold happens when the real time to the next
  trig is longer than the clocks counted: the swing correction is floored to whole clocks, and a next trig the note
  handler drops (the track user-muted, $20ccf6) leaves the slide to run on until the next increment would pass the
  end, then stop; the target then stays at that last value, the end within one increment, until a trig of the track
  plays. [V for the code paths; I for the cases]
- **Then:** every parameter the slide step locked carries the marker, so the next trig gets either its own Lock or
  the Kit's value ($fe), straight into the three arrays, and an increment of 0 unless that trig slides too. [V]
- **A Hit from elsewhere** ends it the same way: a MIDI note or a panel trig on the track (source 1 or 2) puts the
  Kit's values back into every parameter with any Lock state, waiting, staged or marked (OS $20cd04-$20cd74); the
  track a Link fires gets the same from a MIDI note, and from a sequencer trig keeps only a Lock already waiting for it
  (OS $20ceb4-$20cf8a). The Kit's value waiting then arms the waiting increment, normally 0. [V] In MoDule every Hit is
  a note and carries its Locks (ADR 0003); the Track's next Hit, or a Link's, disarming the slide matches the machine's
  sequencer trig. [I]
- **No Lock on either step:** nothing; the parameter is never armed. **A Lock on the next trig only:** no slide, it
  jumps at that trig. **A Lock on the slide step only:** it slides to the Kit's value (the code at $23b5ca-$23b5e2),
  which the manual's "locked at two separate steps" does not describe. [V, code; the manual's wording V; not measured]
- **The Slide row on a step without the track's trig:** nothing for that track. [V, $23b454-$23b45e]

## 4. Two contracts for MoDule and md-drums-seq

### (a) A slide note with the Hit, and what MoDule needs in advance

md-drums-seq sends, with the Hit and after its Locks (and note 88): note 89, "this Hit slides", its velocity the clocks
to the Track's next Hit as the OS counts them (clocksPerStep × steps, the swing correction, at least 1); and for each
parameter this Hit locks that the next Hit locks too, a target note (90-113 for parameters 0-23, the value on the
Locks' scale). A parameter locked here without a target slides to MoDule's Kit value, as OS 1.63 does. All bind as
note 88 does (channels 2-16 name their Track; channel 1 the last channel-1 Hit at the same sample, else Track 1),
taken where the block's Locks are taken (`HostModel::applyLocks`), dropped without a Hit. md-drums-seq plans each step
one step ahead (its `CLAUDE.md`), as the OS does, so it knows both. [I: a proposal]

- **Faithfulness:** MoDule runs the OS's own integer ramp (the same start, end, clock count, truncation, stop rule and
  >> 7) into the OS's own smoothing, and the next Hit snaps as on the machine. What differs is the clock's phase: the
  OS's clocks run on the transport's grid, so a swung slide step's first increment comes less than a clock after it,
  where clocks counted from the Hit come a full clock after; md-drums-seq's micro-timing and ratchets have no OS
  counterpart at all. [I]
- **A Live clip:** a few notes at the Hit's position, which Live keeps, replays without the sequencer, and moves with
  the Hit on quantize, groove and copy (ADR 0003's reason for notes). A clip edit that moves the next Hit leaves the
  clock count stale; Live's chance on the slide Hit drops the whole slide with it, and on the next Hit leaves the slide
  to stop at its end, as the machine does with a muted trig. [I]
- **Latency and sample accuracy:** no Hit waits for anything: the notes are taken with the block's Locks, and every Hit
  still sounds `Engine::HitDelay` after its note. The ramp's stairs are smoothing targets, which the engine reads at its
  ticks, as the OS reads them at its own (3 to 11 blocks apart on the firmware, `mdEngine/README.md` "Links and
  Chokes"), so a clock applied at the block that holds it is within the machine's own spread. [I]
- **HostModel:** a slide state per track and parameter, two pending inputs like the Locks', the arming in
  `applyLocks`, a clock countdown in `renderBlock`, `target()` reading the slide, a skip for a track struck in the
  block (the OS's $2ae8c2). Contained; the Device adds three note ranges to `Device::apply`. [I]

### (b) Intermediate values as Locks without a Hit

md-drums-seq computes the ramp and sends a Lock per clock and parameter between the slide Hit and the next; MoDule
accepts Locks without a Hit while a slide is on.

- **Faithfulness:** only if md-drums-seq reproduces the OS's arithmetic (and the Kit's value as the end, from the
  `.kit` file of ADR 0003's 2026-10-16 amendment, a second copy of what MoDule knows), and MoDule writes a Hit-less
  Lock into the smoothing target only. Today a Lock snaps into the three arrays (`HostModel.cpp:143-161`): taken that
  way, the ramp would be a staircase without the OS's smoothing, so (b) needs a second Lock path anyway. The Locks'
  scale cannot carry 63 (ADR 0003, Consequences), so a ramp crossing it plays 62 or 64 there. [I; the scale V]
- **Channel 1:** played live, a Max for Live device's notes all reach MoDule on channel 1 (ADR 0003, amendments of
  2026-10-11 and 2026-10-13), where a Lock without a Hit just before it is Track 1's. Live, only Track 1 could slide,
  or a new binding rule is needed for Hit-less Locks, ambiguous when several Tracks slide at once. This decides
  against (b). [V for the rule, I for the consequence]
- **ADR 0003's guarantee:** a Lock without its Hit is dropped, which keeps a chance-skipped Hit's Locks from changing
  the sound that rings. Gating Hit-less Locks on "a slide is on" needs a slide flag with the Hit anyway, so (b) carries
  (a)'s first note and loses its compactness. [V for the ADR, I for the rest]
- **A Live clip:** at 1x, five extra notes per step of slide, per locked parameter, per Track; a long slide on several
  Tracks prints thousands of notes per pass, which quantize and groove shift off their clocks and any edit of the next
  Hit leaves wrong. Live play must also keep each note-off right after its note-on (md-drums-seq's issue 24). [I]
- **Latency:** each Lock lands at its sample, taken at its block; no Hit waits. Equal to (a). [I]
- **HostModel:** a Hit-less, target-only Lock path, held until the next Hit; less state than (a), but the binding rule
  of channel 1 and the drop rule both change in `Device::apply`. [I]

### Recommendation: (a)

It puts the machine's behaviour in MoDule, where the OS's smoothing and the snap at the next Hit already are, with the
three numbers the OS itself computes the slide from; it travels as a handful of notes bound to their Hit, which a clip
keeps and Live play binds on channel 1 by the rule already in place; no Hit waits; and `HostModel` changes in one place,
where the trigger path's Locks are, as the README lists local changes. (b) would need a slide flag too, a second Lock
path, and a binding Live play cannot give. [I]

## Open points

- **Not verified on the firmware** (done since: see "Measured on the firmware" below). The oracle can: write a pattern with Locks and a Slide row into `mdLib`'s machine
  (`Hardware::writeRamIfUnchanged` at the live pattern layout, or a `$67` dump), play it, and read $1000ddc, $2ae996
  and $10011cc at each tick. That settles the formula, the first increment's clock, the Kit's value as the end, and the
  freeze on stop. Until then everything above is static reading.
- **The clock count's range:** a velocity carries 1-127 clocks, 21 steps at 1x; the OS counts up to a whole 64-step
  loop (384 clocks at 1x, 512 at 3/4x). A second note for the high bits, or a cap in md-drums-seq, is to decide.
- **The clock's phase** for swung and micro-timed slide Hits, and what "the next Hit" means after a ratcheted step:
  md-drums-seq's choice; the OS gives no reference for micro-timing or ratchets.
- **Knob and host automation during a slide:** the OS's slide rewrites the target each clock; how its knob path treats
  a locked or sliding parameter was not traced. MoDule's `setParam` changes the Kit's value, which `target()` would
  ignore while a Lock or a slide holds the parameter. (Measured since, and MoDule changed: see below.)
- **The transport:** the OS freezes a slide when its sequencer stops; MoDule has no transport for Hits, so a slide would
  run its clocks out unless md-drums-seq sends something on stop.
- **Identities inferred, not traced:** $1001544 as EXTENDED mode, the timer's 2 µs unit, the scale names of the table
  at $247e4c, the states of $1001f34.

## Measured on the firmware (2026-10-19)

`mdSlideFirmwareTest` (`mdTrigLatencyFirmwareTest --slides`, in the gate) plays slides on the full firmware's own
sequencer: Pattern A01 rewritten in RAM (track 1's trigs, one lock row on FLTF, the Slide row with its EDIT ALL word),
PLAY from the front panel, track 1's FLTF smoothing target ($1000ddc + 12) read after every 256-sample block, at the
boot's 125 BPM in EXTENDED mode. [M] for everything in this section.

- **The formula holds**, value for value: 0 to 127 over one step at 1x gives 0 21 42 63 84 105, then 127 at the next
  trig; over two steps (12 clocks) 0 10 21 31 42 52 63 74 84 95 105 116, then 127; 100 to 20 over one step 100 86 73
  60 46 33, then 20. Each stair is a clock after the last (768 or 1024 samples at the block's resolution, 882 on
  average). The slide step's Lock is the start; the running value ($2ae996) is value x 128 and the increment ($2afd4a)
  the truncated quotient: 2709 for 16256 / 6, 1365 for 8192 / 6, 1354 for 16256 / 12.
- **The Kit's value as the end**: a slide step whose next trig has no Lock on the parameter slides to the Kit's value
  (64 here), and that trig puts the Kit's value back at once.
- **The trig's own clock is a race.** On some passes the trig's clock skips the track and the first stair comes a
  clock later (the start visible for a clock, five stairs in a step, the research's model); on others the control tick
  arms the track between the clock handler's trigs and its update, and the update steps it at the trig's own clock
  (the Lock and its first stair within one block, six stairs in a step, the last one increment short of the end:
  0 to 64 shows 10 21 31 42 53 63, then 64). Which one happens follows the control tick's phase against the clocks.
  MoDule's `HostModel::clock` always skips the clock in the Hit's block, the first case, within the machine's own
  spread.
- **Stopped mid-slide, it stands still**: after STOP the target held its value (21) for a second.
- **Starting the sequencer**: the first pass after PLAY plays step 1 without its Locks and its slide (the setup runs
  one step ahead, and there is no step before the first); from the second pass on they play. What a run staged stays
  staged across STOP: the next PLAY's first step slides with the last run's staging.
- **$1001544 is not EXTENDED mode**: it reads 0 with the EXTENDED LED lit and slides playing. Its identity stays open.
- **A CC over a Lock or a slide** sets the smoothing target at once, and the Kit's value: over a Lock (0) the target
  takes the CC's 100 and keeps it until the next trig, which plays the Kit's, now 100; over a slide it takes 100 until
  the slide's next clock, which writes the ramp's next stair as if nothing happened (0 10 21 31 42, 100, 52 63 ...).
  The firmware takes a CC a few milliseconds after it comes, so a CC just before a clock is overwritten within the
  same block. MoDule's `HostModel::setParam` did not touch a locked parameter's target; it now writes it, as the OS's
  knob path does (2026-10-19).

The clock count's range, the transport and the clock's phase above were settled with md-drums-seq on 2026-10-19
(ADR 0003, amended): note 114 for the clocks past 127; the host transport's MIDI clocks step MoDule's slides and stop
them; md-drums-seq counts the clocks between the two Hits as they play.
