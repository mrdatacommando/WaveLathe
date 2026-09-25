// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MasterPlugin/MasterPluginProcessor.h"

#include <cmath>
#include <iostream>

using namespace wavelathe;
using C = mastering::Control;

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

bool nearlyEqual(float a, float b, float tolerance = 0.01f)
{
    return std::abs(a - b) <= tolerance;
}

// Every control, and a value for it that is inside its range but nowhere near
// its default - so a control that quietly failed to be written shows up as its
// default rather than passing by coincidence.
struct Probe
{
    C control;
    const char* name;
    float value;
};

const Probe probes[] = {
    {C::enabled, "enabled", 0.0f},
    {C::satDrive, "satDrive", 0.62f},
    {C::satMix, "satMix", 0.41f},
    {C::compThreshold, "compThreshold", -27.5f},
    {C::compRatio, "compRatio", 7.5f},
    {C::compAttack, "compAttack", 3.4f},
    {C::compRelease, "compRelease", 415.0f},
    {C::compMakeup, "compMakeup", 9.5f},
    {C::compMix, "compMix", 0.34f},
    {C::limitCeiling, "limitCeiling", -3.7f},
    {C::limitRelease, "limitRelease", 175.0f},
    {C::trim, "trim", -7.5f},
};
} // namespace

int main()
{
    // APVTS and the parameter objects want the JUCE machinery up before any of
    // this is touched.
    juce::ScopedJuceInitialiser_GUI juceSetup;

    std::cout << "WaveLathe Master plugin" << std::endl << std::endl;

    // ---- Every control reaches a parameter ---------------------------------
    // The enum and the parameter ids are two lists that have to agree, and
    // nothing forces them to. A control whose id was mistyped would read zero
    // for ever and write nowhere, silently, which on a mastering bus means a
    // dial that does nothing and no reason why.
    {
        MasterPluginProcessor processor;

        check((int) (sizeof(probes) / sizeof(probes[0])) == (int) C::count,
              "the probe list covers every control in the enum");

        const auto& published = processor.getParameters();
        check(published.size() == (int) C::count,
              "the host is offered one parameter per control");

        std::cout << "  " << published.size() << " parameters published, "
                  << (int) C::count << " controls in the enum" << std::endl;
    }

    // ---- Set, then get -----------------------------------------------------
    {
        MasterPluginProcessor processor;

        for (const auto& probe : probes)
        {
            processor.set(probe.control, probe.value);

            const float readBack = processor.get(probe.control);
            check(nearlyEqual(readBack, probe.value),
                  juce::String(probe.name) + " reads back what was written to it");
        }

        std::cout << "  all " << (int) C::count << " controls round-trip through the host parameters"
                  << std::endl;
    }

    // ---- The switch is ON by default ---------------------------------------
    // Deliberately the opposite of the synth's Master page, where off protects
    // sixty-five factory presets voiced without it. Here somebody has put an
    // effect on a bus on purpose, and one that does nothing until a second
    // switch is found reads as broken. Worth a test because it is the kind of
    // default that gets "tidied" into consistency by someone who has only seen
    // one of the two.
    {
        MasterPluginProcessor processor;
        check(processor.get(C::enabled) > 0.5f, "the effect plugin starts switched on");
        std::cout << "  starts enabled, unlike the page inside the synth" << std::endl;
    }

    // ---- State survives a save and reload -----------------------------------
    {
        MasterPluginProcessor saved;
        for (const auto& probe : probes)
            saved.set(probe.control, probe.value);

        juce::MemoryBlock block;
        saved.getStateInformation(block);
        check(block.getSize() > 0, "the plugin writes some state");

        MasterPluginProcessor restored;
        restored.setStateInformation(block.getData(), (int) block.getSize());

        for (const auto& probe : probes)
            check(nearlyEqual(restored.get(probe.control), probe.value),
                  juce::String(probe.name) + " survives save and reload");

        std::cout << "  every control survives a project save" << std::endl;
    }

    // ---- It actually processes ---------------------------------------------
    // Not a test of the chain, which has its own suite. A test that this
    // processor is wired to one at all: without the call in processBlock
    // everything above would still pass and the plugin would be a bypass with
    // twelve decorative dials.
    {
        MasterPluginProcessor processor;
        processor.setPlayConfigDetails(2, 2, 44100.0, 512);
        processor.prepareToPlay(44100.0, 512);

        check(processor.getLatencySamples() > 0,
              "the plugin reports the latency its chain costs");

        processor.set(C::enabled, 1.0f);
        processor.set(C::limitCeiling, -6.0f);
        processor.set(C::trim, 18.0f); // drive it well past the ceiling

        juce::AudioBuffer<float> buffer(2, 512);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 512; ++i)
                buffer.setSample(channel, i,
                                 (float) (0.5 * std::sin(2.0 * juce::MathConstants<double>::pi
                                                         * 220.0 * i / 44100.0)));

        juce::MidiBuffer midi;
        processor.processBlock(buffer, midi);

        const float ceiling = juce::Decibels::decibelsToGain(-6.0f);
        check(buffer.getMagnitude(0, 512) <= ceiling * 1.001f,
              "audio driven past the ceiling comes out under it");

        std::cout << "  processes audio and holds its ceiling" << std::endl;
    }

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "ALL MASTER PLUGIN TESTS PASSED" << std::endl;
    else
        std::cout << failures << " MASTER PLUGIN TEST(S) FAILED" << std::endl;

    return failures == 0 ? 0 : 1;
}
