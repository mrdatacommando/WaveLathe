// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "DrumVoice.h"

#include <cmath>

namespace drums
{
namespace
{
constexpr float twoPi = 6.283185307179586f;

// The six oscillator ratios an 808's metal circuit runs at, and the two the
// cowbell uses. Not round numbers and not meant to be: the point of the set is
// that no two of them share a harmonic, which is what makes six squares sound
// like struck metal rather than like a chord.
constexpr double sixOscRatios[6] = { 1.0, 1.4471, 1.6170, 1.9265, 2.5028, 2.6637 };
constexpr double twoOscRatios[2] = { 1.0, 1.4815 };

// How far the pitch of a bassDrum or tone starts above where it settles, and
// how quickly it gets there, are in the spec. What is here is the floor under
// every envelope: a coefficient for a decay measured in seconds.
float coefficientFor(float seconds, double sampleRate)
{
    const auto samples = juce::jmax(1.0, (double) seconds * sampleRate);
    return (float) std::exp(-1.0 / samples);
}

// How many decay constants a voice is allowed to live for before it is faded
// out and switched off.
//
// Six puts the cut at about -52 dB, which is below anything audible under a
// kit, and it BOUNDS the voice: an exponential never actually reaches zero, so
// without a number here a two-second crash would keep sixteen multiplies a
// sample running for the rest of the session. The added 60 ms is a floor under
// the very short voices, whose six decay constants would otherwise be gone in
// a fifth of the time it takes the end fade to run.
constexpr float lifetimeDecays = 6.0f;
constexpr float lifetimeFloorSeconds = 0.06f;

// The fade at the end of that life, and the one a choke uses. Both exist for
// the same reason: cutting a ringing oscillator at a non-zero sample is a
// click, and a click is louder than the tail it replaced.
constexpr float endFadeSeconds = 0.008f;
constexpr float chokeFadeSeconds = 0.005f;

// When the clap stops being struck and starts ringing. Its three strikes
// happen before its tail begins, so they are added to its life rather than
// spent out of it.
constexpr float clapTailSeconds = 0.026f;

// How quickly the step left by an interrupted hit is let go of.
//
// Two milliseconds, which is a decade below the lowest note this kit makes
// and so is heard as part of the new strike rather than as an event. Longer
// and the carried-over level starts to be an audible thump of its own;
// shorter and it is the click it was meant to remove.
//
// A third fade, alongside the end fade and the choke's, and it exists for the
// same reason both of those do: cutting a ringing oscillator at a non-zero
// sample is a click, and a click is louder than whatever it replaced. The
// other two cover a voice ENDING. This one covers a voice being interrupted
// by itself, which is the one case neither of them ever saw.
constexpr float declickSeconds = 0.002f;

// What an un-accented hit at full velocity is worth, leaving the rest for
// accent. Without this, accent could only do something to a hit that was not
// already at full velocity - and the accents people actually write are on the
// loudest hits in the bar.
constexpr float accentHeadroom = 0.75f;

// ---- The sixteen engines ---------------------------------------------------
//
// The 808's own voices in its own order, then four more that people reach for
// anyway. Sixteen SOUNDS to choose from, on twelve slots - which is why this
// table did not shrink when the kit did. Dropping a row cost nothing here.
//
// The first eleven are the 808's instruments and are also what slots 0-10 play
// by default, so this table's order is load-bearing twice over: as the list a
// menu shows, and as defaultEngineFor's identity mapping. Inserting a sound in
// the middle would silently repoint every slot on every saved project.
// Append instead.
//
// The first twelve names must stay in step with project::drumVoiceName, which
// names the same rows on the grid. They are duplicated rather than shared
// because this library deliberately cannot see the sequencer, and two lists
// that cannot see each other are two lists that can drift: row 9 saying OHat on
// the grid and something else here would play the wrong drum without crashing,
// which is the kind of fault that gets blamed on the pattern.
//
// DrumPatternTest links both halves and is the one place they are held equal.
// It compares the first numVoices names rather than all of them, because past
// the twelfth there is no row for a name to disagree with.
//
//        name       engine             freq  decay  sweep sweepS  filtHz    Q  noise osc choke trim
const VoiceSpec specs[numEngines] = {
    { "Kick",    Engine::bassDrum,      52.0f, 0.42f, 4.50f, 0.016f, 2400.0f, 0.8f, 0.06f, 1, 0, 1.00f },
    { "Snare",   Engine::snare,        190.0f, 0.20f, 1.00f, 0.010f, 1800.0f, 0.8f, 0.55f, 2, 0, 1.05f },
    { "Rim",     Engine::tone,        1700.0f, 0.035f, 1.60f, 0.004f, 2400.0f, 1.2f, 0.35f, 2, 0, 0.90f },
    { "Clap",    Engine::clap,           0.0f, 0.26f, 1.00f, 0.010f, 1050.0f, 1.1f, 1.00f, 0, 0, 1.25f },
    { "LoTom",   Engine::bassDrum,      90.0f, 0.36f, 2.00f, 0.030f, 3000.0f, 0.8f, 0.03f, 1, 0, 1.00f },
    { "MidTom",  Engine::bassDrum,     135.0f, 0.32f, 2.00f, 0.028f, 3000.0f, 0.8f, 0.03f, 1, 0, 1.00f },
    { "HiTom",   Engine::bassDrum,     200.0f, 0.28f, 2.00f, 0.025f, 3000.0f, 0.8f, 0.03f, 1, 0, 1.00f },
    { "Cowbell", Engine::metal,        540.0f, 0.30f, 1.00f, 0.010f, 2640.0f, 1.4f, 0.00f, 2, 0, 1.60f },
    { "CHat",    Engine::metal,        205.3f, 0.055f, 1.00f, 0.010f, 8200.0f, 0.9f, 0.00f, 6, 1, 2.20f },
    { "OHat",    Engine::metal,        205.3f, 0.50f, 1.00f, 0.010f, 7600.0f, 0.9f, 0.00f, 6, 1, 2.20f },
    { "Crash",   Engine::metal,        205.3f, 2.00f, 1.00f, 0.010f, 5200.0f, 0.5f, 0.00f, 6, 0, 1.75f },
    { "Ride",    Engine::metal,        205.3f, 1.40f, 1.00f, 0.010f, 4200.0f, 0.7f, 0.00f, 6, 0, 2.00f },
    { "Clave",   Engine::tone,        2500.0f, 0.045f, 1.00f, 0.001f,   0.0f, 0.7f, 0.00f, 1, 0, 0.85f },
    { "Maraca",  Engine::shaker,         0.0f, 0.032f, 1.00f, 0.010f, 7000.0f, 0.6f, 1.00f, 0, 0, 1.60f },
    { "Perc 1",  Engine::metal,        820.0f, 0.09f, 1.00f, 0.010f, 2400.0f, 1.3f, 0.00f, 2, 0, 1.50f },
    { "Perc 2",  Engine::bassDrum,     320.0f, 0.16f, 3.00f, 0.020f, 2000.0f, 0.8f, 0.10f, 1, 0, 1.00f }
};
} // namespace

float decaySecondsFor(const VoiceSpec& spec, float decayControl)
{
    const auto decay = juce::jlimit(0.0f, 1.0f, decayControl);
    return juce::jmax(0.002f, spec.decaySeconds * std::pow(3.0f, decay * 2.0f - 1.0f));
}

const VoiceSpec& engineSpec(int engine)
{
    // Out of range returns the kick rather than reading past the table. A
    // caller that has lost track of which engine it means gets a drum, not the
    // stack. noEngine lands here too, which is deliberate: whoever asks a
    // blank slot for a spec has already decided to make a sound, and the
    // guard's job is to make that safe rather than to argue about it. Not
    // triggering a blank slot at all is DrumKit's decision and is made before
    // this is ever called.
    if (engine < 0 || engine >= numEngines)
        return specs[0];

    return specs[engine];
}

const char* engineName(int engine)
{
    if (engine < 0 || engine >= numEngines)
        return "";

    return specs[engine].name;
}

static_assert(numEngines >= numVoices - 1,
              "defaultEngineFor maps slot N to engine N for all but the last slot, so there "
              "have to be at least that many engines to map onto.");

// What fraction of the sound the Attack dial can take at the top of its
// travel, and therefore where the line between attack and decay sits.
//
// A quarter of the decay is a fifth of the whole, so the two come out at the
// 20:80 the kit is built around. The hand-tuned column this replaced sat
// between 27% and 33%, so a top-of-the-dial attack is a touch shorter than it
// used to be and nothing else moves.
constexpr float attackShareOfDecay = 0.25f;

// How hard the attack dial's curve leans towards the short end.
//
// exp(4x) spends half the travel inside the first eighth of the range, which
// is where a drum's useful attacks are - a few milliseconds, enough to take
// the click off and no more. Squared, which is what this was, spent half the
// travel on the first quarter and the rest on a swell nobody was reaching for.
constexpr float attackCurve = 4.0f;

float attackShape(float control)
{
    const auto x = juce::jlimit(0.0f, 1.0f, control);

    // exp(0) is 1, so this is EXACTLY zero at the bottom rather than nearly
    // zero. Attack defaults to zero on every voice and "Off" means precisely
    // that, so a curve that bottomed out at a few microseconds would make
    // every untouched drum very slightly late.
    return (std::exp(attackCurve * x) - 1.0f) / (std::exp(attackCurve) - 1.0f);
}

float attackSecondsFor(const VoiceSpec& spec, float attackControl, float decayControl)
{
    return attackShape(attackControl)
           * attackShareOfDecay * decaySecondsFor(spec, decayControl);
}

float tuneRatioFor(float tuneControl)
{
    const auto tune = juce::jlimit(0.0f, 1.0f, tuneControl);
    return std::pow(2.0f, tune * 2.0f - 1.0f);
}

float sampleAttackSeconds(const Sample& sample, float attackControl, float decayControl)
{
    // Against what is actually being HEARD, not against the whole file. Decay
    // gates a sample shorter, and an attack range measured off the untrimmed
    // length would keep reaching past the end of the gate - a swell with
    // nothing after it, which is the same fault the synthesised voices had.
    return attackShape(attackControl)
           * attackShareOfDecay * sampleAudibleSeconds(sample, decayControl);
}

float sampleAudibleSeconds(const Sample& sample, float decayControl)
{
    const auto decay = juce::jlimit(0.0f, 1.0f, decayControl);
    const auto whole = sample.seconds();

    // Clamped at 1, not at 3. Above the centre the law asks for a recording
    // longer than the one that was loaded, and there is no such recording.
    const auto fraction = juce::jmin(1.0f, std::pow(3.0f, decay * 2.0f - 1.0f));

    return (float) (whole * (double) fraction);
}

float hitLengthSeconds(const VoiceSpec& spec, float decayControl, float attackControl)
{
    auto seconds = attackSecondsFor(spec, attackControl, decayControl)
                   + decaySecondsFor(spec, decayControl) * lifetimeDecays + lifetimeFloorSeconds;

    // The clap's tail has not begun while it is still being struck, so its
    // strikes are added to its life rather than spent out of it.
    if (spec.engine == Engine::clap)
        seconds += clapTailSeconds;

    return juce::jmax(seconds, endFadeSeconds * 2.0f);
}

// ---- The filter ------------------------------------------------------------
void SvFilter::setCutoff(float hz, float q, double sampleRate)
{
    // Clamped well below Nyquist. g is tan(pi * fc / fs), which runs away to
    // infinity as fc approaches half the rate - and Tune can push a hat's band
    // an octave up from 8.2 kHz, which at 44.1 kHz is most of the way there.
    const auto limited = juce::jlimit(20.0, sampleRate * 0.45, (double) hz);
    const auto safeQ = juce::jmax(0.05f, q);

    g = (float) std::tan(juce::MathConstants<double>::pi * limited / sampleRate);
    r2 = 1.0f / safeQ;
    h = 1.0f / (1.0f + r2 * g + g * g);
}

void SvFilter::reset()
{
    s1 = s2 = 0.0f;
    hp = bp = lp = 0.0f;
}

void SvFilter::process(float input)
{
    hp = h * (input - (r2 + g) * s1 - s2);
    bp = g * hp + s1;
    s1 = g * hp + bp;
    lp = g * bp + s2;
    s2 = g * bp + lp;
}

// ---- The voice -------------------------------------------------------------
void Voice::prepare(double newSampleRate, juce::uint32 noiseSeed)
{
    sampleRate = juce::jmax(8000.0, newSampleRate);

    // Never zero. A xorshift seeded with zero produces zeros for ever, and a
    // silent noise generator is the kind of fault that looks like a quiet mix.
    noiseState = noiseSeed != 0 ? noiseSeed : 0x9e3779b9u;

    reset();
}

void Voice::reset()
{
    active = false;
    spec = nullptr;
    elapsed = 0;
    lifetime = 0;

    phase = phase2 = 0.0;
    for (auto& p : phases)
        p = 0.0;

    ampEnv = toneEnv = pitchEnv = fastEnv = 0.0f;
    chokeGain = 1.0f;

    lastOutput = 0.0f;
    declickValue = 0.0f;

    sample = nullptr;
    samplePosition = 0.0;
    sampleIncrement = 1.0;
    voiceTrim = 1.0f;
    chokeStep = 0.0f;

    filter.reset();

    // The mixer gains snap to whatever the first block asks for rather than
    // ramping up from zero, which would fade the kit in every time it was
    // prepared.
    gainsInitialised = false;
}

float Voice::nextNoise()
{
    // xorshift32. Ours rather than juce::Random so that the sequence depends on
    // nothing but the seed and the number of samples drawn - which is what
    // makes a render in one block and the same render in sixty-four-sample
    // pieces come out byte for byte identical.
    noiseState ^= (juce::uint32) (noiseState << 13);
    noiseState ^= (juce::uint32) (noiseState >> 17);
    noiseState ^= (juce::uint32) (noiseState << 5);

    return (float) ((double) (juce::int32) noiseState * (1.0 / 2147483648.0));
}

void Voice::trigger(float velocity, bool accent, const VoiceSpec& newSpec,
                    const VoiceParameters& params, const Sample* sampleToPlay)
{
    // Where the voice was when it was interrupted, carried across the join.
    //
    // Everything below replaces the whole of this voice's state, oscillator
    // phase included, so without this the output steps from wherever the old
    // hit's waveform happened to be straight to wherever the new one starts.
    // On a hat that step hides inside noise. On a kick - one low sine and
    // almost nothing else - it is the only discontinuity in the signal, and
    // it is plainly audible as a click.
    //
    // Read BEFORE anything is overwritten, and only when the voice was
    // actually sounding: a voice that finished a bar ago left its last output
    // at the bottom of its end fade, and carrying that across would be
    // carrying a number that means nothing.
    declickValue = active ? lastOutput : 0.0f;
    declickCoeff = coefficientFor(declickSeconds, sampleRate);

    spec = &newSpec;

    const auto clampedVelocity = juce::jlimit(0.0f, 1.0f, velocity);
    hitGain = clampedVelocity * (accent ? 1.0f : accentHeadroom);

    // Tune, Decay and Attack, read once and held. Plus or minus an octave, a
    // third to three times the voice's own length, and up to whatever the
    // spec says this particular drum has room for.
    //
    // Attack is latched with the other two rather than read every block,
    // because it describes the hit. Turning it up while a crash rings should
    // no more re-shape the strike already in the air than turning Decay
    // should shorten it.
    const auto tune = juce::jlimit(0.0f, 1.0f, params.tune.load());
    const auto decay = juce::jlimit(0.0f, 1.0f, params.decay.load());
    const auto attack = juce::jlimit(0.0f, 1.0f, params.attack.load());

    tuneRatio = tuneRatioFor(tune);
    decaySeconds = decaySecondsFor(newSpec, decay);

    baseFrequency = newSpec.frequency * tuneRatio;

    ampCoeff = coefficientFor(decaySeconds, sampleRate);
    toneCoeff = coefficientFor(decaySeconds * 0.35f, sampleRate);
    pitchCoeff = coefficientFor(juce::jmax(0.0005f, newSpec.sweepSeconds), sampleRate);

    ampEnv = 1.0f;
    toneEnv = 1.0f;
    pitchEnv = 1.0f;
    fastEnv = 1.0f;

    // The filter moves with Tune along with the pitch: tuning a drum up
    // brightens it, which is what tuning a drum does. On the two voices that
    // are nothing but noise this is the ONLY thing Tune can move, and without
    // it a maraca would have a dead control.
    filter.setCutoff(newSpec.filterHz * tuneRatio, newSpec.filterQ, sampleRate);
    filter.reset();

    // Rounded rather than truncated, so the ramp below reaches exactly 1 on
    // the sample the attack is meant to end - and so an attack shorter than
    // one sample is zero samples, which is the instant strike this kit had
    // before there was an attack at all.
    // A slot holding a recording plays the recording and leaves its own
    // circuit silent. Everything above still runs - the envelopes, the filter,
    // the pitch sweep - and is simply not read, which costs a few coefficients
    // nobody uses and keeps one code path through trigger rather than two.
    sample = (sampleToPlay != nullptr && !sampleToPlay->isEmpty()) ? sampleToPlay : nullptr;
    samplePosition = 0.0;
    voiceTrim = newSpec.outputTrim;

    fadeSamples = juce::jmax(1, (int) (endFadeSeconds * sampleRate));

    if (sample != nullptr)
    {
        // The source rate against the host's, times Tune. Tune moves speed and
        // pitch together, which is what a sampler does and what makes tuning a
        // loaded kick sound like tuning a drum rather than like a formant
        // shift.
        sampleIncrement = (sample->sourceRate / juce::jmax(1.0, sampleRate)) * (double) tuneRatio;

        // The recording arrives at whatever level it was recorded at, and
        // second-guessing that is not ours to do - so no per-engine trim.
        voiceTrim = 1.0f;

        attackSamples = (int) (sampleAttackSeconds(*sample, attack, decay) * sampleRate + 0.5);

        // Bounded by whichever runs out first: the gate Decay asks for, or the
        // recording itself. Playing past the end would be reading zeros, and
        // reading zeros for two seconds is two seconds of a voice that is not
        // sounding but is still counted as active.
        const auto gated = (int) (sampleAudibleSeconds(*sample, decay) * sampleRate);
        const auto playable = sampleIncrement > 0.0
                                  ? (int) ((double) sample->length() / sampleIncrement)
                                  : 0;

        lifetime = attackSamples + juce::jmin(gated, playable);
    }
    else
    {
        sampleIncrement = 1.0;
        attackSamples = (int) (attackSecondsFor(newSpec, attack, decay) * sampleRate + 0.5);
        lifetime = (int) (hitLengthSeconds(newSpec, decay, attack) * sampleRate);
    }

    lifetime = juce::jmax(lifetime, fadeSamples + 1);

    elapsed = 0;
    chokeGain = 1.0f;
    chokeStep = 0.0f;

    switch (newSpec.engine)
    {
        case Engine::bassDrum:
        case Engine::tone:
            // Started at phase zero, where a sine is zero, so the attack is
            // the envelope's and not a step.
            phase = 0.0;
            phase2 = 0.0;
            fastCoeff = coefficientFor(0.0012f, sampleRate);
            break;

        case Engine::snare:
            phase = 0.0;
            phase2 = 0.0;
            fastCoeff = coefficientFor(0.0012f, sampleRate);
            break;

        case Engine::clap:
        {
            // Three strikes about nine milliseconds apart and then a tail. The
            // spacing is the whole character: wider and it is three claps,
            // tighter and it is one.
            strikeSamples[0] = 0;
            strikeSamples[1] = (int) (0.009 * sampleRate);
            strikeSamples[2] = (int) (0.018 * sampleRate);
            tailSample = (int) (clapTailSeconds * sampleRate);

            fastCoeff = coefficientFor(0.0042f, sampleRate);

            // The tail has not started yet, so its envelope starts at zero
            // rather than one. This is the one engine where ampEnv is not
            // running from the first sample.
            ampEnv = 0.0f;
            // Its lifetime already allows for them - see hitLengthSeconds.
            break;
        }

        case Engine::metal:
        {
            // The oscillators are NOT reset. They free-run, exactly as the
            // hardware's do, so no two hats start from the same alignment and a
            // row of sixteenths does not turn into one sound repeated. Only
            // their rates are set here.
            const auto count = juce::jlimit(1, 6, newSpec.oscillators);
            const auto* ratios = count <= 2 ? twoOscRatios : sixOscRatios;

            for (int i = 0; i < count; ++i)
            {
                const auto hz = (double) baseFrequency * ratios[i];
                increments[i] = juce::jlimit(0.0, 0.49, hz / sampleRate);
            }

            fastCoeff = coefficientFor(0.0012f, sampleRate);
            break;
        }

        case Engine::shaker:
            // A maraca is a rush of beads, not a tick, so it gets a couple of
            // milliseconds of attack. fastEnv counts DOWN from one and the
            // engine uses one-minus-it, which is a rise without a fifth
            // envelope to carry it.
            fastCoeff = coefficientFor(0.0018f, sampleRate);
            break;
    }

    active = true;
}

void Voice::choke()
{
    if (!active)
        return;

    chokeStep = -1.0f / juce::jmax(1.0f, (float) (chokeFadeSeconds * sampleRate));
}

float Voice::renderFromSlot()
{
    const auto length = sample->length();
    const auto* data = sample->data.data();

    // Four-point Hermite, which is a handful of multiplies and is the
    // difference between a tuned drum sounding tuned and sounding dull.
    // Linear interpolation is a lowpass whose corner moves with the ratio, so
    // a sample played back slowly loses its top end - on a hi-hat that is the
    // whole sound.
    const auto index = (int) samplePosition;
    const auto t = (float) (samplePosition - (double) index);

    const auto at = [data, length](int i)
    {
        // Past either end is silence rather than a wrap or a clamp. A clamp
        // would hold the last sample as a DC step for as long as the voice
        // lived, which is a click waiting to be reported.
        return (i < 0 || i >= length) ? 0.0f : data[i];
    };

    const auto x0 = at(index - 1);
    const auto x1 = at(index);
    const auto x2 = at(index + 1);
    const auto x3 = at(index + 2);

    const auto c0 = x1;
    const auto c1 = 0.5f * (x2 - x0);
    const auto c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const auto c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);

    samplePosition += sampleIncrement;

    return ((c3 * t + c2) * t + c1) * t + c0;
}

// The voice's own circuit, whichever of the six it is.
//
// Split out from renderSample when slots learned to hold a recording: the
// two are alternatives, and a switch nested inside an if reads as though
// one were a special case of the other.
float Voice::renderFromEngine()
{
    float value = 0.0f;

    switch (spec->engine)
    {
        case Engine::bassDrum:
        {
            // The sweep IS the kick. A sine at 52 Hz is a test tone; a sine
            // that starts five times that high and falls to it in sixteen
            // milliseconds is a beater hitting a head.
            const auto frequency = (double) baseFrequency * (1.0 + (double) (spec->sweep - 1.0f) * pitchEnv);
            phase += frequency / sampleRate;
            if (phase >= 1.0)
                phase -= 1.0;

            value = std::sin(twoPi * (float) phase) * ampEnv;

            // The click, which is what lets a kick be heard on a laptop
            // speaker that cannot reproduce a note of its pitch.
            if (spec->noiseMix > 0.0f)
            {
                filter.process(nextNoise());
                value += filter.bandPass() * fastEnv * spec->noiseMix;
            }

            pitchEnv *= pitchCoeff;
            ampEnv *= ampCoeff;
            fastEnv *= fastCoeff;
            break;
        }

        case Engine::snare:
        {
            // Two tones a fifth-and-a-bit apart, which is the pair of tuned
            // shells, and a band of noise, which is the wires underneath. The
            // tones die away faster than the noise: that difference is what
            // "Snappy" balances, and it is fixed here at the spec's noiseMix.
            phase += (double) baseFrequency / sampleRate;
            phase2 += (double) baseFrequency * 1.65 / sampleRate;
            if (phase >= 1.0) phase -= 1.0;
            if (phase2 >= 1.0) phase2 -= 1.0;

            const auto tone = 0.5f * (std::sin(twoPi * (float) phase) + std::sin(twoPi * (float) phase2));

            filter.process(nextNoise());

            value = (1.0f - spec->noiseMix) * tone * toneEnv
                  + spec->noiseMix * filter.bandPass() * ampEnv;

            toneEnv *= toneCoeff;
            ampEnv *= ampCoeff;
            break;
        }

        case Engine::clap:
        {
            // Re-arm on each strike sample rather than working out an envelope
            // from elapsed time: three comparisons against an int are cheaper
            // than three exponentials, and the result cannot drift.
            for (const auto strike : strikeSamples)
                if (elapsed == strike)
                    fastEnv = 1.0f;

            if (elapsed == tailSample)
                ampEnv = 0.8f;

            filter.process(nextNoise());
            value = filter.bandPass() * juce::jmax(fastEnv, ampEnv);

            fastEnv *= fastCoeff;
            ampEnv *= ampCoeff;
            break;
        }

        case Engine::metal:
        {
            const auto count = juce::jlimit(1, 6, spec->oscillators);
            float sum = 0.0f;

            for (int i = 0; i < count; ++i)
            {
                phases[i] += increments[i];
                if (phases[i] >= 1.0)
                    phases[i] -= 1.0;

                sum += phases[i] < 0.5 ? 1.0f : -1.0f;
            }

            // Squares, unfiltered by anything but the band below, which means
            // they alias. That is not an oversight: the 808's own oscillators
            // are square and its cymbals are made of what comes out of a
            // narrow band around them. Band-limiting them here would take the
            // grit out of the one sound whose whole identity is grit.
            filter.process(sum / (float) count);
            value = filter.bandPass() * ampEnv;

            ampEnv *= ampCoeff;
            break;
        }

        case Engine::tone:
        {
            const auto frequency = (double) baseFrequency * (1.0 + (double) (spec->sweep - 1.0f) * pitchEnv);
            phase += frequency / sampleRate;
            if (phase >= 1.0)
                phase -= 1.0;

            value = std::sin(twoPi * (float) phase);

            // A second oscillator, deliberately not in any harmonic relation
            // to the first. One sine is a clave; two that disagree are a rim.
            if (spec->oscillators >= 2)
            {
                phase2 += frequency * 0.53 / sampleRate;
                if (phase2 >= 1.0)
                    phase2 -= 1.0;

                value = 0.5f * (value + std::sin(twoPi * (float) phase2));
            }

            value *= ampEnv;

            if (spec->noiseMix > 0.0f)
            {
                filter.process(nextNoise());
                value = (1.0f - spec->noiseMix) * value
                      + spec->noiseMix * filter.bandPass() * ampEnv;
            }

            pitchEnv *= pitchCoeff;
            ampEnv *= ampCoeff;
            break;
        }

        case Engine::shaker:
        {
            filter.process(nextNoise());
            value = filter.bandPass() * ampEnv * (1.0f - fastEnv);

            ampEnv *= ampCoeff;
            fastEnv *= fastCoeff;
            break;
        }
    }

    return value;
}

float Voice::renderSample()
{
    if (!active || spec == nullptr)
        return 0.0f;

    if (elapsed >= lifetime)
    {
        active = false;
        return 0.0f;
    }

    // A recording instead of the circuit. The two are alternatives, which is
    // what the plan meant by a slot being EITHER a synthesised voice or a
    // loaded one - everything downstream of here, the envelope and the fades
    // and the pan, does not care which it got.
    const auto value = sample != nullptr ? renderFromSlot() : renderFromEngine();

    // The fade that bounds the voice's life, and the faster one a choke asks
    // for. Both multiply in here rather than being folded into ampEnv, because
    // a choke arrives partway through a hit and cannot be built into a
    // coefficient chosen when the hit started.
    float fade = 1.0f;
    const auto remaining = lifetime - elapsed;
    if (remaining < fadeSamples)
        fade = (float) remaining / (float) fadeSamples;

    // The attack, as a straight ramp from silence.
    //
    // Straight rather than curved, because on a drum this is nearly always a
    // few milliseconds spent taking an edge off a transient, and over that
    // span no curve is distinguishable from a line. The one case where it is
    // - a crash swelling over half a second - a line is the shape people draw
    // anyway.
    //
    // Worked out from `elapsed` rather than accumulated per sample, so it is
    // the same number at the same sample however the block was cut. An
    // accumulating ramp would drift, and the whole suite rests on a chopped
    // render matching a long one exactly.
    if (elapsed < attackSamples)
        fade *= (float) elapsed / (float) attackSamples;

    ++elapsed;

    if (chokeStep < 0.0f)
    {
        chokeGain += chokeStep;

        if (chokeGain <= 0.0f)
        {
            chokeGain = 0.0f;
            active = false;
        }
    }

    // The new hit, plus whatever the interrupted one was still worth, let go
    // of over a couple of milliseconds.
    //
    // Added rather than crossfaded, and the arithmetic is the whole point: at
    // the first sample of a new hit the offset is exactly the last sample of
    // the old one, so the two join with no step at all. What the new hit is
    // doing at that instant is almost nothing - a sine starts at zero - so
    // the join is continuous and then the offset gets out of the way.
    //
    // Zero on a first strike, where there is nothing to carry, and zero
    // within a few milliseconds of every other one. It costs one multiply.
    //
    // The choke is applied to it as well as to the hit, so that cutting a
    // voice cuts ALL of it. Nothing on the way in is affected - a fresh
    // trigger sets chokeGain back to 1 - and the case it covers is narrow: a
    // hat retriggered and then choked a millisecond later, where the leftover
    // would otherwise be the one part of the voice the choke did not reach.
    // A path where "cut" does not cut everything is how the next click report
    // gets written.
    const auto output = (value * fade * hitGain * voiceTrim + declickValue) * chokeGain;

    declickValue *= declickCoeff;
    lastOutput = output;

    return output;
}

void Voice::renderAdding(juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                         const VoiceParameters& params, float masterLevel)
{
    if (!active || numSamples <= 0)
        return;

    const auto numChannels = buffer.getNumChannels();
    if (numChannels <= 0)
        return;

    const auto level = juce::jlimit(0.0f, 1.0f, params.level.load());
    const auto pan = juce::jlimit(0.0f, 1.0f, params.pan.load());
    const auto gain = level * masterLevel;

    // Constant power, so a voice swept across the image does not dip through
    // the middle. A centred voice is 0.707 in each channel, which is the same
    // total power as hard left.
    float targetLeft = gain * 0.70710678f;
    float targetRight = targetLeft;

    if (numChannels > 1)
    {
        const auto angle = pan * juce::MathConstants<float>::halfPi;
        targetLeft = std::cos(angle) * gain;
        targetRight = std::sin(angle) * gain;
    }

    if (!gainsInitialised)
    {
        leftGain = targetLeft;
        rightGain = targetRight;
        gainsInitialised = true;
    }

    // A ramp across the block, which de-zippers a level or pan that moved. It
    // is a smoother and not a promise: a block split in two by a hit landing
    // mid-block ramps over each half, so a render chopped into pieces matches
    // one long render exactly only while the controls are still. That is the
    // condition the identity test states, and it is the condition a smoother
    // can honestly offer.
    const auto stepLeft = (targetLeft - leftGain) / (float) numSamples;
    const auto stepRight = (targetRight - rightGain) / (float) numSamples;

    auto* left = buffer.getWritePointer(0, startSample);
    auto* right = numChannels > 1 ? buffer.getWritePointer(1, startSample) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        if (!active)
            break;

        const auto value = renderSample();

        leftGain += stepLeft;
        rightGain += stepRight;

        left[i] += value * leftGain;

        if (right != nullptr)
            right[i] += value * rightGain;
    }

    // Snapped to the target rather than left wherever the accumulated steps
    // landed, so a long run of blocks cannot drift away from the dial.
    leftGain = targetLeft;
    rightGain = targetRight;
}
} // namespace drums
