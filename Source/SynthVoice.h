// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "WavetableOscillator.h"
#include "SynthParameters.h"

namespace wavelathe
{
constexpr int maxUnisonVoices = 7;

class SynthVoice : public juce::SynthesiserVoice
{
public:
    SynthVoice(const WavetableProvider& tableProvider, const SynthParameters& sharedParams);

    bool canPlaySound(juce::SynthesiserSound*) override { return true; }

    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int pitchWheelPos) override;
    void stopNote(float velocity, bool allowTailOff) override;

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

    void prepare(double sampleRate, int maximumBlockSize);

    // Makes the noise source repeat itself from one note to the next. Off for
    // live playing, where genuinely random noise is the point and a repeated
    // note reusing the same noise sounds mechanical; on for offline rendering,
    // where the same patch has to measure the same every time it is tried.
    void setRepeatableNoise(int seedValue) { noiseSeed = seedValue; repeatableNoise = true; }
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

    // Tempo the LFOs sync to, refreshed by the processor each block.
    void setBpm(double newBpm) { currentBpm = newBpm; }

    // What pitch the current note's glide actually started from, or zero if it
    // started on its own pitch.
    //
    // Exists so a test can read the decision rather than re-derive it. The first
    // version of the voice-allocator test kept its own copy of the glide rule
    // and reported nonsense the moment the rule changed - which is the trouble
    // with a test that models what it is meant to be measuring.
    float getGlideStartHz() const { return glideStartedAtHz; }

    // The pitch this voice is sounding right now, mid-glide included.
    //
    // Same reason as above: a test that worked this out for itself from the
    // note number and the glide curve would be testing its own arithmetic. It
    // is the value handed to every oscillator's setFrequency below, not a model
    // of it - and the legato test checks it against what the zero-crossing
    // measure hears, so the two agree before either is trusted alone.
    float getCurrentHz() const { return baseFrequencyHz; }

private:
    const SynthParameters& params;
    const WavetableProvider& provider;
    std::array<WavetableOscillator, maxUnisonVoices> oscillators;
    std::array<WavetableOscillator, maxUnisonVoices> oscillators2;
    juce::ADSR adsr;      // hardwired to level
    juce::ADSR modEnvelope; // routable: wavetable position and cutoff
    // One filter per channel: with the stereo LFO engaged the two sides are
    // modulated a quarter-cycle apart, so they need independent cutoffs.
    juce::dsp::StateVariableTPTFilter<float> filterLeft, filterRight;
    // Second stage, cascaded for the 24 dB/octave lowpass.
    juce::dsp::StateVariableTPTFilter<float> filterLeft2, filterRight2;
    juce::dsp::ProcessSpec spec{};

    // Sub oscillator and noise source, both per voice.
    float subPhase = 0.0f;
    juce::Random noiseRandom;
    int noiseSeed = 0;
    bool repeatableNoise = false;
    float pinkState = 0.0f;

    // Where the pitch is now and where it is heading. They differ only while a
    // glide is in progress; with glide off the one catches the other at once.
    float baseFrequencyHz = 440.0f;
    float targetFrequencyHz = 440.0f;

    // The pitch THIS voice last played, which is where its next glide starts
    // from. Per voice rather than shared - see startNote for what sharing it
    // did to chords. Zero means this voice has not sounded yet, and a first
    // note has nothing to slide from.
    float lastNoteFrequencyHz = 0.0f;

    // What the current note's glide started from, for getGlideStartHz.
    float glideStartedAtHz = 0.0f;

    // Whether this voice has already been told its note is over. stopNote can
    // arrive twice - see stopNote for why - and only the first is a key going up.
    bool noteIsReleasing = false;
    float noteVelocity = 1.0f;
    float lfoPhase = 0.0f;
    float lfo2Phase = 0.0f;
    double currentBpm = 120.0;

    static float softClip(float x, float driveAmount);
};
} // namespace wavelathe
