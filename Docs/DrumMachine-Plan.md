# Drum machine: research, constraints and a plan

Written before any of it was built, so that the reasoning survives the decisions.
The expensive part of a subsystem this size is not the code — it is finding out
which shape it has to be, and that knowledge evaporates unless it is written down.

The prerequisite is in [ParameterCap-128.md](ParameterCap-128.md). Nothing here
can start until that lands.

---

## What the codebase decides for us

Four findings, each of which closes off an option that otherwise looks reasonable.

**The step sequencer is strictly monophonic.** `StepSequencer` holds
`int soundingNote = -1`, and a `StepData` carries one `degree` and one `octave`.
One note per step. A drum pattern needs kick and hat on the same step, so the
existing pattern structure cannot express one — whatever else changes.

**The 64-parameter cap is WaveLathe's own, not a platform limit.** It is
`paramreg::maxParameters`, enforced by the `uint64_t` mask the audio thread reads
to ask "is a hand on this dial?". `WaveLatheMaster` uses APVTS and has no such
limit. This matters because it means the cap is ours to raise.

**The sequencer already speaks MIDI** — `process(juce::MidiBuffer&, ...)` — but
both products are built `NEEDS_MIDI_OUTPUT FALSE`, so nothing leaves.

**Sample loading barely exists.** One `AudioFormatManager`, local to
`SampleMatcher.cpp`. Loading drum samples needs real infrastructure: decode,
resample to the host rate, trim, store, serialise.

---

## What an 808 actually is

Twelve to sixteen voices, each a fixed analogue circuit, with **three controls
each**: Level, Tune/Tone, Decay. The restraint is the instrument's character, not
a shortcoming, and copying the restraint matters more than copying the circuits.

| Behaviour | Why it is not optional |
|---|---|
| **Choke groups** | Closed hat cuts open hat. Without it, hats do not sound like hats |
| **Accent** | One global per-step accent lifting everything on that step. `StepData` already has `accent` and `velocity` |
| **Three knobs per voice** | Level, Tune, Decay. The discipline is the point |

Voice construction, for reference: the kick is a pitch-enveloped sine; the snare
is tone plus noise balanced by "Snappy"; hats and cymbal are six square
oscillators through a bandpass, differing only in decay and choke.

Sample slots sit alongside rather than instead of these. Each slot is *either* a
synthesised voice *or* a loaded sample, and Level / Tune / Decay / Pan map onto
both — which is how every modern 808 descendant works.

---

## Core effects, and why these ones

**Per voice** — cheap, and where drum character is actually made:

- **Filter**, high and low pass. The most-used drum control after level.
- **Drive**, reusing `MasteringChain::saturateSample`. The 808 sound *is* a
  saturated 808.
- **Bit crush / sample rate reduction**. The one genuinely characterful cheap
  addition.

**Two sends**, one instance each, shared by all voices: **reverb** and **delay**.
Per-voice reverb multiplies cost and wash for no benefit — the same reasoning
`MasterEffects.h` already gives for keeping reverb off the voice path.

**Output**: reuse `MasteringChain`. `MasteringPanel` drops in unchanged because
it takes a `mastering::Controls` interface rather than a processor.

Transient shaping is deliberately left out of a first version. Per-voice attack
and decay cover most of what it would buy.

---

## Where it lives

**Inside WaveLathe**, as a second pattern in the existing sequencer, rather than
as a third plugin.

A separate plugin was the first recommendation, on parameter-budget grounds. The
argument against it is sync: two standalones have no host clock between them and
cannot lock together. In a DAW it is a non-issue; standalone, it is fatal to the
thing being built. One app, one project file, one transport is a better
instrument than three plugins that have to be wired together.

The cost of that choice is the parameter cap, and it is not avoidable — see
below.

### A view switch, not a mode switch

`SequencerState` gains a `DrumPattern` — 16 lanes × 64 steps of on/off, carrying
the `accent` / `velocity` / `probability` vocabulary `StepData` already has —
alongside the existing melodic pattern. They share transport, length and
division.

The tab switches **which pattern you are editing, not which one is running**.
Both play. That is how a groovebox behaves, it is barely more code than a mode
switch because the drum pattern is a separate structure either way, and it lets
a bassline and a beat play together, which a mode switch forbids.

### The MIDI channel

Worth having, but it is routing rather than a solution. A separate channel lets
an external controller play drums while the keyboard plays the synth, and
channel 10 is a convention people already know. It does not make the sequencer
polyphonic — that is `StepData`, one level up.

---

## Why the cap raise cannot be dodged

The mastering chain avoided the 64-parameter cap by leaving its twelve dials
**unregistered**: saved with a preset, invisible to a host. That trick does not
work here.

An automation lane is keyed by registry id — `LaneData { int parameterId; }`.
**An unregistered parameter cannot have a sequencer lane.** Drum voice levels
would be unsequencable by the very sequencer they are being connected to, which
defeats the purpose.

Sixteen voices × four controls is 64 parameters before a single send exists.
There is one slot free.

---

## Phases

1. ~~**Raise the cap to 128.** On its own, first, with the sequencer suite as the
   net. See [ParameterCap-128.md](ParameterCap-128.md). Nothing else starts
   until this lands.~~ **Done**, and raised again to 192 in 0.44.0 when every
   drum dial was registered - the same doc records both.
2. ~~**`DrumPattern` and the view switch.** Data structure, preset v17, the grid
   UI. No sound yet.~~ **Done.** 19/19, `DrumPatternTest` added, and the grid
   was driven by hand to check it draws what it stores. Two things it leaves
   behind, both deliberate:

   - **No ruler on the drum grid.** Every fourth column is shaded, which gives
     the pulse, but there are no bar numbers - so scrolled out to step 40 you
     cannot say which bar you are on. The note grid has one. Worth adding, and
     it costs the rows some height.

### What automation does when drums arrive

Worth settling now, because the obvious answer is wrong in an interesting way.

**Drum automation needs no new mechanism.** A lane is keyed by registry id, and
drum voice controls will be *registered* parameters - that is precisely what
raising the cap to 128 was for. The moment a voice's Level is a registry entry,
the existing strip records it, draws it, saves it and plays it back with no code
that knows it is a drum. Nothing "switches over".

**What does need deciding is whether the strip filters by view.** Sixteen voices
times four controls is sixty-four automatable parameters. A project that
automates a few drum levels would put those lanes in the same strip you are
looking at while editing a bassline, and the reverse: filter cutoff lanes
cluttering the drum page. Three options, none obviously right:

- **Show everything** - one timeline, nothing hidden. Honest, and unusable once
  a dozen lanes exist.
- **Filter by view** - the drum page shows drum lanes. Tidy, but a lane you
  cannot see is a lane you forget is recording.
- **Filter with an override** - filtered by default, with a way to see all.

This is a phase 5 decision, since that is when drum controls get registered.
Recorded here so it is a choice rather than whatever falls out.

**Decided in 0.46.0: filter with an override.** It was not decided in phase 5,
and it fell out as "show everything" - which the paragraph above predicted and
which stayed tolerable until 0.44.0 made all 108 drum dials automatable. The
strip now shows the lanes for the grid that is up, and **Show all** brings back
the rest.

The objection to filtering is answered rather than accepted: the button counts
what it hides ("Show all (3 hidden)"), and an empty strip over hidden lanes says
how many there are. Clear removes what is shown and only that - reading "Clear
All" only when all is in view - because a filter that made it easy to delete
lanes you were not shown would be worse than no filter. The strip, Clear and
`SequencerTest` all ask `LaneFilter.h`, so what is drawn and what is cleared
cannot disagree.

### 5a. The mixer, and what the budget actually allowed

The plan's arithmetic broke here, and the break is worth recording.

Sixty-three synth dials plus sixteen voices times four controls is 127 of the
128 the cap allows. That part worked. But phase 5 also wants per-voice filter,
drive and bit-crush plus two sends — another eighty — and *that* does not fit
under any cap short of 256, which would put around two hundred parameters in a
host's automation list.

**Decided: the mixer is registered, the per-voice effects are not.** Level,
Tune, Decay and Pan are what anybody automates; a bit-crush amount is set once
and left. The effects will be saved with the project and editable on the page,
invisible to the host — which is exactly the trick the mastering chain already
uses, and the reason its twelve dials never needed a registry slot.

*(Half of this was revised in 5b below: Tune and Pan turned out to be the two
reached for least, and gave their registry slots to the two sends. The shape of
the decision — four registered per voice, effects unregistered — stands.)*

**Where the sixty-four live.** One flat `std::array` in `SynthParameters`, not
sixty-four named members. Named members would have meant three hand-written
lists of sixty-four that all had to agree — the members, the registry rows
pointing at them, and the copy into `drums::KitParameters` — and three lists of
sixty-four is three chances to put the cowbell's decay on the clave. The array
makes all three loops over one index function. The cost is that a registry
`Descriptor` can no longer just be a pointer-to-member, so it grew a second
storage kind and a `valueOf()` that is the only thing which knows the
difference.

**Units are the host's, not the kit's.** The registry holds Tune in semitones
and Pan from left to right, because a DAW showing "-3.0" beats one showing
"0.38". `updateDrumParameters` is the single place that converts to the kit's
0-1.

**The mixer travels with the project, not the preset.** Same line the sequencer
settings are on: picking "Bright Lead" over "Warm Pad" should not retune your
kick. Preset v19, appended at the **tail** rather than the header — tried the
header first, which broke the byte-exact downgrade helpers in
`DrumPatternTest`, because those patch a file's tail and sixty-four floats had
appeared in the middle of it.

**Two things the sixty-four broke on the way in:**

- **MIDI learn ran out of controllers.** `MidiMapTest` mapped id → `id % 128`,
  which was fine until the registry passed 120 — after which the last seven
  ids landed on CC 120-127, the Channel Mode range, which the map refuses on
  purpose. The product was right and the test was asking for a controller that
  does not exist. Worth knowing generally: there are 127 parameters and 120
  assignable CCs, so a single map cannot reach them all at once.
- **A dial at dead centre read "+0.1 st".** A `ParameterKnob` has 128
  positions, so over two octaves one step is 0.19 of a semitone and the centre
  sits 0.09 off zero — wrong, and impossible to correct, since there is no
  position in between. The readout's dead zone is now half a step wide rather
  than an arbitrary small number.

### 5b. Sends, a shared rack, and which four the host gets

5a registered Level, Tune, Decay and Pan on the reasoning that those are what
anybody automates. Half of that turned out to be wrong, and the correction came
from the person using it: **Tune and Pan are the two reached for least.** A
drum's tuning is set once while building a kit; where it sits in the image is
set once and left. What a track actually wants moving under it is the fader and
the routing.

So the four registered per voice are now **Level, Decay, Send 1 and Send 2**.
Tune and Pan are still controls, still saved, still on the panel — they simply
lost the argument over which four of six the host sees. The arithmetic is
unchanged: thirty-two out, thirty-two in, still 127 of 128.

**A send picks a destination, not an amount.** Positions 0-4: Off, Reverb 1,
Dist, EQ, Delay. The amount lives on the unit, as its return level. Sixteen
per-voice send amounts would have been another sixty-four parameters and there
is room for none of them — and a shared effect with per-unit returns is what a
mixer with four sends is anyway. The cost is honest and worth naming: two
voices sharing a reverb cannot have different amounts of it.

**The rack is four units of three dials, unregistered.** Reverb 1, Distortion,
EQ, Delay, down the right-hand side of the Drums page. Twelve dials saved with
the project and invisible to the host — the same trick the mastering chain
uses, and the only option left at 127 of 128.

**Where the units live.** `Source/Drums/DrumFx.h/.cpp`, inside the drums
library rather than reached for from `juce_dsp`, which that target deliberately
does not link. `juce::Reverb` is the one exception and it is not one: it lives
in `juce_audio_basics`, already a dependency. The other three are a tanh
waveshaper, a pair of one-poles split into three bands, and a circular buffer.

**A rack is not a chain.** `MasterEffects` and `MasteringChain` both run
everything through everything in a fixed order. These four sit side by side and
hear only what is sent to them — which is the difference between an insert and
a send, and the reason four effects cost twelve dials instead of sixty-four.

**What the sends cost the render path.** A voice can no longer be rendered
straight into the mix, because once it has been summed there is nothing left to
take a copy of. Every active voice now goes through a scratch buffer and is
added on twice over: to the mix, and to each bus it is routed to. Post-fader,
which falls out of the order and is also what a mixer means — a fade takes its
reverb with it.

**A unit keeps running after its last send stops.** Six seconds, counted in
samples. "The bus is silent" is not the same question as "nothing is being
sent here": a reverb whose last send was just switched off still has three
seconds of tail owed, and a unit that stopped the moment nothing fed it would
cut that dead. The same six seconds is added to `getTailLengthSeconds` when any
send is routed, so a bounce does not clip the tail — added rather than maxed,
because the reverb starts when the hit arrives and finishes after it.

**Preset v20 is a block of its own, not a wider v19.** The mixer grew from four
controls a voice to six, so the obvious move was to widen v19's sixty-four
floats to ninety-six — which would change what the bytes at a given offset mean
in files that already exist. Every version here is additive precisely so that
never happens. v19's sixty-four are still v19's sixty-four in v19's order; the
two sends and the rack are appended past them. The cost is that one array is
written in two pieces.

**Three things this got wrong on the way in, worth recording:**

- **A test claimed a kick stops sounding after a second.** It does not — the
  decay is 0.42s and a voice is cut after six of them, so it is still going at
  2.5. The first send test measured in the wrong window and failed against
  correct code. Past the cut the dry path is *exactly* zero, which turned out
  to be a far better place to measure from.
- **Testing the sends against the reverb alone proved almost nothing.**
  `juce::Reverb`'s shortest comb is about 1100 samples, so a reverb wrongly
  reset at every segment boundary simply outputs silence at any block size the
  suite uses — and silence matches silence. The mutation survived. The
  block-size identity test now routes one voice into each of the four units;
  the drive and EQ are one-poles that move every sample and cannot agree
  across two segmentations unless they really are continuous.
- **A reverb Size of 1.0 is not a large room, it is a stuck one.** At the top
  of `juce::Reverb`'s range a hit is as loud three seconds later as it was dry.
  The dial maps to 0.2–0.92 instead.

**Still open:** the two sends can both point at the same unit, which sends the
voice there twice. Deliberately not guarded — it is the one thing here with no
musical use, and the guard would cost a comparison per voice per block to
prevent a mistake that is instantly audible and instantly undone.

### 5c. The sends become a chain, and the amounts take the MIDI slots

Two corrections from use, both of which reversed a decision in 5b.

**The selectors came off the host's list and the amounts went on.** 5b put the
selectors there on the reasoning that routing is what a track wants moving. It
is not: *which* effect a drum runs through is set once while building a kit,
and *how much* of it there is is a fader. So the four registered per voice are
now Level, Decay, Amt 1 and Amt 2. Still 127 of 128 — the swap is even.

**The two slots became a chain, not two parallel sends.** A voice goes through
slot 1's unit and then through slot 2's, and each Amount is the wet/dry mix of
that stage, defaulting to 0 — which is exactly the input, bit for bit, so an
unused chain provably costs nothing.

**That forced private processors, and the cost is real.** Slot 2 needs slot
1's output *for this voice*, and a bus carrying all sixteen summed together
cannot give it that. So every voice owns a copy of all four units and the
rack's twelve dials are shared *settings*. Two consequences worth naming:

- Two drums in "Reverb 1" are now in two identical rooms rather than the same
  one. A shared room is what a send bus buys, and a send bus cannot be chained.
- Sixteen private delay lines is real memory, so they are one second each —
  a quarter note at 60bpm. The Time dial is limited to a quarter and shorter
  to match, because offering "1/1" on a dial that would be clamped to a
  quarter of it is offering a setting that lies.

**Preset v21** appends the amounts in a third block. Three blocks for one
array is a cost; a single block whose layout shifts with the code would make
every saved project a guess about which build wrote it.

### 5d. Attack, and why it is per voice

Asked for as "attack and decay ... at least for those that need it? I think
some do" — and the second half is the interesting part, because it is true.

**Every strip has the dial; how far it reaches is the drum's own.** A new
`VoiceSpec::maxAttackSeconds` column: 600ms on the crash, 400 on the ride, ten
on a rim shot. So full Attack on a crash is a swell and full Attack on a rim
is a nudge, and the readout says so in real milliseconds rather than in a
percentage that would mean something different on every row. Roughly a third
of each voice's own body, with the cymbals given more because a swell is a
thing people actually want from them.

A ragged grid — some strips with an Attack dial and some without — was the
other option and is worse: sixteen columns that do not line up are harder to
read than one dial that is honest about its own range.

**Unregistered, at zero by default.** There is one free registry slot and
sixteen attacks need sixteen, so it joins Tune, Pan and the selectors as saved
but not automatable. Zero everywhere means a kit nobody has touched sounds
exactly as it did before the control existed — and the test for that is
`identical`, not "close".

**A straight ramp, worked out from `elapsed`.** Straight because over the few
milliseconds this is usually set to, no curve is distinguishable from a line.
From the sample counter rather than accumulated per block because the whole
suite rests on a chopped render matching a long one exactly, and an
accumulating ramp drifts.

**Preset v22**, one float per voice.

### 5e. The click on a retriggered kick

Reported from use: *"a little clicking ... usually when i hit a drum while its
still playing a previous sound. even with attack in use. Only noticing it on
the Kick and Toms."* Every clause of that turned out to be diagnostic.

**Kick and Toms are the same engine** — `bassDrum`, whose body is one low
sine. `Voice::trigger` replaces the whole of a voice's state, oscillator phase
included, so retriggering stepped the output from wherever the old hit's sine
happened to be straight to where the new one starts. On a hat that step hides
inside noise; on 52 Hz it is the only discontinuity in the signal.

**"Even with attack in use" is the giveaway, not a second bug.** Attack shapes
how the NEW hit arrives. Nothing it can do smooths how the OLD one stopped.
Any fix had to be on the ending.

**The fix is a third fade.** The end fade covers a voice finishing, the choke
covers one being cut by another, and neither had ever seen a voice interrupted
by *itself*. `trigger` now carries the last emitted sample across the join and
lets go of it over 2 ms, so the first sample of the new hit is exactly the last
sample of the old one and the waveform is continuous. The choke multiplies it
too, so that cutting a voice cuts all of it.

**Measured as the biggest one-sample jump**, because that is what a click is —
it is quiet against a drum, so loudness will not find it and RMS averages it
away. Against the jump a hit makes starting from silence, which is the honest
baseline: a fresh kick onset is genuinely fast and "no jump at all" would be
the wrong thing to ask for.

| | before | after | fresh onset |
|---|---|---|---|
| Kick | 0.168 | 0.025 | 0.022 |
| LoTom | 0.239 | 0.014 | 0.017 |
| MidTom | 0.070 | 0.023 | 0.023 |

The test was written first and failed on three of the four voices before
anything was changed, which is a better guarantee than any mutation.

**What the mutation testing found here:** loosening the v22 read guard to
`>= 21` breaks nothing — a v21 file reads four bytes past its end per voice,
gets zeros, and zero is the right answer anyway. The guard is kept because it
stops being harmless the moment a v23 block goes behind it, and because
tightening it to `>= 23` fails immediately, which is the direction that
actually corrupts a load. Recorded rather than quietly passing: a guard whose
absence no test can see is worth knowing about.

### 2a. Sub-step cells and nudge (preset v18)

Added after the first version was tried by hand. Two separate things that get
confused with each other:

- **A step now holds `project::drumSubdivisions` cells** (two), so a row is 128
  cells and a hit can land on the 32nd between two 16ths. This is *placement* -
  and because both cells exist independently, it is also what makes a roll or a
  flam possible at all.
- **Every hit carries a `nudge` in ticks**, signed, +/-3. This is *feel*: the
  cell says which 32nd a hit belongs to, the nudge says how tightly it sits on
  it. Signed because pushing early is half of what makes a groove, and the
  melodic pattern's late-only `tickOffset` cannot express it.

`maxDrumNudge` is `ticksPerDrumCell / 2 - 1`, not `/ 2`. At exactly half a cell,
cell N pushed fully early and cell N-1 pushed fully late land on the **same
tick**, and a hit's position would no longer say which cell it came from. A test
asserted the property, failed, and the `- 1` is the result.

Drawn where it will fire rather than where its cell is, with a tick mark
underneath showing the cell - otherwise a nudged row is a wobbly line of blocks
with nothing to read the wobble against.

Drawing is still a plain click or drag. **Shift-drag** a placed hit to nudge it;
right-click still toggles accent.
   - **Undo is untested for drums.** It should work for free, because undo
     snapshots the same `SequencerState` that now carries the pattern, but
     "should work for free" is exactly the claim worth a test rather than a
     sentence.
3. ~~**`DrumVoice` synthesis** — kick, snare, hats with choke, clap, tom.
   Headless and measurable, the way `MasteringChain` was.~~ **Done.** 20/20,
   `DrumVoiceTest` added, and four deliberate mutations confirmed it has teeth
   before it was believed. See below for the shape it took.

### 3a. What the kit turned out to be

**A library, not more source files.** `Source/Drums/` builds as `DrumKit`, and
`DrumVoiceTest` links that and nothing else — so the day the kit reaches back
into `SynthParameters` or `StepSequencer`, the suite stops building. Same trick
as `MasteringChain`, for the same reason. It does not even link `juce_dsp`: the
one filter the voices need is written out, which costs a dozen lines and buys a
library anything with `juce_audio_basics` can pick up.

**Six engines, sixteen rows.** An 808 is sixteen fixed circuits and most of them
are the same circuit with different components: kick and the three toms are one
design, the four cymbals and the cowbell are another. So the code is six engines
(`bassDrum`, `snare`, `clap`, `metal`, `tone`, `shaker`) and a table of
constants, and adding a drum is adding a row rather than writing a function.

**Hits carry a sample offset, and the renderer splits the block at each one.**
This is the part phase 2a made non-negotiable. Having just given every hit a
signed nudge measured in ticks, a renderer that started hits on block boundaries
would quantise all of it away — silently, and only at some buffer sizes. The
choke fires at the split too, not when the hit was queued: at a 512-sample
buffer, choking on arrival would cut an open hat up to twelve milliseconds
early, and by an amount that depended on the host's buffer setting.

**Which controls a ringing hit still listens to.** Tune and Decay are read once,
at the hit, and held: turning Tune while a crash rings should no more retune it
than turning a tuning peg retunes a stick already in the air. Level and Pan are
read every block, because a level automation lane drawn across a two-second
cymbal has to be heard doing something. The test holds both halves.

**Snappy is a constant, not a dial.** Sixteen voices times four controls is
sixty-four, which is exactly what raising the registry cap to 128 left over the
sixty-three the synth already has. A fifth control per voice does not fit — and
between Snappy on one voice and Pan on all sixteen, Pan wins.

**There is a way to hear it.** `DrumRender out.wav [bpm] [bars]` writes a bar of
drums with every voice at its default. A tool rather than a suite, named so
`run-tests.ps1` leaves it alone, alongside `WavAnalyzer` and `MatchProbe`. It
renders in 512-sample blocks, which the identity test proves is the same as one
long call — so it is a fair rehearsal of how a host will play it rather than a
different thing that happens to sound similar. Worth reaching for in phases 4
and 5, when a sample slot or a per-voice filter needs judging by ear.

### 3b. Wiring it to the sequencer

Done in the step after phase 3, and the shape of the seam is the interesting
part.

**The sequencer hands out hits; it does not strike drums.** `process` fills a
list of `DrumHit { voice, velocity, accent, sampleOffset }` and the processor
carries them to the kit. Three reasons, and the third is the one that paid off:

- A sequencer that called into the kit would drag the DSP into every target
  that compiles `StepSequencer.cpp` — five suites with no interest in it.
- The kit stays a library that knows nothing about patterns.
- **"Does a hit nudged three ticks early land at the end of the bar before it"
  becomes a question about integers.** `SequencerTest` answers it without
  rendering a sample of audio, and it does answer it: at 120 bpm a hit on cell
  0 nudged −3 is reported at sample 21017, which is tick 61 of a 64-tick
  pattern, not sample 0.

**At most one cell can fire on any tick**, so the per-tick scan is over sixteen
voices rather than over the grid. That is `maxDrumNudge` being `n/2 - 1` rather
than `n/2` paying for itself a second time: because a nudge cannot span half a
cell, the tick names its own cell and there is nothing to search.

**The drums sit after the synth's effects and before the mastering chain.**
Both halves deliberate. A beat washed through the pad's reverb is not a mix
decision anybody made, and the drums get sends of their own in phase 5. The
mastering chain is the mix bus and has to catch everything, or its limiter is
holding a ceiling the kit walks straight through.

**Clicking a row's name plays that voice.** The gutter was dead space, and
until phase 5 gives the kit a mixer it is the only way to hear a voice on its
own. It crosses threads — the editor sets a bit in an atomic, the audio thread
drains it — because `Kit::trigger` writes a queue the audio thread is reading
and the message thread must not touch it.

**The host is told about the drum tail**, but only when the pattern uses a
drum. A crash at default Decay rings for 12 seconds, and putting that on the
end of every render of a project with no drums in it would be a tax on
everyone for the benefit of nobody.

Verified by hand as well as by test: a pattern drawn on the grid, Play pressed,
output at −7.1 dB with the playhead moving; transport stopped, −inf; one click
on the Crash row name, −7.0 dB with the transport still stopped.

**What it leaves behind:**

- **The kit has no mixer.** Every voice is at its default Level, Tune, Decay
  and Pan, and nothing in the interface moves them. That is phase 5, and it is
  what the cap raise to 128 was for.
- **`Perc 1` and `Perc 2` are stand-ins.** They are where a loaded sample goes
  in phase 4; until then they are synthesised so the rows are not silent.
- **The metal engine aliases, on purpose.** The 808's oscillators are square and
  its cymbals are what comes out of a narrow band around them. Band-limiting
  would take the grit out of the one sound whose identity is grit.
4. **Sample slots** and a real `SampleLoader`.
5. **Per-voice effects** — filter, drive, bit crush. Two sends.
   Started: **5a, the mixer and the Drums page, is done.** See below.
6. **MIDI channel routing** — drums on their own channel, General MIDI note map.

Phases 2 through 4 are testable without any user interface, which is what made
the mastering work go smoothly.

---

### 4. Sample slots

**Any voice, not just Perc 1 and Perc 2.** The plan already said it — "each
slot is *either* a synthesised voice *or* a loaded sample" — and it is the
more useful reading anyway: loading your own kick onto Kick is the first thing
anybody tries. The two stand-ins stop being special and become two more rows.

**The library learns what audio is, not what a file is.** `drums::Sample` is
floats, a rate and a name; `drums::SampleBank` is sixteen pointers, published
to the audio thread as one pointer the way the wavetables are. Decoding lives
in `wavelathe::drumsamples`, which links `juce_audio_formats` — the drums
library still links neither that nor anything else that ties it to this synth,
and `DrumVoiceTest` tests the playback with a sample it builds in memory.

**Mono, and that is the one lossy thing the loader does.** A kit voice has a
Pan control, and Pan is what you give a mono source. A stereo slot would mean
making the voice, the chain and the panner stereo for a gain nobody asked for
on a drum hit — and it halves both the memory and the size of a project.
Summed with a 1/channels scale, so the stereo version of a hit is not louder
than the mono one.

**No resampling on the way in.** The file keeps its own rate and the playback
ratio is `sourceRate / hostRate × tune`. That ratio is the interpolation Tune
needs anyway, so pre-converting would be doing the same work twice — and a
project moved between a 44.1k session and a 96k one is right without having
been converted. Four-point Hermite rather than linear, because linear is a
lowpass whose corner moves with the ratio and on a tuned-down hat that is the
whole sound.

**Trimmed at both ends.** Leading silence is a timing bug: a file with twenty
milliseconds of it plays twenty milliseconds late on every step, which sounds
like the groove rather than like a fault. Threshold is -60 dB below the file's
own peak with an absolute floor at a 16-bit LSB, and a little run-up is kept
so the transient is not cut into.

**What the existing dials mean for a slot.** Level, Pan, velocity, accent,
choke and the FX chain all work unchanged — they are downstream of what made
the sound. Tune is the playback ratio. Attack reaches half the recording or
half a second. Decay is the same third-to-three law **clamped at the whole
recording**, so anything from the centre up plays the sample as loaded and the
bottom half gates it shorter. The top half of Decay does nothing for a sample,
which is worth saying out loud: you cannot make a recording longer than it is.

**Preset v23 embeds the audio.** Referenced-by-path loses the argument in one
line — a project you send to somebody else has to still play. Sparse, so a
project with no samples pays four bytes. Floats rather than 16-bit, because a
format that requantises every time a project is reopened degrades work; the
size is bounded by the loader's ten-second cap instead. Measured: 6982 bytes
for 1733 frames across two slots, which is the audio plus fifty bytes.

`PresetValues` holds `std::shared_ptr<const Sample>`, not the audio. Undo
snapshots a whole `PresetValues` every time a dial is touched, so a deep copy
there would make turning a knob cost however many megabytes are loaded.

**Loading is right-click on the strip header.** No room for another control —
a strip is a sixteenth of the page wide and every pixel of its height is a
dial. The row keeps its own name whatever is loaded, because the grid on the
Sequencer page still calls it that; the header tints and the tooltip says what
it holds.

**One gap the tests nearly had.** Every sample-playback check used a recording
made at exactly the rate the test renders at, where the ratio is 1 — so a
playback path that ignored the file's rate entirely would have looked perfect.
That is precisely the risk named at the bottom of this document before any of
it was written. There is now a half-rate case, and deleting the rate from the
increment fails it.

### Phase 4b — choosing a sample by ear, and a lamp per voice

**The dialog had to stop being the OS one.** A sample library is a folder of
names that all describe the same drum, and a native file dialog cannot say what
has been highlighted — so choosing one meant loading it, listening, and opening
the dialog again. JUCE *will* host a preview component inside the Windows
native dialog, but only by falling back to the pre-Vista `GetOpenFileName`:
no places sidebar, no search, no recent folders. That is the wrong trade for
somebody working a library, so `SampleBrowser` uses JUCE's own browser.

**The preview is the real thing.** Moving through the list loads the sample
onto the voice, so what you hear is that pad — Level, Tune, Attack, Decay, Pan
and both sends — and in the groove if the pattern runs. Load keeps it; every
other way out puts the slot back. The preset browser's bargain, applied to
files. Debounced 140 ms so holding an arrow key scrolls rather than decoding
every file it passes.

**Previewing per keypress broke the retention bargain.** A voice holds a raw
`Sample*` for a whole hit, so a displaced sample cannot be freed on the spot,
and the answer had been to keep every sample the session ever saw. Fair when
loading one is deliberate; a leak when arrowing a folder loads thirty.
`drums::RetiringStore` holds a displaced one for a minute — past the worst
case of ten seconds of recording at the slowest Tune plus six of reverb tail,
so twenty-six — then drops it. A `static_assert` in `DrumKit.cpp`, the one
place both constants are visible, holds the sixty to that arithmetic.

**The lamp is metered after the sends, not before.** That is the only reading
worth having: a hit at a sensible Level driven into the distortion clips
*there*, and a pre-send meter would call it fine. Proven by a pair — the same
hit dry and driven — which reads 0.530 against 0.200 and would read 0.530
twice if the meter moved above the chain. It is lower driven, not higher,
because the drive compensates its own gain at 1/√gain.

**Two things only measurement caught.** The lamp's first version reached
(158,160,166) against an unlit (43,49,61) — a mid grey, called white because
the code said `white`. A gain of 1.5 over the square root fixed it, and the
numbers came from probing the pixel rather than looking at a screenshot. And
undo restored the parameters and the pattern but not the slots, because the
editor's restore path never called what `applyProject` called: "Undid kick
sample" on the display, sample still on the pad. Both paths go through
`restoreDrumSamples` now.

### Phase 4c — eleven drums and a blank, and a slot that can be any of them

**The count was never checked against the machine.** Sixteen rows went in on
the reasoning that four spare ones could hold what people reach for anyway. An
808 - and the RD-8 that clones it - has ELEVEN instrument channels. The four
extra were width, height and sixteen grid cells apiece spent on rows nobody was
filling. Twelve now: the 808's eleven and one blank for a sample.

**Slots 0-10 were already exactly that eleven**, which is what made this
affordable. Kick, Snare, Rim, Clap, LoTom, MidTom, HiTom, Cowbell, CHat, OHat,
Crash is the RD-8's set with Crash for Cymbal. Only the twelfth changed meaning
(Ride became the blank) and 12-15 came off. Reordering them to match the
hardware's front panel was considered and rejected: it reads better left to
right and it would change what every saved pattern and every automation lane
already means, which is a bar playing its claps as toms.

**Nothing was lost, because a sound stopped being a position.** `specOf(slot)`
became `engineSpec(engine)`, and each slot names the engine it wants. Sixteen
drums on twelve slots, two slots allowed to hold the same one. That is the
808's Tom/Conga toggle generalised - one channel, two sounds, a switch - and it
means the engine table can grow without costing a row, a strip, four host
parameters and a column of grid per entry.

**The choke had to follow the drum.** It read `specOf(v).chokeGroup` by slot
index, which was the same number while a slot was its sound. Now a closed hat
cuts an open one wherever they sit, and the hat ROWS choke nothing once the
hats are moved off them. The slot-indexed version passes the first half of that
and fails the second, so the test asserts both.

**A blank slot is refused before engineSpec is asked.** That function hands
back the kick for anything out of range, which is the right answer to "give me
a spec" and a kick drum on an empty twelfth row otherwise. One `if`, and a test
that strikes the blank slot and demands exact silence.

**Preset v24 is the first version to change a block's SHAPE.** Every version
from v19 to v23 appended, so the downgrade helpers could make an old file by
cutting blocks off a new one. This changes how many times the v19-v22 loops go
round, so the count is frozen per version (`v19DrumVoices`) exactly as the
control lists are. Old blocks are now BUILT at sixteen voices in the test
rather than stripped down from twelve - a stripped v24 file is a v21 file with
a quarter missing from every block, and the reader correctly swallows the next
block trying to find what it was promised.

**Worth recording: the v19-only case passed while v20 and v21 failed**, which
read as a reader bug and was not. A v19 file has one block, so its over-read
ran off the end of the file and returned zeros nobody was checking. A test that
passes for that reason is not testing what it says.

**Dropped rows are named, empty ones are not.** A project from the sixteen-row
kit has rows for Clave and Maraca whether or not anybody touched them, so
reporting every out-of-range row would cry wolf on every old project. Only rows
with hits, and samples, are reported.

## Risks worth naming before starting

**Sample memory and serialisation.** A project carrying eight samples is a far
bigger file than one carrying a wavetable. Decide early whether samples are
embedded or referenced by path: embedded survives being sent to somebody else,
referenced does not.

**Sample rate.** Samples load at their own rate and must be resampled to the
host's, or everything is detuned. Cheap to get right at the start, expensive to
retrofit.

**Scope.** Sixteen voices, times sample loading, times a pattern engine, times
effects, is larger than the entire mastering effort was. Phase 3 alone is a real
piece of work.
