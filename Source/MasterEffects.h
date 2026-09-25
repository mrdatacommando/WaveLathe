// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "SynthParameters.h"
#include "TempoSync.h"

namespace wavelathe
{
// Effects applied once to the mixed output of all voices, rather than per
// voice. Reverb and delay in particular only make sense here: run per voice
// they would multiply with polyphony, both in cost and in wash.
//
// Where the three bands sit. Named here rather than written into
// updateEqualiser, because the spectrum view draws this EQ's response over the
// signal and must ask the same numbers the filtering uses - a curve drawn from
// a second copy of them is a curve that drifts the first time one is changed.
//
// float, not double, because every one of them is handed to
// juce::dsp::IIR::Coefficients<float>, whose makers take the coefficient type
// for frequency and Q and only the sample rate as a double. As doubles they
// were truncated at each of six call sites, which is six /W4 warnings saying
// the same true thing: these are float coefficients and always were.
namespace eq
{
constexpr float lowFrequency = 220.0f;
constexpr float lowQ = 0.7071f;
constexpr float midFrequency = 1200.0f;
constexpr float midQ = 0.8f;
constexpr float highFrequency = 4500.0f;
constexpr float highQ = 0.7071f;
} // namespace eq

// Everything one pass of the chain reads, as plain values.
//
// There are two sets of these dials now - the Synth page's, and the mix bus's
// on the Master page - and one chain class serves both. So the chain reads
// this rather than SynthParameters, and each caller fills it from wherever its
// dials live. Raw, as the dials hold them: the chain does its own clamping, and
// tailLengthSeconds reads the delay time unclamped on purpose (see there).
struct EffectSettings
{
    float distortion = 0.0f;

    float chorusRate = 0.6f;
    float chorusDepth = 0.35f;
    float chorusMix = 0.0f;

    float phaserRate = 0.4f;
    float phaserFeedback = 0.4f;
    float phaserMix = 0.0f;

    float delayTimeMs = 320.0f;
    float delayFeedback = 0.35f;
    float delayMix = 0.0f;
    bool delaySync = false;

    float reverbSize = 0.5f;
    float reverbMix = 0.0f;

    float eqLowDb = 0.0f;
    float eqMidDb = 0.0f;
    float eqHighDb = 0.0f;

    // Linear, applied after the EQ. Unity for the Synth page's chain, whose
    // Gain dial is a voice level applied in SynthVoice rather than a stage
    // here - so that path does exactly what it did before this field existed.
    float outputGain = 1.0f;
};

// The Synth page's dials.
EffectSettings effectSettingsFrom(const SynthParameters& params);

// The mix bus's, from SynthParameters::busFx.
EffectSettings busEffectSettingsFrom(const SynthParameters& params);

// Chain order is distortion -> chorus -> phaser -> delay -> reverb -> EQ, the
// conventional arrangement: distortion shapes the dry tone, the modulation
// effects thicken and move it, the delay repeats that, the reverb places the
// whole thing in a space, and the EQ is the last word on the finished sound.
class MasterEffects
{
public:
    void prepare(double sampleRate, int maximumBlockSize, int numChannels);
    void reset();
    // Any length of buffer is accepted: anything longer than the block size this
    // was prepared for is split up internally. The chorus and phaser size their
    // working buffers when prepared, so handing them a longer block than that
    // runs off the end of those buffers - and a caller that renders a whole
    // note in one go has no way to know what length is safe.
    void process(juce::AudioBuffer<float>& buffer, const EffectSettings& settings, double bpm);

    // The Synth page's chain, as it was always called.
    void process(juce::AudioBuffer<float>& buffer, const SynthParameters& params, double bpm)
    {
        process(buffer, effectSettingsFrom(params), bpm);
    }

private:
    void processChunk(juce::AudioBuffer<float>& buffer, const EffectSettings& settings, double bpm);

    double sampleRate = 44100.0;
    int maximumBlockSize = 512;
    bool prepared = false;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine{96000};
    juce::Reverb reverb;
    juce::Reverb::Parameters reverbParameters;

    juce::dsp::Chorus<float> chorus;
    juce::dsp::Phaser<float> phaser;

    // Shelves at the ends and a bell in the middle - the three controls a tone
    // stack gives you, rather than a full parametric nobody would reach for.
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                    juce::dsp::IIR::Coefficients<float>> lowShelf, midBell, highShelf;
    float builtLowGain = 0.0f, builtMidGain = 0.0f, builtHighGain = 0.0f;
    void updateEqualiser(float lowDb, float midDb, float highDb);

public:
    // The EQ's response at one frequency, as a gain multiplier, built from the
    // same coefficients the audio path uses. Static because a view asking
    // "what does this EQ do at 300 Hz" is asking about the settings, not about
    // any particular instance - and because the instance it would otherwise
    // have to ask is being run by the audio thread.
    static float getEqualiserMagnitude(float lowDb, float midDb, float highDb, double frequency,
                                        double sampleRate);

private:

    // Smoothed so turning a dial mid-note doesn't click or zipper.
    juce::SmoothedValue<float> smoothedDistortion, smoothedDelayMix, smoothedDelayFeedback,
        smoothedDelaySamples, smoothedReverbMix;

    // Multiplicative, because a gain glides evenly in ratio rather than in
    // amplitude - a linear ramp from 0.25 to 1 spends most of its time loud.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedOutputGain;

    // Set by reset(), cleared by the first block after it. A note has to start
    // at the settings it was given rather than gliding to them over ~50ms from
    // whatever the last one left behind - inaudible when playing, but it makes
    // an offline render depend on what was rendered before it.
    bool snapSmoothers = true;

    static float softClip(float x, float amount);
};

// How long the sound rings on after the last note has let go - which is what a
// host keeps rendering for when it bounces, freezes or exports a track. Get it
// short and the end of the sound is simply cut off; get it long and every
// export sits waiting for silence.
//
// Three things outlast the note: the amp envelope's release, the delay, and the
// reverb. Chorus and phaser colour what is already there rather than outliving
// it, and the filter and EQ shape it without holding it.
//
// Lives beside the chain that does the ringing rather than on the processor, so
// it can be measured against a real render - which is the only way to know
// whether an estimate covers what actually comes out.
double tailLengthSeconds(const SynthParameters& params);

// The part of that the effects add on their own - the delay's repeats and the
// reverb's decay, with no note underneath. What the mix bus contributes, since
// it rings on after whatever went into it has stopped.
double effectsTailSeconds(const EffectSettings& settings);
} // namespace wavelathe
