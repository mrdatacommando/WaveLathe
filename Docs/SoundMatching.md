# Sound matching: what is established, what failed, and what is still open

A record of what has actually been measured about WaveLathe's preset matcher, kept
because the expensive part of this work is not writing the code — it is finding out
which ideas do not work, and that knowledge evaporates unless it is written down.

Every number here came from `MatchProbe`. Anything not measured is marked as such.

---

## The constraint this all works under

The matcher only ever analyses **rendered audio the user supplies as a reference**.
It does not read, decode, or reverse-engineer another synthesiser's preset format.
When WaveLathe refuses a foreign `.fxp` with *"this preset was not created by
WaveLathe, so its internal data can't be decoded"*, that is **by design, not a bug**,
and it should stay that way.

Everything below is about closing the gap using the only legitimate input: the sound
itself.

---

## How matching works

Analysis-by-synthesis. Render WaveLathe's own engine with candidate settings, measure
how far each render sits from the reference under a perceptual distance, and search
for the settings that minimise it. Differential evolution (DE/rand/1/bin) over 49
searched parameters.

### The distance

| term | resolution | weight |
| --- | --- | ---: |
| log-mel spectrum, coarse | 2048 | 1.0 |
| log-mel spectrum, fine | 512 | 0.6 |
| envelope, dB | — | 0.35 |
| modulation spectrum | 0.3–40 Hz | 12.0 |

A typical distance for a decent match is around **75**. That number matters more than
it looks — see *the prior-weight trap* below.

### The staged search

Four stages, classified by `stageForName`, split by **what can stand in for what**
rather than by position in the signal path:

1. **voice** — the static sound before anything moves it
2. **modulation** — everything that makes it move, wherever it lives
3. **effect** — the master chain, including the equaliser, which gets the last word
4. **joint** — a short pass over everything to settle the stages against each other

The chorus is fitted alongside the detune it competes with; the equaliser is held back
from the filter it can cancel.

---

## What is established

### The objective can see every parameter it searches

Holding everything at the target and sweeping one parameter across its range puts the
minimum on the true value, every time:

| parameter | wanted | found |
| --- | ---: | ---: |
| filterCutoffHz | 2600 | 2456 |
| attack | 0.020 | 0.016 |
| wavePosition | 0.550 | 0.550 |
| chorusMix | 0.750 | 0.750 |
| chorusDepth | 0.700 | 0.700 |
| unisonDetuneCents | 14.0 | 15.0 |
| eqHighGain | 8.0 | 8.4 |

**This is the single most useful fact in the file.** When the search returns something
wrong, the measure is not what is wrong. Look at the staging.

### Twenty of forty-nine dimensions are flat

Moving them across their entire range changes the distance by under 2% of what the
loudest parameter changes it; ten of them change it by **exactly nothing**.

```
0.0  osc2WavePosition   0.0  osc2Semitones    0.0  osc2Fine
0.0  modEnvAttack       0.0  reverbSize       0.0  subOctave
0.0  noiseColour        0.0  phaserRate       0.0  phaserFeedback
0.0  subWave
```

They are inert because their parent is switched off — a second oscillator's tuning at
zero level, a reverb's size at zero mix. The loudest, for contrast: `wavePosition`
(spread 253.8), `filterCutoffHz` (167.9), `reverbMix` (151.8), `modEnvToCutoff` (132.3).

This looks like wasted search budget. It is not - see *freezing the dead dimensions*
below, where that intuition was followed and turned out to be wrong. What it does
cost is real, though: whatever arbitrary value these drift to is silent while
matching and audible the moment the user raises the parent, which is what
`tidyInactiveParameters` puts back after the search.

### The factory bank beats a heuristic estimate

Playing all 43 shipped presets against the reference costs less than one generation.
Over eight seeds:

| | distance | cutoff error |
| --- | ---: | ---: |
| bank off | 78.7 | 2.33 oct |
| **bank on** | **74.5** | **1.23 oct** |

6 of 8 seeds ended closer with the bank. On a test phrase the heuristic estimate ranked
**39th of 43** — it read the filter at 398 Hz against a real 3000 Hz.

Note the design detail: the search still *starts* from the estimate. The bank
contributes company in the population, not a place to begin. Starting from the bank
winner was measured and was no better.

### Fitting several pitches beats fitting one

Judged at a pitch the search never saw:

| | held-out distance | cutoff error |
| --- | ---: | ---: |
| one pitch | 196.4 | 1.90 oct |
| **three pitches** | **154.6** | **1.18 oct** |
| one pitch, 3× the budget | 176.3 | 3.61 oct |

Four of four seeds. The third row is the important one: **more budget on one pitch made
the filter worse**, because it only overfitted harder. A filter cuts the harmonic series
somewhere different at every fundamental, so three pitches make the overfit impossible
in a way no penalty term reliably did.

The budget is divided between the notes, not multiplied by them.

### The engine is deterministic, and was not

Two causes were found and fixed:

- an **unseeded noise RNG** in `SynthVoice`
- **master-chain smoothers gliding from the previous candidate's settings** after
  `reset()`, which made a candidate's score depend on what had been rendered before it,
  and therefore on thread scheduling

The same seed once produced 68.64 and then 93.3. Both fixed; `MatchTest` now asserts
bit-identical scores across fresh renderers, repeat renders, and renders interleaved
with other patches.

**This invalidated a set of staging measurements taken before the fix.** Any number in
an older note that predates this is untrustworthy.

---

### The invented detune: fixed, and what it revealed (0.23.6)

Fitted to a plucked note with no unison at all, the search returned **9.5 cents** of
detune. The remedy is not in the loss function — a post-search pass offers each unison
setting at zero, renders again, and leaves it there unless putting it back actually
improves the fit.

**The first attempt failed, and the failure is the interesting part.** With a 1%
tolerance every case was rejected:

```
unisonDetuneCents 0.220 -> 0.000 : distance 24.6509 -> 26.1590
unisonDetuneCents 0.537 -> 0.000 : distance 25.2514 -> 26.8866
```

Taking away detune the target does not have makes the objective's distance **worse, by
6%**. So the detune was never drifting with nothing to push it back — the objective
actively prefers it. It is almost certainly compensating for the cutoff error on the same
target (1.60 octaves), smearing the spectrum in a way that partly hides a filter in the
wrong place. The same phenomenon 0.20.0 staged the search to prevent, showing up in a
pair of parameters the staging does not separate.

That makes the threshold a question with a measurable answer rather than a taste. The
same search was run against the same pluck twice, once dry and once at five voices and
20 cents:

| target | cost of removing the detune |
| --- | --- |
| really has 20 cents | +128%, +79%, +129%, **+35%** |
| has none | **+6.1%**, +6.5% |

A five-fold gap, so 15% sits in the middle of a gap rather than on top of a measurement.

Result: **9.5 cents → 0.0** on the dry target, **10.2 cents still recovered** on the
20-cent one. Nothing else moved — cutoff error 1.60 oct, chorus 0.093, wave 0.324, voice
recovery mean 75.8 / 1.23 oct, all unchanged.

`MatchProbe` gained the section **"unison earns its keep"**, which runs both targets and
checks both directions. That is deliberate: a pass that stripped the unison
unconditionally would have scored perfectly on the dry target alone. This is the
harmonicity lesson applied before the fact rather than after.

**The generalisation worth keeping:** every change to the *distance function* has failed
here — six chorus attempts, the per-band feature, harmonicity, the dead-dimension freeze.
Every change *around* it has worked — staging, determinism, the factory bank, multi-pitch
fitting, `tidyInactiveParameters`, and now this. Five for five against nought for nine.
When something in a match looks wrong, reach for the ring around the objective first.
## What was tried and did not work

Recorded so it is not tried again from scratch.

### The chorus, five or six attempts

The search returns `chorusMix` **0.093** against a target of 0.75.

What is known:

- the objective sees chorus fine — with everything else right, *no chorus* is the worst
  setting on the sweep
- sweeping chorus around the patch the search **actually returns** shows the best mix
  there is **0.00**

So the voice the search settles on genuinely does not want chorus. The dry voice stage
imitates the chorused sound with detune and a different wavetable position, and having
done so, adding the real chorus on top makes things worse. **The search is then right to
refuse it.** This is a staging problem, not a measurement problem.

Attempted and reverted:

| attempt | outcome |
| --- | --- |
| regroup the stages, effects earlier | looked like a win on one target; invented 0.254 chorus on a pluck that had none |
| `log1p` compression of the modulation term | no improvement |
| `cbrt` compression | no improvement |
| `cbrt` + prior-weight restoration | no improvement |
| multi-pitch fitting | improved everything else; chorus recovery got *worse* (0.186 → 0.032) |
| per-band modulation feature | see below |

**Recommendation: stop.** Six attempts, no movement.

#### The last remaining doubt, closed

One suspect was never eliminated: that the chorus effect itself was not modulating
properly under offline rendering, in which case no objective could have heard it and
all six attempts were aimed at the wrong end of the problem. There was a plausible
mechanism — `MasterEffects` calls `chorus.reset()` when `snapSmoothers` is set, and a
reset takes the chorus LFO back to phase zero.

It does not hold. `snapSmoothers` is cleared at the end of the first `processChunk`
([MasterEffects.cpp:295](../Source/MasterEffects.cpp)), so the snap happens once per
render and not once per block — which is exactly the determinism an offline render
needs.

`MasterFxTest` then measured the effect directly, by feeding it a steady 80 Hz tone and
watching what the moving comb does to the level:

| setting | level travel | wobble rate |
| --- | --- | --- |
| depth 0.00, rate 2 Hz | 13.1% | (a still comb) |
| depth 0.60, rate 2 Hz | **104.3%** | — |
| rate dial 0.5 / 1 / 2 / 4 Hz | — | 1.02 / 2.02 / 4.01 / 8.01 Hz |

The rate reads at exactly twice the dial because a sweep of this depth crosses two comb
notches per cycle, which is expected; what matters is that it tracks in perfect
proportion across an eight-fold range.

**The chorus works.** The effect is not the problem, and there is now no remaining
explanation for the six failures other than the objective and the staging. That does not
make the chorus worth another attempt — the recommendation above still stands — but it
does mean nobody need wonder about the effect again.

### The per-band modulation feature

The idea: detune and chorus are physically distinguishable. Harmonic *k* of each detuned
copy sits at *k·f₀(1 ± δ)* and beats at *k·f₀·2δ*, so the rate **rises with frequency**;
a chorus is an LFO sweeping a delay, which does not.

Measured across 15 chorus settings and 14 detune settings:

| | rate climb, lowest band → highest |
| --- | ---: |
| detune, every pure setting | **×7.75 to ×11.0** |
| chorus | ×0.58 to ×11.40 |
| a patch with nothing moving | ×1.75 |

**The detune half is real and holds** — the whole cents range, every voice count, two
octaves, dark and bright voices.

**The chorus half is noise.** A chorus modulates a band by ~128 units where a detune
modulates it by ~989 and a still patch by 4.6, so the measure reads the band's own
broadband envelope rather than the chorus. The chorus cases land on top of the control.

So it is a **detune detector, not a chorus detector**. Shelved for chorus. Possibly
useful for the detune-invention problem below, though it was validated on a 4-second
held note and a pluck decays in 0.25s, which needs checking first.

Two earlier versions of this measurement looked cleanly separating and were wrong: one
had a 25 Hz ceiling *below* the rate it was trying to find, and picked whichever
harmonic of the LFO was loudest per band. Both were fixed on stated grounds, and the
separation went away.

### Freezing the dead dimensions — the premise was wrong

The reasoning: 20 of 49 dimensions are flat, so roughly 40% of the search budget is
spent crossing ground that cannot improve anything. Freeze them and the remaining 29
get about 1.7× the effective budget.

**Both halves of that turned out to be wrong**, and it is worth knowing why before
anyone reaches for the idea again.

**The implementation locked the filter.** Probing each parameter at three points in its
current context and freezing the flat ones froze `filterCutoffHz` — the second-loudest
parameter in the whole set — because the estimate's `filterType` bypasses the filter, so
cutoff genuinely does nothing *there*. Every seed then returned an identical 1.62 octave
cutoff error. It also froze `chorusRate`, `chorusDepth`, `delayTimeMs` and
`unisonDetuneCents`, all inert only because a sibling the search was about to move was
currently switched off.

> Measuring a parameter's audibility in a context the search is about to change, and
> then locking it on that basis, pre-commits the search to whatever configuration it
> happened to start in.

**And the premise underneath it does not hold.** In differential evolution, one render
evaluates the **whole vector**. `evaluateBatch(trials, ...)` is called once per
generation with the full population regardless of how many dimensions are active, so the
render count is `populationSize × (generations + 1)` either way. Inert dimensions never
cost renders. They do not degrade a trial's score either, since by definition they do
not move the objective. **The 40% waste does not exist** — and the probe needed to find
the flat dimensions would have *added* around 140 renders to buy it.

What the 20 flat dimensions actually cost is only that they drift to arbitrary values,
which is silent while matching and audible the moment the user raises the parent. That
is already handled by `tidyInactiveParameters`, which runs *after* the search, where
locking a value in is safe because there is nothing left to pre-commit.

Reverted in full. The lasting value is the rule in the blockquote above, and the
reminder that "wasted dimensions" is an intuition from gradient methods that does not
transfer to a population method where cost is per-vector, not per-dimension.

### wavePosition: the search buys noise instead of a wavetable

This one has a mechanism, which makes it the most promising thing on the list.

`wavePosition` has the **largest spread of any searched parameter** — 253.8, well above
the filter cutoff's 167.9 — and sweeping it with everything else at the target puts the
minimum exactly on the true 0.550. The search returns **0.000**, the end of the range
that sweep calls among the worst.

From the patch the search actually arrives at, though, the curve wants 0.10. The search
is locally right: forcing the true value into its answer makes the patch **56% worse**
(79.8 → 124.8). Putting the rest of the patch back progressively moves the preference a
little, and — far more tellingly — collapses the spread:

| context | wants | spread |
| --- | ---: | ---: |
| around the target | 0.55 | 253.8 |
| around the search's answer | 0.10 | 52.6 |
| + the target's detune | 0.10 | 55.1 |
| + the target's master chain | 0.15 | 18.4 |
| + the target's filter | 0.20 | 13.9 |

So `wavePosition` has not moved somewhere else. It has gone **quiet** — eighteen times
less audible in the search's patch than in the target's. The source mix says why:

```
  the target    osc1 1.00   osc2 0.00   sub 0.00   noise 0.00   drive 0.00
  the answer    osc1 0.20   osc2 0.39   sub 0.00   noise 1.00   drive 0.64
```

**The search built a patch carried by noise.** The wavetable oscillator runs at a fifth
of its level, full-scale noise sits on top of it, and 0.64 of drive saturates what is
left. Of course the wavetable position is inaudible — it controls a quiet component
buried under noise and distortion, so it drifts to a rail.

Why the objective allows it: noise is a cheap way to match a dense spectrum. A chorused,
detuned saw has a crowded log-mel picture, and full-scale noise plus drive reproduces
that crowding without needing the right wavetable at all. The two log-mel terms carry 1.6
of the 1.95 non-modulation weight, and they measure **energy per band, not whether that
energy is harmonic**.

**Untested hypothesis for a fix:** a harmonicity term — spectral flatness, or the
strength of harmonic peaks against the floor between them — should separate noise from a
rich oscillator immediately. Unlike the chorus-against-detune question, there is no
subtlety about whether the signature exists; telling noise from tone is the textbook case
these features were invented for.

This may also explain the pluck's 1.60 octave cutoff error, since what filter is right
depends on what is being filtered, and it may be part of the chorus story too — noise
supplies some of the same broadband density a chorus would.

Measure it with:

```bash
./build/MatchProbe_artefacts/Release/MatchProbe.exe "wave position"
```

Eighteen seconds.

### Harmonicity: validated in isolation, but a regression in the loss

The wavePosition mechanism says the objective cannot tell noise from tone. A
harmonic-to-noise ratio can. This was validated *before* touching the loss, which is the
order the per-band chorus attempt got wrong.

**What it measures:** the fraction of spectral energy landing in bands around multiples
of f0 — known, since the note being rendered is known — against what lies between them.
Bands are an eighth of the harmonic *spacing* either side, so they are constant in bins
and tile a quarter of the spectrum.

| | dB |
| --- | ---: |
| the target | 8.7 |
| the patch the search chose | 1.1 |
| **separation** | **7.6** |

Sweeping the tone-to-noise balance — osc1 1.00 down to 0.20 as noise goes 0.00 to 1.00,
the exact path the search took — across seven contexts:

| context | tone end | noise end | travel |
| --- | ---: | ---: | ---: |
| plain, middle C | 8.7 | -2.3 | 11.0 |
| wavetable at 0.0 (a sine) | 54.5 | -3.7 | 58.3 |
| wavetable at 0.9 | 2.4 | -3.8 | 6.2 |
| an octave down | 3.6 | -2.9 | 6.5 |
| an octave up | 16.9 | 1.5 | 15.4 |
| with some drive | 8.8 | -2.3 | 11.1 |
| with heavy drive | 8.5 | -2.4 | 10.9 |

Monotonic in 7 of 7, and the structural result: the contexts spread **52.1 dB at the tone
end and 5.2 dB at the noise end**. Whatever wavetable is buried under it, a patch carried
by noise reads like a patch carried by noise.

And the decisive test — a reference that is *meant* to be noisy (3.3 dB) is matched 0.0 dB
away by a noisy candidate and 5.3 dB away by a clean one. **A distance, not a prejudice
against noise**, so it would not wreck patches that are supposed to be noisy.

**Three formulations, and what each got wrong:**

| formulation | flaw |
| --- | --- |
| median harmonic peak over median floor | separated by 7.1 dB but read a **sine** as 4.3 dB, as unharmonic as hiss — it asked whether every harmonic slot was filled, not whether the sound is tonal |
| harmonic-to-noise, bands proportional to k·f0 | fixed the sine, but bands grew to 61% of the spectrum by the 30th harmonic and swallowed the noise they existed to exclude; travel collapsed to 0.1–1.7 dB |
| harmonic-to-noise, bands a fixed fraction of f0 | what is there now |

Predictions were written down before each run. On the third attempt four of six held; both
misses came from the same mistake — `noiseLevel = 1.0` does not mean "a patch made of
noise" while osc1 is still at 1.0, so the noise-level sweep never reached the condition
being predicted. That is why the travel assertion is applied to the balance sweep. The
threshold did not move and the measure was not adjusted to clear it.

**It was then put in the loss, and reverted** - see the next section. Everything above
this line still holds: the measurement is sound for what it measures, and the probe
passes. What it does not establish is that the loss is better for having it.

```bash
./build/MatchProbe_artefacts/Release/MatchProbe.exe harmonicity
```

1.3 seconds.

### Putting harmonicity in the loss: a clear regression, reverted

Wired in at weight 1.25 — the gap between a reference and the noise-carried patch worth
about what the coarse spectral term is — with the fundamental passed through every call
site. Measured on both ship gates. It made almost everything worse.

| metric | before | with harmonicity | |
| --- | ---: | ---: | --- |
| recovery cutoff error, bank on | 1.23 oct | **2.30 oct** | much worse |
| seeds that ended closer with the bank | 6 of 8 | **3 of 8** | worse |
| chorus mix recovered | 0.093 | 0.047 | worse |
| **wave position error** | **0.324** | **0.306** | ~unchanged |
| pluck: chorus invented | **0.000** | **0.220** | **guard fired** |
| pluck: phaser invented | 0.000 | 0.070 | worse |
| pluck: detune invented | 9.5 cents | 12.7 cents | worse |
| pluck: cutoff error | 1.60 oct | 2.00 oct | worse |

Two things settle it without any further tuning.

**The wave position error barely moved.** That was the entire motivation, and at full
weight it went 0.324 to 0.306. A smaller weight would move it less while still costing
something, so there is no weight at which this trade is worth making.

**The pluck guard fired.** A dry sound with no movement in it came back with 0.220 of
chorus where it had reliably come back with 0.000 before. That guard exists precisely to
catch a change that was judged on the moving target, and it caught this one.

The factory-bank audition also picked a different winner - `Wide Stack` instead of
`String Ensemble` - so the term reordered the bank, and the new front-runner is a worse
place to start from.

**The likely mechanism, and the gap in the validation.** The harmonicity probe swept the
tone-to-noise balance across seven contexts and held in all of them - but every one of
those contexts was a *sustained, full-level* patch. The pluck is not: sustain 0.05 and a
0.25 s decay, so the analysis window from 0.3 s to 0.67 s is measuring a very quiet tail
where the render's own noise floor can dominate. If a decayed reference reads as
low-harmonicity for that reason, the search will add chorus and noise to match it - which
is exactly the behaviour the gate reported.

So the feature is probably unreliable on quiet or decayed material, and the validation
never tested that axis. **Thorough on one axis is not the same as validated.** The
tone-to-noise sweep was careful, and it was still measuring the wrong population.

The probe is kept - it passes, and the measurement is sound for what it measures. Nothing
of the wiring remains in the loss. What survives in the code is the prior restructure
below, which was worth doing anyway.


### The prior-weight trap

`neutralPreferenceWeight` (0.04) and `railPreferenceWeight` (0.05) are **absolute**, and
implicitly tied to a typical distance of ~75.

Compressing the modulation term once dropped the typical distance from ~75 to ~19 —
which **quadrupled the priors' real influence** without a line of the prior code
changing. One experiment was measured under silently wrong priors before this was
spotted.

> **Any change to the distance scale must rescale these two weights deliberately.**

---

## What is still open

| problem | measured | note |
| --- | --- | --- |
| chorus not recovered | 0.093 vs 0.75 | six attempts; recommend leaving it |
| ~~detune invented on dry sounds~~ | fixed in 0.23.6 | was **9.5 cents** on a pluck with no unison; **0.0** now. Not the loss function: a post-search pass offers each unison setting at zero and keeps it there unless the fit actually improves. See below |
| `wavePosition` railed to 0.00 | mean error 0.324 | **mechanism found** - the search builds a patch carried by noise, in which the wavetable is inaudible. See above. The most promising lead on the list |
| pluck cutoff error | 1.60 oct | 1200 Hz landing anywhere from 400 to 3600 |
| ~~glide bug~~ | fixed in 0.23.2 | the glide origin was shared between voices, so a chord slid up from itself. Per voice now, with `GlideTest` holding it |
| ~~poly glide~~ | fixed in 0.23.3 | per-voice history alone lost the glide entirely on a line played faster than its release - measured at 0 of 5 notes, the two pitches parking on one voice each. A fresh voice now picks up from the note that most recently *stopped*, which a line always has and a chord never does. Measured through a real `Synthesiser`: 5 of 5 both detached and quick |
| ~~mono glide~~ | fixed in 0.23.4 | the 0.23.2 claim that mono was unaffected "there being only one voice" was wrong - mono limits sounding *notes*, not voices, and with legato off the note-off and note-on land on the same sample, so mono was the worst case rather than the exempt one. Falling back to a held key also did not slide: the Synthesiser stops any voice already playing that note first, and a stale releasing voice republished its own pitch. Only the first stop counts now |

Ideas still untried, and the reasoning against most of them: MFCCs are a truncated DCT of
the log-mel spectrum and discard precisely the harmonic fine structure needed here; a
spectral centroid summarises a distribution the loss already compares band by band;
A-weighting is a perceptual tilt that mel-scaling partly does already. Spectral
convergence is the one worth considering, after harmonicity is in. CMA-ES is a fix for a
search that cannot find the minimum, and the measurements say the minimum is in the wrong
place - it would reach the noise-carried patch faster. Optimiser work belongs after the
objective stops rewarding the wrong answer.

---

## How to measure anything here

Two suites, split because the single one had grown to twenty minutes — which quietly
changed how the work got done, making every experiment cost a coffee break so ideas got
reasoned about instead of measured.

```bash
./build/MatchTest_artefacts/Release/MatchTest.exe
```

**5.7 seconds.** The guards — things that must keep being true. Run after every change.

```bash
./build/MatchProbe_artefacts/Release/MatchProbe.exe
```

**13 minutes.** The measurements. Both binaries take section-name arguments:

```bash
./build/MatchProbe_artefacts/Release/MatchProbe.exe pluck "voice recovery"
```

That is **5m 14s** and covers both ship gates. Everything *except* those two sections
runs in 45 seconds.

### The two ship gates

Run before any change to the staging or the objective:

- **`voice recovery`** — eight seeds against the moving target. The only honest read on
  whether a change helped.
- **`pluck`** — a dry sound with no movement in it. A staging tuned until the moving
  target comes out well will start hearing chorus in sounds that have none, and this is
  what catches it.

---

## Methodology, learned the hard way

Every one of these cost real time.

**Three seeds is not a measurement.** On the factory-bank question, three seeds pointed
the *opposite* way from eight. Use eight and assert on means.

**One target hides regressions.** A restaging looked like a clear win until the pluck
target showed it inventing 0.254 chorus where there was none. `pluckTarget()` exists
because of that near-miss.

**Held-out evaluation is the only honest test of overfitting.** Fit on some pitches,
judge at an unseen one. Measured in-sample, more budget always looks better; measured
held-out, 3× the budget on one pitch made the filter *worse*.

**A measurement corrected three times after seeing its result is a measurement fitted to
the test.** The per-band probe went through three versions, each fix discovered by
looking at the answer. That is the point to stop, not the point to try a fourth.

**Timing belongs in the suite.** One section turned out to be 54% of the whole probe
runtime and nobody knew until it was printed. Both suites now report where their time
went.
---

## Where this stands

*As of 14 September 2026.*

A full day of matcher work produced **three reverts and no improvements**:

| attempt | why it failed |
| --- | --- |
| per-band modulation feature (chorus attempts 5 and 6) | the detune half holds; the chorus half is indistinguishable from a still patch |
| freezing the dead dimensions | the premise was wrong - DE's cost is per-vector, not per-dimension, so inert dimensions were never costing budget |
| harmonicity in the loss | validated in isolation, regressed on both ship gates |

The last is the informative one. It passed a careful seven-context validation and still made
things worse, because the validation swept one axis thoroughly and was blind to another -
every context was a sustained, full-level patch, and the sound it broke on was a decaying
pluck.

What shipped instead: the suite split, this record, the prior restructure, and a **synth**
bug fix (glide) that came from reading code rather than from research.

### The recommendation

**Stop matcher research for now.** The matcher works: 74.5 distance and 1.23 octaves of
cutoff error with the bank, multi-pitch fitting, quick match from the factory bank. What
remains - chorus at 0.093, invented detune at 9.5 cents, wavePosition railing - are quality
issues on a working feature, and the hit rate on them is currently nought for three.

If it is picked up again, in order of evidence:

1. **The invented detune.** Measured at 9.5 cents on a dry pluck with no unison at all,
   user-visible, and unlike the chorus it is about the search *adding* something - which the
   existing rail prior is already shaped to discourage.
2. **Harmonicity with an adaptive analysis window.** The fixed 0.3 s to 0.67 s window is
   the most likely reason it broke on decayed material. Place the window where the energy
   actually is, then re-run both gates. Note that this is a fourth correction to that
   measurement, and the earlier three are recorded above for a reason.
3. **Not the chorus.** Six attempts.

### The rule that would have saved the most time

> Validate a feature on the population it will meet, not on the population that is easy to
> sweep.

Both of the day's most expensive mistakes were versions of this. The per-band chorus measure
was swept across chorus settings but never against a control with no movement in it. The
harmonicity measure was swept across seven contexts that were all sustained and full-level,
and then met a pluck.
