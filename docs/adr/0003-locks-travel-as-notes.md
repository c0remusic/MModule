# Locks travel as notes, on their Track's MIDI channel

MD Drums plays Locks from Live clips, so that a sequencer in Max for Live can print them into clips and Live can play
them back without it. A Lock is a note on its Track's MIDI channel (channel 1-16 for Track 1-16): its pitch names the
parameter (64 to 87 for SYN1 to LFOM, the engine's 24), its velocity carries the value. It goes with the Hit of its
Track that reaches the engine in the same 32-sample block, and sets the parameter at once, without the OS's glide, as
the trigger path already sets a pending Machine. The Track's next Hit without a Lock on that parameter plays the Kit's
value again, at once too. A Lock with no Hit of its Track in its block is dropped. The Kit never changes. In Live, each
Track gets a MIDI track whose External Instrument sends to MD Drums on the Track's channel and returns the Track's Out.

Notes, because a Lock belongs to one Hit: a clip keeps notes as events, at the sample, LOM writes them exactly
(`Clip.add_new_notes`), and quantize, groove and copy move a Lock's note with its Hit's when both share a position. A
clip also gives every Lock's note Live's chance and velocity deviation: conditional and random Locks, with no sequencer
running.

## Considered Options

- **MIDI CC, the Machinedrum's own map.** A clip keeps CC as an envelope and Live sends only its changes: a second Hit
  locked to the same value sends nothing and plays the Kit. Only the 24 parameters have CCs, and CC reaches the engine
  through `HostModel::setParam`, which glides over the OS's smoothing, run once a tick (11 blocks of 32).
- **Host parameter automation.** JUCE's VST3 wrapper keeps the last point of each buffer and the Controller sends it
  at offset 0: a Lock lands up to a buffer early, two Locks in one buffer keep only the last, and it glides too. Live
  exposes only configured parameters to LOM, not 576.
- **Max for Live setting parameters through LOM.** On Live's main thread, with tens of milliseconds of jitter.
- **MPE note expressions.** Attached to the Hit itself, but three or four per note, and writing them through LOM is
  unverified.

## Consequences

- Amends ADR 0002: the Device takes Hits (notes 36-51, any channel, as before) and Locks (notes 64-87, on the Track's
  channel) from the host, still no CC or SysEx.
- A Live clip carries no MIDI channel, so Locks for Track n come from a Live track that sends on channel n. One MIDI
  track on one channel locks one Track.
- Velocity 0 is a note-off, so velocity 1-127 stretches onto values 0-127, rounded: 1-63 give 0-62, 64-127 give
  themselves (`messages::lockValue`). Both ends and the centre can be locked, 63 cannot. Every clip printed from the
  first push on depends on this scale.
- `HostModel` models Locks (its header listed them as not modelled): a local change to mdEngine, listed in its README.
- Locks are invisible to the host parameters and the editor, whose knobs show the Kit.
- A Hit whose note has a chance of not playing leaves its Locks without a Hit, and so dropped.

## Amendment (2026-10-11): on channel 1, a Lock goes with the Hit just before it

Live does not keep the MIDI channel a Max for Live device puts on its notes: md-drums-seq measured channel 1 for notes
it sent on channels 3 and 7, right after the device and on another track taking its MIDI. A sequencer playing in Live
could then lock Track 1 only.

A Lock on channel 1 now goes with the Hit note received just before it at the same sample, whatever that Hit's Track;
with none, it is Track 1's, as before (`Device::apply`). Channels 2-16 still name their Track, before or after its Hit.
So a sender that cannot keep channels sends each Hit followed by its Locks (Hit A, A's Locks, Hit B, B's Locks), and
one that keeps them, such as a clip through an External Instrument on channel n, works as before. A lone Track 1 Hit
plays its Locks in either order, as before.

- The order of two notes at one sample now matters on channel 1. MD Drums keeps it (`synthLib::Plugin` inserts an
  event after those at the same offset, JUCE's MidiBuffer too). Whether Live plays a clip's simultaneous notes in a set
  order is not measured: until it is, a clip on channel 1 carrying another Track's Locks may bind them to the wrong
  Hit, and a clip on its Track's channel is the safe way.
- `mdDrumsDeviceTest` and `mdDrumsVst3HostTest` check both rules, the second with md-drums-seq's notes.

## Amendment (2026-10-13): the last channel-1 Hit

md-drums-seq prints a pattern as one Session clip per Track, each on its own MIDI track routed to the MD Drums track's
"N-MD Drums" input, which reaches the plug-in on channel N (measured in Live 12.2.1; md-drums-seq issue 07). Track 1's
clip then plays on channel 1, and another Track's Hit can land between Track 1's Hit and its Locks at the same sample:
under the rule above, that Hit would take Track 1's Locks.

A Lock on channel 1 now goes with the last channel-1 Hit received just before it at the same sample; with none, it is
Track 1's, as before. Channels 2-16 are unchanged. Live play is too: a Max for Live device's notes all reach MD Drums on
channel 1, so the last channel-1 Hit is the last Hit. `mdDrumsDeviceTest` and `mdDrumsVst3HostTest` add a channel-5
(and a channel-3) Hit between a channel-1 Hit and its Locks.

## Amendment (2026-10-16): notes 124-127 are md-drums-seq's hello

md-drums-seq shows each Track's Machine and, later, the names of SYN1-8 and the Kit's values behind the Locks. The LOM
shows a plug-in's parameters only once configured in Live, 128 at most, which "Prepare Push list" already fills; so
MD Drums writes them into a file the device names through the MIDI it already sends (md-drums-seq's ADR 0003).

Four note-ons 124, 125, 126 and 127, in that order, on any channel, each velocity 1 + six bits of a 24-bit id (the
most significant first), are the device's hello; any other note-on between them cancels it, note-offs do not. They
trigger nothing, lock nothing, and are no note that plays nothing. The Device publishes the id (`Telemetry::hello`);
the Controller's timer writes `<temp>/md-drums-seq/<id>.kit` (`KitFiles`): the Kit played's name, each Track's
Machine id, name and SYN1-8 names, and its 24 Kit values in the Locks' order, version 1, one record per line. The
file is written at once for a new id and again when the Kit played changes, at most four times a second, whole
(renamed over itself); it goes fifteen seconds after its device's last hello, and with the plug-in. Notes 36-51 and
64-87 are unchanged. `mdDrumsDeviceTest` checks the hello and the files on a clock of its own, `mdDrumsProcessorTest`
a hello through the host's MIDI.

## Amendment (2026-10-17): note 88 accents its Hit

The Machinedrum's sequencer plays an accented step at velocity 128 + 2 x the Pattern's accent amount. A MIDI note stops
at 127 and the OS gives MIDI notes no accent, so no input of MD Drums carried one, although the engine plays it
(`HostModel::trigger`'s accent, `setAccentAmount`). md-drums-seq gives its Patterns an Accent row and an amount
(md-drums-seq issue 26).

Note 88, after the Locks' 64-87, accents the Hit it goes with, as the sequencer accents a step: that Hit plays at
velocity 128 + 2 x amount, and its Link's Hit too, as the OS's note handler gives the trig group's track the trigger's
accent ($20cf8c).

- It binds to its Hit as a Lock does: on channels 2-16 it names its Track; on channel 1 it goes with the last channel-1
  Hit received just before it at the same sample, or else Track 1. It applies to that Hit within the same 32-sample
  block, and is dropped without one.
- Its velocity carries the accent amount on the Locks' scale, 1-63 for 0-62 and 64-127 for themselves
  (`messages::lockValue`). The amount stays the engine's from then on, as the Pattern's does on the machine.
- The engine takes it where the block's Locks are taken (`HostModel::accent`), so the Hit's note-on, which triggers at
  once, may come before it: no Hit waits for it, and every Hit still sounds `Engine::HitDelay` after its note.
- Its note-off does nothing, like a Lock's.
- md-drums-seq sends, at one tick and for every ratchet of an accented step, the Hit, then its Locks, then note 88.
  `mdDrumsDeviceTest` checks that order, both rules of channel 1, the Link, the drop, and the loudness on the Track's
  Out: at amount 64 the Hit plays about twice as loud as at velocity 127.

## Amendment (2026-10-19): note 89 slides its Hit, notes 90-114 give the next Hit's Locks and the clocks past 127

The Machinedrum's sequencer slides a locked step: on a step with its trig, its Locks and the Slide row, every parameter
the step locks glides to the value of the Track's next trig, over the time between the two, at the tempo. md-drums-seq
gives its Patterns a Slide row shared by the 16 Tracks. OS 1.63 has no slide routine that MD Drums could call as it
calls the smoothing and the LFOs: the slide is inline code in the sequencer's clock handler, fed from the Pattern one
step ahead (`.scratch/slide/research-01-os-slide.md`). Its arithmetic is ported instead, and the sequencer, which knows
the next Hit, sends what the OS computes it from. The user agreed to this contract, (a) of the research, on 2026-10-19;
(b), a Lock per clock without a Hit, was ruled out: on channel 1, where Live play puts a Max for Live device's notes,
a Lock without a Hit cannot name its Track.

- Note 89, after the Hit's Locks, slides that Hit. Its velocity is the clocks of 24 ppqn to the Track's next Hit as
  they play (the steps between the two times the step's clocks, 6 at 1x, swing and micro-timing included, rounded to
  the nearest clock), 1-127.
- Notes 90-113, for parameters 0-23 in the Locks' order, give the value the Track's next Hit locks on a parameter this
  Hit locks, on the Locks' scale (`messages::lockValue`). A parameter locked without one slides to the Kit's value, as
  OS 1.63 does; a parameter this Hit does not lock never slides.
- Note 114, only past 127 clocks, adds 127 clocks per step of its velocity: clocks = velocity of 89 + 127 x velocity of
  114. md-drums-seq's longest slide is 64 steps at 1/8x, 3072 clocks. Without note 89 it slides nothing.
- They bind to their Hit as a Lock does (channels 2-16 name their Track; on channel 1 the last channel-1 Hit just
  before them at the same sample, else Track 1), are taken where the block's Locks are (`HostModel::slide`,
  `extendSlide`, `slideTarget`), and are dropped without a Hit of their Track in the block. Their note-offs do nothing.
  No Hit waits for them.
- The engine runs the OS's ramp ($23b25a, $23ac2c): increment = (end - start) x 128 / clocks, truncated toward zero,
  added at each clock, the smoothing target taking the sum >> 7 and the OS's smoothing chasing it at its ticks; it
  stops before a sum would pass the end. The clocks are the host transport's, which the plug-in stack turns into MIDI
  clocks on the host's 24-ppqn grid (`synthLib::MidiClock`; the Device takes them from the stack only, at their
  sample, `HostModel::clock`): as on the machine slaved to a clock, a slide steps on the transport's grid, a swung or
  micro-timed slide Hit's first step less than a clock after it, and stands still while the transport stops, as the
  OS's does when its sequencer stops (asked of MD Drums by the user through md-drums-seq). A clock in the slide Hit's
  own block skips its Track, as the OS's update skips a track whose trig waits for its tick; on the firmware the
  trig's own clock skips it or steps it as the control tick's phase falls, so this is one of the machine's two
  outcomes (`mdSlideFirmwareTest`, which measures the firmware's stairs against the same ramp). The Track's next Hit ends
  the slide at once with its own Locks or the Kit's values, as every Hit does; a Hit the mute drops, or another
  Track's, leaves it running. A host that gives no transport plays no slide.
- md-drums-seq sends, at one tick, with the last ratchet of a slide step, the Hit, then its Locks, then note 88 if it
  is accented, then note 89, the target notes and note 114. `mdDrumsEngineTest` checks the ramp against the research's
  worked one (0 to 127 over 6 clocks: 0, 21, 42, 63, 84, 105, a clock apart) and the OS's arithmetic block by block,
  the skipped clock, 384 clocks, the stop; `mdDrumsDeviceTest` the notes' binding, note 114, the drop and the
  transport stopped.
