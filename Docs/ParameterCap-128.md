# Raising the parameter cap from 64 to 128

The prerequisite for [the drum machine](DrumMachine-Plan.md), and for anything
else that ever needs more than the one registry slot currently left.

It buys nothing visible — no feature, no sound, no interface. It is the tax on
the work that follows, not a part of it, and it is worth saying that out loud
before spending a day on it.

---

## Why it cannot be avoided

`paramreg::maxParameters` is 64 because a set of touched parameters has to fit
in one `std::atomic<uint64_t>`, which the audio thread reads to ask "is a hand
on this dial right now?" without taking a lock.

The mastering chain dodged the cap by leaving its twelve dials unregistered —
saved with a preset, invisible to a host. **That does not work for anything the
sequencer must automate.** An automation lane is keyed by registry id
(`LaneData { int parameterId; }`), so an unregistered parameter cannot have a
lane at all.

---

## Every site

| File | What | Change |
|---|---|---|
| `ParameterRegistry.h:52` | `maxParameters = 64` | → 128; it had no comment, and now has one |
| `StepSequencer.h:172,177,185` | three `atomic<uint64_t>` masks | → `AtomicParameterMask` |
| `StepSequencer.h:194,199` | `releaseFromRecording(uint64_t)`, `heldParameters()` | carry `ParameterMask` by value |
| `StepSequencer.h:168` | `array<Lane, maxParameters>` | resizes itself — see memory |
| `StepSequencer.cpp:261,274,296,304,571,596` | six `1ull << id` | **the dangerous ones** |
| `StepSequencer.cpp:312` | `~0ull` | every id at once |
| `StepSequencer.cpp:334` | `~parameterMask` | whole-mask complement |
| `StepSequencer.cpp:590` | `if (touched == 0)` | whole-mask test |
| `StepSequencer.cpp:741,760` | two `latchedParameters.store(0)` | latch reset |
| `PluginProcessor.h:234` | `hostWroteParameters` | → `AtomicParameterMask` |
| `PluginProcessor.cpp:545,649` | `exchange(0)`, `fetch_or(1ull << id)` | drain and set |
| `PluginProcessor.h:229,262` | two arrays sized by the cap | resize themselves |
| `PluginEditor.h:111` | `parameterIsMapped` | resizes itself |
| `FxpPreset.cpp:47` | `maxSavedLanes` | resizes itself |
| `SequencerTest.cpp:1031` | *"fits in one touched-bit word"* | the premise being deleted |

Six files with real edits; six more where a constant follows along.

The table above was written from a read-through and was two sites short — the
`~0ull` in `clearAllLanes` and the two `latchedParameters.store(0)` at the
edges of a recording pass. Doing step 2 began with `grep` rather than the
table, which is the only reason they were not missed twice. **Grep before each
step; this table is a map, not an inventory.**

---

## The one bug that will actually bite

```cpp
uint64_t bit = 1ull << (uint64_t) parameterId;
```

**On x86, `1ull << 64` is `1`, not `0`.** The shift count wraps.

So the moment parameter 64 exists, touching it silently touches **parameter 0** —
the `Wave` dial. 65 hits 1, 66 hits 2. It is undefined behaviour, it produces no
warning, it does not crash, and it presents as an inexplicable bug in the
oscillator section.

All seven shift sites have this, and it lives entirely in ids 64–127 — which do
not exist until the last step of the work, which is why the ordering below
matters.

---

## Design

Two types, because the code snapshots a mask and then loops over it.

```cpp
// A plain value: what heldParameters() returns and callers loop over.
struct ParameterMask
{
    static constexpr int words = (paramreg::maxParameters + 63) / 64;
    std::array<uint64_t, words> w{};

    bool test(int id) const;          // id / 64, id % 64 - never a bare shift
    bool any() const;
    ParameterMask operator|(const ParameterMask&) const;
    static ParameterMask forParameter(int id);
    static ParameterMask all();       // what ~0ull used to mean
};

// The storage. The same three masks, just wider.
class AtomicParameterMask
{
    std::array<std::atomic<uint64_t>, ParameterMask::words> w{};
public:
    void set(int id, bool on);
    void merge(const ParameterMask&);
    void andNot(const ParameterMask&);
    void clearAll();
    ParameterMask load() const;
    ParameterMask exchangeZero();
};
```

`words` derives from `maxParameters`, so 256 later is one constant rather than
another refactor. The point of wrapping it at all is that **no bare shift
survives** anywhere in the codebase.

---

## Concurrency: less risk than it appears

The comment on the mask claims a single atomic load gives a consistent snapshot.
**That is already untrue:**

```cpp
uint64_t heldParameters() const
{
    return touchedParameters.load() | controllerTouched.load() | latchedParameters.load();
}
```

Three separate loads. The code already tolerates the three being read at
slightly different instants. Widening each to two words makes it six loads
instead of three — the *class* of inconsistency does not change, only the
degree.

The consequence is bounded. `applyAutomation` snapshots once, then decides per
id whether the lane plays or a hand is on the dial. A torn read means one lane
plays or does not **for one tick**, on a control somebody is physically moving.
Inaudible, and corrected on the next tick.

The drain in `processBlock` becomes two `exchange(0)` calls. A bit set between
them is picked up next block — a one-block delay on a controller hold that
already runs down over thousands of blocks.

No new locks, no new allocation, no change to the spin-lock usage.

---

## The cost nobody expects: memory

`Lane` holds an **inline** `std::array<float, maxTicks>` — 1024 floats, about
4.1 KB.

| | 64 | 128 |
|---|---|---|
| Per `StepSequencer` | ~256 KB | **~512 KB** |
| `SequencerTest`, ~30 as locals | ~5 MB | **~10 MB** |
| Its stack (`/STACK:16777216`) | 16 MB | 16 MB — tight |

### What this section got wrong

It said the inlining was "deliberate and correct" because `recordAutomation`
creates lanes **on the audio thread**, where a `std::vector` would allocate,
and that it should not be "fixed".

**That was a misreading of our own code.** Nothing ever creates a `Lane`. All
of them exist from construction, and `recordAutomation` only sets `used = true`
on one that is already there. There is no `push_back`, `emplace` or `resize`
against `lanes` anywhere in the file — every access is an index or an
iteration.

So the lanes moved to the heap: one allocation, in the constructor, on the
thread that builds the sequencer. The audio thread allocates exactly as often
as before, which is never. A `StepSequencer` is a few dozen bytes again.

The outcome is better than the one planned for. `SequencerTest` no longer needs
`/STACK:16777216` at all, and **there is now no `/STACK` option anywhere in the
project** — where the plan expected to be choosing between a 32 MB stack and a
thirty-site edit to the test.

`ProcessorTest` did overflow the default stack, but for an unrelated reason: a
`WaveLatheProcessor` is a large object in its own right and the test builds
seven. They are heap-allocated there too, which is the test's own business and
costs the product nothing.

---

## Tests

1. **Word-boundary mask test** — set, test and clear ids **63, 64 and 65**
   independently, and assert that setting 64 leaves 0 clear.

   Worth being exact about what this proves, because an earlier draft of this
   line was not. The bug is the *absence of word indexing*; using the type
   **eliminates** it by construction, since there is nowhere inside it to write
   a bare shift. The test guards the replacement across the boundary. It does
   not detect the original fault — nothing does, once the fault cannot be
   expressed.
2. **A lane at a high id** — storage, the held set, and the capture/restore a
   project file is built from, at ids 64 and 127.

   **Playback at a high id is not testable yet, and the test says so rather
   than implying coverage it does not have.** `applyAutomation` walks
   `paramreg::count()`, which is still 63, so a lane above that cannot sound
   until something is registered up there. Raising the cap does not create
   parameters.
3. **`SequencerTest` unchanged** — the regression net for everything else.
4. **Rename the premise assertion** at `SequencerTest.cpp:1031`.

---

## Order of work

1. ~~Add the mask types **with tests, wired to nothing.**~~ **Done** (`cd33eab`).
   Boundary behaviour proven before anything depends on it.
2. ~~Swap the three `StepSequencer` masks over, **cap still 64**.~~ **Done.**
   17/17 green. `SequencerTest` was shown to catch a broken `merge` and a
   broken `all()` on its own, so the substitution is under the existing net
   and not merely compiling.
3. ~~Swap `hostWroteParameters`.~~ **Done** — but *not* "suite again", as this
   line originally said. **No test target compiles `PluginProcessor.cpp`.** The
   suite runs 17 green without it, so a green run says nothing whatever about
   this step; the change was verified by building the product itself. See the
   gap below.
4. ~~**Only then** raise the constant to 128, fix the test's stack, and add the
   high-id lane test.~~ **Done.** 18/18, and the whole net was shown to catch
   the original bug — see below.

**All four steps are complete. The cap is 128.**

### Proof the net catches the real thing

The last check was not another passing run. `wordOf` was changed to return 0,
which reproduces the single-word aliasing exactly as the old code had it, and
the suite was run against it:

```
ParameterMaskTest  FAIL: id 65 leaked into id 1, id 127 leaked into id 63  (8)
ProcessorTest      FAIL: expected only id 64, held 0, 64                   (6)
                   FAIL: and the Wave dial at id 0 is untouched
SequencerTest      FAIL: without holding id 0 with it                      (1)
```

Three independent suites, and `ProcessorTest` names the exact symptom the bug
would have produced in a DAW. Then reverted and proven byte-identical.

---

## The gap step 3 exposed

`Source/PluginProcessor.cpp` is named by exactly one target in
`CMakeLists.txt`: the `WaveLathe` plugin. **Nothing in `Tests/` compiles it.**

So the main product's processor — `processBlock`, the host-write drain, the
controller hold countdown, the order the latch, harmony and arpeggiator run in
— sits entirely outside the regression net, and every test run has always
reported GREEN without it. `HostParameterTest` tests the `HostParameter` class
against a stub listener; it never constructs a processor. `MasterPluginTest`
does construct one, but of the *other* product.

That makes the host-write path the worst-placed thing to widen blind, because
its failure at a high id is silent: a host writing parameter 70 would put the
hold on parameter 6, and the symptom is a dial that stops responding to its own
automation lane.

**Step 4 must add a `ProcessorTest`** that constructs a `WaveLatheProcessor`,
prepares it, calls `noteHostWroteParameter(id)` for an id at or above 64, runs a
block, and asserts the hold landed on that id and no other.
`MasterPluginTest`'s target is the pattern to copy — a console app listing the
processor's sources, with the editor along for the link because `createEditor`
names it. For WaveLathe that is 34 files, which is why it is worth deciding
deliberately rather than slipping it in: it is the largest test target in the
project, and it is also the only one that would test the product.

Steps 2 and 3 are pure substitutions, verifiable against the existing suite.
Step 4 is the only one that changes behaviour, and by the time it happens every
bare shift is already gone.

---

## Out of scope

`maxSteps = 64` and `ticksPerStep = 16` are unrelated constants that happen to
share a number with this one. Nothing here touches them.

---

## What the 128 was actually spent on

Recorded here because the cap was raised for the drum kit and the kit has since
changed size.

The synth has 63 registered dials. The kit was sixteen voices of four
automatable controls, so 63 + 64 = **127 of 128**, with a single id spare. Four
controls per voice rather than five was forced by exactly that: there was no
room for a fifth.

The kit came down to twelve voices in 0.41.0, which makes it 63 + 48 = **111**,
with seventeen spare. They stay spare. A fifth automatable control per voice
would cost twelve of the seventeen and put the ceiling back within one id of
the wall — and the choice of which four are registered was never only about the
arithmetic. Level, Decay and the two send Amounts are the ones a track wants
moving under it; Tune, Pan, Attack and the two selectors decide what the drum
IS, and that gets decided once.

`DrumPatternTest` asserts the count as `63 + project::numDrumParameters` rather
than as a literal, and separately asserts it is still under 128 — because the
first check follows the kit wherever it goes and only the second one is about
the ceiling.

---

# 0.44.0: 128 to 192, and every drum dial registered

The section above ends by saying the seventeen spare ids "stay free" and that
the four-of-nine split "was never only about the arithmetic". Both sentences
were written in good faith and the second one was wrong in a way worth naming,
because the same reasoning would have kept the split forever.

## What the reasoning got wrong

The argument for four was: *a selector picks which effect a voice runs through
and is set once while building a kit, but how much of it there is, is a fader -
and a fader is the thing a track wants moving under it.*

That is a sound argument about **automation lanes**. It was used to answer a
question about **MIDI learn**, and those are not the same want. Wanting a hand
on Tune while the take runs is not the same wish as wanting a Tune curve drawn
under it; the first needs a knob, the second needs a lane. The four were chosen
against the first wish using the second one's reasons, and nobody noticed
because the two share an id space.

They still share it. Registering a control still gives it a lane, a host
parameter and a learnable target all at once, and splitting those into two id
spaces was considered and rejected — two kinds of "parameter" that behave
differently is exactly the confusion the registry exists to prevent.

So the cap moved instead.

## The arithmetic

| | ids |
|---|---|
| Synth | 63 |
| Kit, 12 voices x 4 controls | 48 |
| **Before** | **111 of 128** |
| Kit, 12 voices x 9 controls | 108 |
| **After** | **171 of 192** |

192 rather than 171 because a cap is a ceiling and not a count. Picking the
current total would mean moving it again for the next dial.

Still unregistered: the drum rack's twelve and the mastering chain's twelve.
Same reason for both, and it is a real reason this time rather than a borrowed
one — they are one set of settings the whole kit or the whole output shares,
not anything a voice or a note owns.

## What it cost

Nothing structural. This is the part the 64-to-128 work paid for in advance.

- **`maxParameters = 192`.** One constant. `ParameterMask` is
  `BitMaskValue<Capacity>` with `words = (Capacity + 63) / 64`, so it became
  three words on its own, and there is no bare `1ull <<` anywhere outside it to
  find. Every per-parameter array in the project is already sized by the cap.
- **~260 KB.** A `Lane` is about 4.1 KB and there is one per id, so a
  `StepSequencer`'s lanes went from ~525 KB to ~787 KB. On the heap since the
  last round, so no stack anywhere had to grow and `/STACK` is still absent from
  the project.
- **A longer list in the host.** 171 automatable parameters, shown flat by most
  DAWs. Every name is meaningful - "Kick Tune", "OHat Pan" - but it is a lot to
  scroll, and that is the honest cost of the feature rather than a side effect
  to be fixed.

## The one thing that was not free

**Every drum id moved.** Ids were `firstDrumParameterId() + voice * 4 + offset`
and are now `+ voice * 9 + control`. Snare Level was 67 and is 72.

The alternative was appending the new sixty at 111-170 and leaving the existing
forty-eight where they were, which preserves saved work at the price of a
voice's nine controls living in two non-contiguous ranges for good, plus the
mapping function that implies. The project is still in development and the user
called it: take the break, keep the clean layout.

So **automation lanes and MIDI assignments saved against a drum dial before
0.44.0 now point at the wrong dial.** A lane on Snare Level plays into Snare
Pan. Nothing warns about this, because a lane is an id and an id is a valid id.
The synth's sixty-three did not move, and neither did anything else in a
project file.

The header's "IDS ARE APPENDED, NEVER INSERTED" rule stands. This is the one
recorded exception, and it is recorded so the next person does not read the
break as permission.

## What became simpler

Worth listing, because a cap raise that only adds is usually a bad sign.

- `project::drumParameterOffset` and `isDrumControlAutomatable` are **gone**.
  The registry offset and the storage index are the same number again, so
  `drumControlIndex` answers both and `drumParameterId` is a base plus it.
- `DrumPanel::unregisteredRange` is **gone** - a second table of ranges for the
  five dials with no registry row. Every dial takes its range from the registry
  now, so a dial and its own automation lane cannot disagree about what the ends
  mean. That table was exactly the kind of parallel list that drifts.
- The send selectors are **stepped parameters**, with `steps` and `stepLabels`
  built from `project::drumSendName`. A DAW shows "Reverb 1" rather than "1.00"
  and cannot draw a ramp through positions the engine rounds away.

## The gap this closed

`SequencerTest` carried a comment saying playback of a lane at a high id could
not be tested, because `applyAutomation` walks `paramreg::count()` and nothing
was registered above 63. Storage, the held set and the save round trip were
covered; **playing** one was not.

`count()` is 171 now, so it is tested: a lane written at Aux Amt 2 - id 170, the
last parameter in the project and the first thing ever to live in the mask's
third word - is played through a bar and lands in the storage slot the panel's
own dial writes to. The id it would have aliased onto under a single-word shift
is checked to be where it was left.
