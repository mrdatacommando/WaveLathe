// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "DrumParameters.h"
#include "DrumSample.h"

namespace drums
{
// ---- One drum voice, and the table that says which drum it is --------------
//
// Sixteen voices built from six engines. An 808 is sixteen fixed circuits, and
// most of them are the same circuit with different components soldered in: the
// kick and the three toms are one design, the four cymbals and the cowbell are
// another. Writing six engines and a table of constants says that out loud,
// and means a new voice is a row rather than a function.
enum class Engine
{
    // A sine whose pitch falls as it sounds, with a click on the front. Kick
    // and the three toms; the difference between them is how far the pitch
    // falls and how long it takes.
    bassDrum,

    // Two tone oscillators and a band of noise, balanced against each other.
    // The balance is the 808's "Snappy" control.
    snare,

    // Noise struck three times in quick succession and then left to ring. The
    // triple strike IS the clap - a single burst of the same noise is a
    // handclap the way a single frame is a film.
    clap,

    // Squares through a band. Hats, cymbals, cowbell. Six oscillators for the
    // cymbals and two for the cowbell, which is what the hardware does.
    metal,

    // A single resonant sine and nothing else. Clave, rimshot, woodblock.
    tone,

    // A band of noise and nothing else. Maracas.
    shaker
};

// What makes one voice different from another. Everything an engine needs, so
// that adding a drum is adding a row rather than writing code.
struct VoiceSpec
{
    const char* name;
    Engine engine;

    // Hz at Tune centre. For the noise-only engines this is unused and the
    // filter carries the pitch instead.
    float frequency;

    // Seconds at Decay centre - how long the hit rings before it is cut.
    float decaySeconds;

    // bassDrum and tone: how far ABOVE the base frequency the pitch starts, as
    // a multiple of it, and how quickly it falls back. A kick is a sine with a
    // fast, deep sweep; a tom is the same sine with a shallow, slower one.
    float sweep;
    float sweepSeconds;

    // The band the noise or the squares run through. Moves with Tune, so
    // tuning a drum up brightens it, which is what tuning a drum does.
    float filterHz;
    float filterQ;

    // How much of the hit is noise rather than tone. On the snare this is
    // Snappy, fixed rather than dialled: a fifth control per voice does not fit
    // in the parameter budget (see DrumParameters.h), and given the choice
    // between Snappy on one voice and Pan on all sixteen, Pan wins.
    float noiseMix;

    // metal only: how many squares. Six is a cymbal, two is a cowbell.
    int oscillators;

    // Voices sharing a non-zero group cut each other off. Group 0 chokes
    // nothing. Only the two hats are in a group, because that is the only
    // choke an 808 actually has - a closed hat cutting an open one is what
    // makes hats sound like hats, and nothing else on the machine does it.
    int chokeGroup;

    // A per-engine trim, so that every voice arrives at roughly the same
    // loudness at the same Level. Six squares summed are not as loud as one
    // sine, and a kit whose fader positions mean different things per row is a
    // kit you have to re-learn for every pattern.
    float outputTrim;
};

// The synthesised drums, indexed 0..numEngines-1. NOT by slot: a slot names
// the engine it wants, and two slots naming the same one is allowed.
//
// specOf took a SLOT and there were as many engines as slots, so the two
// indices were the same number and the distinction could not be seen. It can
// now, and reaching for a spec with a slot index is the mistake this rename
// exists to make impossible to write by accident.
const VoiceSpec& engineSpec(int engine);
const char* engineName(int engine);

// defaultEngineFor lives in DrumParameters.h, beside the two constants it is
// built from - KitParameters has to call it to lay out a fresh kit, and that
// header cannot see this one.

// What the Attack dial is worth on this voice, in seconds.
//
// RELATIVE TO THE DECAY, which is why the decay control is a parameter here.
// The dial reaches a quarter of however long the drum currently rings, so
// Attack and Decay come out at 20:80 of the hit at the top of the travel.
//
// It used to be a hand-tuned column in the table - sixteen numbers, one per
// voice - measured against each voice's NOMINAL length. Two things were wrong
// with that. The smaller one is that all sixteen turned out to be 27-33% of
// their own decay, so the column was a ratio nobody had written down. The
// larger one is that it did not follow the Decay dial: a Kick at Decay minimum
// rings for 140 ms and could still be given a 120 ms attack, which is 86% of
// the sound and leaves a swell with no drum on the end of it. At Decay maximum
// the same 120 ms was 9%. One dial meaning two very different things depending
// on where another one sits is not a dial anybody can learn.
//
// The curve is exponential rather than the square it was, which is the
// "logarithmic" a drum wants: half the dial now reaches 12% of the range
// rather than 25%, so the first few milliseconds - where taking the click off
// a kick lives - get most of the travel. exp(0) is 1, so subtracting one gives
// EXACTLY zero at the bottom, and zero has to stay exactly zero: it is the
// default on every voice and the thing "Off" means.
//
// Exported rather than kept private because the panel has to read the dial in
// milliseconds, and a second copy of this curve in the UI is a second copy
// that can disagree with the one making the sound.
float attackSecondsFor(const VoiceSpec& spec, float attackControl, float decayControl);

// How long this voice rings at this Decay setting: a third to three times its
// own length, with 0.5 leaving the spec untouched.
//
// Exported because the attack above is a fraction of it, and anything checking
// that relationship has to be able to ask both halves rather than reproducing
// the law and then agreeing with itself.
float decaySecondsFor(const VoiceSpec& spec, float decayControl);

// The same two questions for a slot that is playing a SAMPLE rather than its
// own circuit, where the voice's spec no longer describes what is sounding.
//
// Attack reaches a quarter of however much of the recording you are HEARING,
// which is the same 20:80 rule the synthesised voices follow with the
// recording's audible length standing in for the drum's decay. Gate a sample
// down to a tenth of itself with Decay and the attack range comes down with
// it, rather than staying long enough to swallow what is left.
//
// Decay says how much of the recording you hear. The same third-to-three law
// the synth voices use, but CLAMPED at the whole thing, because the top half
// of the dial is asking to make a recording longer than it is. So anything
// from the centre up plays the sample exactly as it was loaded, and the bottom
// half gates it progressively shorter. A dead top half is worth saying out
// loud; the alternatives were a default that truncated every sample somebody
// loaded, or a law that nothing else in the kit uses.
// What the Tune dial is worth as a playback ratio: plus or minus an octave,
// with 0.5 leaving the voice alone. Exported so that the tail report and the
// hit itself cannot disagree about how fast a sample is being played.
float tuneRatioFor(float tuneControl);

float sampleAttackSeconds(const Sample& sample, float attackControl, float decayControl);
float sampleAudibleSeconds(const Sample& sample, float decayControl);

// How long a hit on this voice lasts at these Attack and Decay settings, from
// the strike to exact silence.
//
// The one place that knows. Voice::trigger uses it to bound the hit, and
// longestTailSeconds uses it to tell a host how long to keep rendering after
// the last note - and those two answers disagreeing is precisely how a bounce
// ends up truncating a cymbal that the plugin itself was still playing.
//
// The attack is ADDED rather than folded in: a hit that takes 200ms to arrive
// and then decays for two seconds lasts two and a bit, not two.
float hitLengthSeconds(const VoiceSpec& spec, float decayControl, float attackControl = 0.0f);

// ---- A state-variable filter, written out rather than borrowed -------------
//
// The topology-preserving form, which is unconditionally stable at any cutoff.
// That matters here: the closed hat's band sits at 8.5 kHz, and Tune can push
// it higher again. The textbook Chamberlin SVF goes unstable well below that
// at 44.1 kHz, so it is not an option however much shorter it is.
//
// Written out rather than using juce::dsp::StateVariableTPTFilter so that this
// library needs only juce_audio_basics. A dozen lines is a cheap price for one
// fewer module between the drums and anything that wants to link them.
struct SvFilter
{
    void setCutoff(float hz, float q, double sampleRate);
    void reset();
    void process(float input);

    // Unity gain at the centre frequency. The raw bandpass state peaks at Q,
    // which would make the filter's resonance a volume control - and then
    // every spec would have to trim against its own Q.
    float bandPass() const { return r2 * bp; }
    float highPass() const { return hp; }

private:
    float g = 0.0f, r2 = 1.0f, h = 1.0f;
    float s1 = 0.0f, s2 = 0.0f;
    float hp = 0.0f, bp = 0.0f, lp = 0.0f;
};

// ---- The voice -------------------------------------------------------------
class Voice
{
public:
    // The seed is per voice and must differ between them. Sixteen voices
    // sharing one noise sequence would make a snare and a clap on the same
    // step correlate, which sums to something louder and narrower than either
    // - the same reason no two noise generators in a real machine agree.
    void prepare(double newSampleRate, juce::uint32 noiseSeed);
    void reset();

    // Starts the hit. Tune, Attack and Decay are read HERE and held for the
    // life of the hit; Level and Pan are read every block in renderAdding.
    //
    // The split is not tidiness. Tune, Attack and Decay describe the hit -
    // turning Tune while a crash rings should no more retune it than turning a
    // tom's tuning peg retunes a stick already in the air. Level and Pan are
    // the mixing desk, and a level automation lane drawn across a two-second
    // cymbal has to be heard doing something or the lane is a lie.
    // `sampleToPlay` is null for a synthesised voice, which is what fifteen of
    // the sixteen usually are. When it is not, the slot plays the recording and
    // its own circuit is silent - the two are alternatives, not layers.
    //
    // Latched like Tune and Decay, and for a sharper version of the same
    // reason: loading a sample over a voice that is mid-hit must not swap the
    // audio out from under the hit already sounding.
    void trigger(float velocity, bool accent, const VoiceSpec& newSpec, const VoiceParameters& params,
                 const Sample* sampleToPlay = nullptr);

    // Cuts the hit short over a few milliseconds rather than at once. An
    // instant cut is a click, and the one place this is used - a closed hat
    // landing on an open one - is the most exposed moment in a hat pattern.
    void choke();

    // ADDS into the buffer. The kit is a sound source among others, and a
    // renderer that cleared first could not sit beside the synth.
    void renderAdding(juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                      const VoiceParameters& params, float masterLevel);

    bool isActive() const { return active; }

private:
    float nextNoise();
    float renderSample();
    float renderFromSlot();
    float renderFromEngine();

    double sampleRate = 44100.0;
    bool active = false;

    const VoiceSpec* spec = nullptr;

    // Where the hit is, in samples since it started. An int, not a float
    // accumulating a per-sample increment: a hit is at most a few hundred
    // thousand samples and an exact count cannot drift, which is what lets a
    // long render and a chopped-up one produce the same bytes.
    int elapsed = 0;
    int lifetime = 0;      // samples until the voice is cut, fade included
    int fadeSamples = 0;   // the fade at the end of that life
    int attackSamples = 0; // the ramp up from silence at the start of it

    float hitGain = 0.0f;      // velocity and accent, fixed at the hit
    float decaySeconds = 0.1f; // latched from Decay
    float tuneRatio = 1.0f;    // latched from Tune

    // Per-sample state for whichever engine is running. Not a union: sixteen
    // voices of this is a few kilobytes, and a union would buy nothing but a
    // way to read the wrong member.
    double phase = 0.0, phase2 = 0.0;
    double phases[6] = {};
    double increments[6] = {};

    // Four envelopes, each a per-sample multiply. Which of them an engine uses
    // is the engine's business, and they are named for what they are rather
    // than for one engine's use of them: "fast" is the kick's click, the
    // maraca's attack and the clap's strike, and calling it clickEnv would have
    // read as a mistake in two of the three.
    float ampCoeff = 0.0f;
    float toneCoeff = 0.0f;
    float pitchCoeff = 0.0f;
    float fastCoeff = 0.0f;

    float ampEnv = 0.0f;
    float toneEnv = 0.0f;
    float pitchEnv = 0.0f;
    float fastEnv = 0.0f;

    // The clap, which is the one engine whose envelope is a schedule rather
    // than a curve: three strikes and then a tail. In samples, worked out once
    // at the hit, so the schedule is the same length in seconds at any rate.
    int strikeSamples[3] = {};
    int tailSample = 0;

    float baseFrequency = 0.0f;

    SvFilter filter;

    // Noise from a generator of our own, seeded per voice and advanced exactly
    // once per sample. Determinism is not an aesthetic preference here: the
    // test that proves a long render and a chopped-up one agree sample for
    // sample could not exist if the noise depended on how the block was cut.
    juce::uint32 noiseState = 0x9e3779b9u;

    // The choke, and the smoothing that makes Level and Pan safe to change
    // while a hit rings. Linear ramps across a block, which is enough for a
    // control that moves at a user interface's pace.
    float chokeGain = 1.0f;
    float chokeStep = 0.0f;

    // The recording this hit is playing, or null when the voice is synthesised.
    // Where the read head is in it, and how far it moves each output sample -
    // the source rate against the host's, times Tune. That ratio IS the
    // resampling, so there is none to do on the way in.
    const Sample* sample = nullptr;
    double samplePosition = 0.0;
    double sampleIncrement = 1.0;

    // Whatever the sounding thing needs trimming by: the spec's number for a
    // synthesised voice, and 1 for a recording, which arrives at whatever
    // level it was recorded at and is not ours to second-guess.
    float voiceTrim = 1.0f;

    // The last sample this voice emitted, and the step carried over from a hit
    // this one interrupted. See the note beside declickSeconds.
    float lastOutput = 0.0f;
    float declickValue = 0.0f;
    float declickCoeff = 0.0f;

    float leftGain = 0.0f, rightGain = 0.0f;
    bool gainsInitialised = false;
};
} // namespace drums
