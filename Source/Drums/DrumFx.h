// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "DrumParameters.h"

#include <array>

namespace drums
{
// How long after the last thing played through it a chain keeps being
// processed, and therefore how much a routed slot adds to what a host has to
// render past the final hit.
//
// Six seconds covers the reverb at its largest and the delay at its longest
// with feedback most of the way up; past that both are below anything audible
// under a drum kit. Wrong in the safe direction either way: too long wastes a
// few microseconds a block, too short clips a tail.
constexpr double fxTailSeconds = 6.0;

// The longest echo one voice can hold.
//
// A whole second, which is a quarter note at 60bpm and every shorter division
// at any tempo above that - so the Time dial is limited to a quarter and
// shorter and nothing it can be set to is ever clipped in normal use. Below
// 60bpm the longest setting is clamped, which is stated rather than hidden.
//
// The number is a memory decision and not a musical one. Each voice needs its
// own line, because the chain is per voice; twelve stereo seconds at 96kHz
// is about twelve megabytes and the twelve seconds a shared bus could afford
// would have been a hundred and fifty.
constexpr double maxDelaySeconds = 1.0;

// ---- One voice's effect chain ----------------------------------------------
//
// Two slots in series: the voice goes through slot 1's unit and then through
// slot 2's, each mixed back against what went into it. An Amount of zero is
// not "quiet", it is the input itself, sample for sample - which is what lets
// an unused chain cost nothing and be provably out of the path.
//
// Private processors, shared settings. Each voice owns one of each unit and
// runs whichever ones its slots name; the rack's twelve dials say how all
// twelve copies are set. That is what a chain forces: slot 2 needs slot 1's
// output for THIS voice, and a bus carrying all twelve summed together
// cannot give it that.
//
// The cost is real and worth stating: twelve reverbs instead of one, so two
// drums in "Reverb 1" are in two identical rooms rather than in the same one.
// A shared room is what a send bus buys, and a send bus cannot be chained.
//
// Written here rather than reached for from juce_dsp, which this library
// deliberately does not link - see the note on the DrumKit target in
// CMakeLists.txt. juce::Reverb is the one exception and it is not one: it
// lives in juce_audio_basics, which is already a dependency.
class VoiceFx
{
public:
    // Spelled out because the non-copyable macro at the bottom declares a
    // constructor, and a class with any user-declared constructor does not get
    // an implicit default one. The same line, for the same reason, as Kit's.
    VoiceFx() = default;

    // maximumBlockSize is the longest run this will ever be asked to process
    // in one call. The kit splits anything longer.
    void prepare(double newSampleRate, int maximumBlockSize);
    void reset();

    // Runs `signal` through both slots IN PLACE. `wet` is scratch space the
    // caller owns - one buffer shared by all twelve chains, since only one is
    // ever mid-process.
    //
    // `fed` says whether the voice actually produced anything this run. It is
    // not the same question as "is a slot routed": a reverb whose drum has
    // finished still has three seconds of tail to give back, and a chain that
    // stopped the moment its voice fell silent would cut that dead.
    void process(juce::AudioBuffer<float>& signal, juce::AudioBuffer<float>& wet,
                 int numSamples, const VoiceParameters& voice, const FxParameters& params,
                 bool fed);

    // Neither in use nor still ringing, so the caller can skip this voice
    // entirely rather than running silence through four units.
    bool isIdle() const { return idleSamples > tailSamples; }

private:
    // One slot: runs `wet` through a unit and mixes it back over `signal`.
    void runSlot(int slot, int unit, float amount, juce::AudioBuffer<float>& signal,
                 juce::AudioBuffer<float>& wet, int numSamples, const FxParameters& params);

    void processReverb(juce::AudioBuffer<float>& bus, int numSamples, const FxParameters& params);
    void processDrive(juce::AudioBuffer<float>& bus, int numSamples, const FxParameters& params);
    void processEq(juce::AudioBuffer<float>& bus, int numSamples, const FxParameters& params);
    void processDelay(juce::AudioBuffer<float>& bus, int numSamples, const FxParameters& params);

    double sampleRate = 44100.0;
    int tailSamples = (int) (fxTailSeconds * 44100.0);
    int idleSamples = (int) (fxTailSeconds * 44100.0) + 1;

    // Where each slot's mix actually is, as against where its dial is. Moved
    // towards the dial across a run rather than jumped to, because a mix
    // stepped between blocks clicks - the same de-zippering the voices do to
    // Level and Pan. With a still dial the step is exactly zero, which is what
    // keeps a chopped-up render bit-identical to a long one.
    std::array<float, 2> slotMix{};
    bool mixInitialised = false;

    juce::Reverb reverb;
    juce::Reverb::Parameters reverbParameters;
    float builtReverbSize = -1.0f, builtReverbDamp = -1.0f;

    // The drive's tone control: a one-pole lowpass per channel, swept across
    // the range where a distorted drum either keeps its top end or loses it.
    std::array<float, 2> toneState{};

    // The EQ's two corners, each a one-pole splitting the signal into low,
    // middle and high so the three can be weighed against each other. Fixed
    // corners rather than sweepable ones, because this is for tilting a drum
    // and a parametric would be four more dials in a panel with room for
    // three.
    std::array<float, 2> eqLowState{};
    std::array<float, 2> eqHighState{};

    // One line per channel. Sized in prepare, never on the audio thread.
    juce::AudioBuffer<float> delayLine;
    int delayWritePosition = 0;

    // The delay time actually in use, moved towards the dial a little each
    // sample. Jumping straight to a new time would step the read head through
    // the line and click.
    float smoothedDelaySamples = 0.0f;
    bool delayTimeInitialised = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceFx)
};
} // namespace drums
