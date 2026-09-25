// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "SynthVoice.h"
#include "SynthSound.h"
#include <cmath>
#include <iostream>
#include <memory>

using namespace wavelathe;

namespace
{
int failures = 0;

void check(bool condition, const juce::String& what)
{
    if (!condition)
    {
        std::cout << "  FAIL: " << what << std::endl;
        ++failures;
    }
}

constexpr double sampleRate = 44100.0;
constexpr int blockSize = 512;
constexpr int blocks = 8;

// A stand-in for brightness: the energy in the sample-to-sample difference
// against the energy in the signal. Differencing is a rising filter, so the
// ratio goes up with harmonic content and is unaffected by loudness - which is
// the point, since loudness is the thing velocity already controlled.
//
// Energy, not total movement: the total distance a waveform travels in a cycle
// is about the same whatever its shape, so measuring that tells a sine from a
// saw not at all. The first version of this test did exactly that and reported
// a wide-open filter as no brighter than a shut one.
double brightnessOf(const juce::AudioBuffer<float>& buffer)
{
    const auto* data = buffer.getReadPointer(0);
    double differenceEnergy = 0.0;
    double signalEnergy = 0.0;

    for (int i = 1; i < buffer.getNumSamples(); ++i)
    {
        double difference = (double) data[i] - (double) data[i - 1];
        differenceEnergy += difference * difference;
        signalEnergy += (double) data[i] * (double) data[i];
    }

    return signalEnergy > 1.0e-12 ? std::sqrt(differenceEnergy / signalEnergy) : 0.0;
}

double peakOf(const juce::AudioBuffer<float>& buffer)
{
    return (double) buffer.getMagnitude(0, buffer.getNumSamples());
}

// Renders one note at one velocity and hands back what came out.
void render(SynthVoice& voice, float velocity, juce::AudioBuffer<float>& destination)
{
    destination.setSize(2, blockSize * blocks, false, true, true);
    destination.clear();

    voice.startNote(60, velocity, nullptr, 8192);

    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> chunk(destination.getArrayOfWritePointers(), 2, block * blockSize,
                                        blockSize);
        voice.renderNextBlock(chunk, 0, blockSize);
    }

    voice.stopNote(0.0f, false);
}

void setupVoicePatch(SynthParameters& p)
{
    p.wavePosition = 0.5f;   // saw, so there is something above the fundamental
    p.osc1Level = 1.0f;
    p.osc2Level = 0.0f;
    p.subLevel = 0.0f;
    p.noiseLevel = 0.0f;
    p.unisonVoices = 1.0f;
    p.attack = 0.001f;
    p.decay = 0.2f;
    p.sustain = 1.0f;
    p.release = 0.1f;
    p.filterType = 0.0f;     // 12 dB lowpass
    p.filterCutoffHz = 300.0f; // low, so opening it is audible
    p.filterResonance = 0.2f;
    p.modEnvToCutoff = 0.0f;
    p.lfoDepth = 0.0f;
    p.driveAmount = 0.0f;
    p.masterGain = 0.8f;
}
} // namespace

int main()
{
    std::cout << "Expression test - velocity, wheel and pressure as modulation" << std::endl
              << std::endl;

    // On the heap: a full set of mip-mapped tables is a couple of megabytes,
    // which is more than a thread stack holds.
    auto tables = std::make_unique<WavetableSet>();
    WavetableProvider provider;
    provider.active.store(tables.get());

    // ---- Does the measure even see a filter sweep? -------------------------
    std::cout << "Sanity - cutoff moved by hand:" << std::endl;
    {
        SynthParameters p;
        setupVoicePatch(p);
        p.velocityToAmp = 0.0f;

        auto v1 = std::make_unique<SynthVoice>(provider, p);
        v1->prepare(sampleRate, blockSize);
        juce::AudioBuffer<float> dark;
        render(*v1, 0.8f, dark);

        p.filterCutoffHz = 4800.0f;
        auto v2 = std::make_unique<SynthVoice>(provider, p);
        v2->prepare(sampleRate, blockSize);
        juce::AudioBuffer<float> bright;
        render(*v2, 0.8f, bright);

        std::cout << "  300 Hz: brightness " << juce::String(brightnessOf(dark), 4) << ", peak "
                  << juce::String(peakOf(dark), 3) << std::endl;
        std::cout << "  4800 Hz: brightness " << juce::String(brightnessOf(bright), 4) << ", peak "
                  << juce::String(peakOf(bright), 3) << std::endl;
    }

    // ---- Velocity opens the filter ----------------------------------------
    std::cout << "Velocity to cutoff:" << std::endl;
    {
        SynthParameters p;
        setupVoicePatch(p);

        // Level taken out of it entirely, so the only thing that can differ
        // between the two takes is tone.
        p.velocityToAmp = 0.0f;
        p.velocityToCutoff = 1.0f;

        auto voicePtr = std::make_unique<SynthVoice>(provider, p);
        auto& voice = *voicePtr;
        voice.prepare(sampleRate, blockSize);

        juce::AudioBuffer<float> soft, hard;
        render(voice, 0.2f, soft);
        render(voice, 1.0f, hard);

        double softBright = brightnessOf(soft);
        double hardBright = brightnessOf(hard);
        double softPeak = peakOf(soft);
        double hardPeak = peakOf(hard);

        std::cout << "  softly struck: brightness " << juce::String(softBright, 4) << ", peak "
                  << juce::String(softPeak, 3) << std::endl;
        std::cout << "  hard struck:   brightness " << juce::String(hardBright, 4) << ", peak "
                  << juce::String(hardPeak, 3) << std::endl;

        check(hardBright > softBright * 1.5,
              "a hard strike is audibly brighter, not a little brighter");

        // The peak rises too, but that is the filter passing more of the sound
        // rather than a gain change - moving the cutoff by hand above did
        // exactly the same thing, and the zero-depth case below shows velocity
        // leaves the level alone when it is not routed to it.
        check(hardPeak > softPeak, "and the opened filter passes more, as moving the cutoff does");
    }

    // ---- Velocity to level, which is what it always did --------------------
    std::cout << std::endl << "Velocity to level:" << std::endl;
    {
        SynthParameters p;
        setupVoicePatch(p);
        p.velocityToAmp = 1.0f;
        p.velocityToCutoff = 0.0f;

        auto voicePtr = std::make_unique<SynthVoice>(provider, p);
        auto& voice = *voicePtr;
        voice.prepare(sampleRate, blockSize);

        juce::AudioBuffer<float> soft, hard;
        render(voice, 0.25f, soft);
        render(voice, 1.0f, hard);

        double softPeak = peakOf(soft);
        double hardPeak = peakOf(hard);
        std::cout << "  peak at 0.25 velocity " << juce::String(softPeak, 3) << ", at 1.0 "
                  << juce::String(hardPeak, 3) << std::endl;
        check(hardPeak > softPeak * 2.5, "full depth still scales the level as it always has");

        // And with the routing at zero, velocity leaves the level alone.
        SynthParameters flat;
        setupVoicePatch(flat);
        flat.velocityToAmp = 0.0f;

        auto steadyPtr = std::make_unique<SynthVoice>(provider, flat);
        auto& steady = *steadyPtr;
        steady.prepare(sampleRate, blockSize);

        juce::AudioBuffer<float> quiet, loud;
        render(steady, 0.25f, quiet);
        render(steady, 1.0f, loud);

        double quietPeak = peakOf(quiet);
        double loudPeak = peakOf(loud);
        std::cout << "  at zero depth: " << juce::String(quietPeak, 3) << " and "
                  << juce::String(loudPeak, 3) << std::endl;
        check(std::abs(loudPeak - quietPeak) < quietPeak * 0.05 + 0.001,
              "at zero depth velocity does not touch the level at all");
    }

    // ---- The wheel moves the sound while the note is sounding --------------
    std::cout << std::endl << "Mod wheel:" << std::endl;
    {
        SynthParameters p;
        setupVoicePatch(p);
        p.velocityToAmp = 0.0f;
        p.wheelToCutoff = 1.0f;
        p.modWheel = 0.0f;

        auto voicePtr = std::make_unique<SynthVoice>(provider, p);
        auto& voice = *voicePtr;
        voice.prepare(sampleRate, blockSize);

        juce::AudioBuffer<float> closed, open;
        render(voice, 0.8f, closed);

        p.modWheel = 1.0f;
        render(voice, 0.8f, open);

        double closedBright = brightnessOf(closed);
        double openBright = brightnessOf(open);
        std::cout << "  wheel down " << juce::String(closedBright, 4) << ", wheel up "
                  << juce::String(openBright, 4) << std::endl;
        check(openBright > closedBright * 1.5, "the wheel opens the filter");
    }

    // ---- Aftertouch, the same but from key pressure ------------------------
    std::cout << std::endl << "Aftertouch:" << std::endl;
    {
        SynthParameters p;
        setupVoicePatch(p);
        p.velocityToAmp = 0.0f;
        p.pressureToCutoff = 1.0f;
        p.channelPressure = 0.0f;

        auto voicePtr = std::make_unique<SynthVoice>(provider, p);
        auto& voice = *voicePtr;
        voice.prepare(sampleRate, blockSize);

        juce::AudioBuffer<float> light, heavy;
        render(voice, 0.8f, light);

        p.channelPressure = 1.0f;
        render(voice, 0.8f, heavy);

        double lightBright = brightnessOf(light);
        double heavyBright = brightnessOf(heavy);
        std::cout << "  no pressure " << juce::String(lightBright, 4) << ", full pressure "
                  << juce::String(heavyBright, 4) << std::endl;
        check(heavyBright > lightBright * 1.5, "leaning on the key opens the filter");
    }

    // ---- A patch that routes nothing sounds as it always did ---------------
    std::cout << std::endl << "Defaults:" << std::endl;
    {
        SynthParameters p;
        check(p.velocityToAmp.load() > 0.99f,
              "velocity still sets the level out of the box, as it always has");
        check(p.velocityToCutoff.load() == 0.0f && p.wheelToCutoff.load() == 0.0f
                  && p.pressureToCutoff.load() == 0.0f && p.velocityToWave.load() == 0.0f
                  && p.wheelToWave.load() == 0.0f,
              "and nothing else is routed until you ask for it");
    }

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "ALL EXPRESSION TESTS PASSED" << std::endl;
    else
        std::cout << failures << " EXPRESSION TEST(S) FAILED" << std::endl;

    return failures == 0 ? 0 : 1;
}
