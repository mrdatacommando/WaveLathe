// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MasterPluginProcessor.h"
#include "MasterPluginEditor.h"

namespace wavelathe
{
const char* MasterPluginProcessor::parameterIdFor(mastering::Control control)
{
    using C = mastering::Control;

    switch (control)
    {
        case C::enabled:       return "enabled";
        case C::satDrive:      return "satDrive";
        case C::satMix:        return "satMix";
        case C::compThreshold: return "compThreshold";
        case C::compRatio:     return "compRatio";
        case C::compAttack:    return "compAttack";
        case C::compRelease:   return "compRelease";
        case C::compMakeup:    return "compMakeup";
        case C::compMix:       return "compMix";
        case C::limitCeiling:  return "limitCeiling";
        case C::limitRelease:  return "limitRelease";
        case C::trim:          return "trim";
        case C::count:         break;
    }

    return "";
}

juce::AudioProcessorValueTreeState::ParameterLayout MasterPluginProcessor::buildParameterLayout()
{
    using C = mastering::Control;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto add = [&layout](C control, const juce::String& name, juce::NormalisableRange<float> range,
                         float defaultValue)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{parameterIdFor(control), 1}, name, range, defaultValue));
    };

    // On by default, which is the opposite of the synth's Master page and is
    // deliberate. There, off protects sixty-five factory presets that were
    // voiced without it. Here somebody has gone to the trouble of putting an
    // effect on a bus, and a plugin that does nothing until a second switch is
    // found reads as broken.
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{parameterIdFor(C::enabled), 1}, "Mastering", true));

    add(C::satDrive, "Sat Drive", {0.0f, 1.0f}, 0.0f);
    add(C::satMix, "Sat Mix", {0.0f, 1.0f}, 1.0f);

    add(C::compThreshold, "Threshold", {-48.0f, 0.0f}, 0.0f);
    add(C::compRatio, "Ratio", {1.0f, 20.0f, 0.0f, 0.5f}, 2.0f);
    add(C::compAttack, "Attack", {0.1f, 100.0f, 0.0f, 0.3f}, 10.0f);
    add(C::compRelease, "Release", {10.0f, 1000.0f, 0.0f, 0.35f}, 100.0f);
    add(C::compMakeup, "Makeup", {0.0f, 24.0f}, 0.0f);
    add(C::compMix, "Comp Mix", {0.0f, 1.0f}, 1.0f);

    add(C::limitCeiling, "Ceiling", {-12.0f, 0.0f}, -0.3f);
    add(C::limitRelease, "Limit Release", {1.0f, 500.0f, 0.0f, 0.35f}, 50.0f);

    add(C::trim, "Trim", {-24.0f, 24.0f}, 0.0f);

    return layout;
}

MasterPluginProcessor::MasterPluginProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "WaveLatheMaster", buildParameterLayout())
{
}

bool MasterPluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in = layouts.getMainInputChannelSet();

    // Mono or stereo, in matching the out. The chain is built for a fixed
    // channel count at prepare and cannot be rebuilt from the audio thread, so
    // a layout that changed shape underneath it is a layout to refuse here
    // rather than to discover later.
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return in == out;
}

void MasterPluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    chain.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());

    // Same reasoning as in the synth: a host that is not told drifts against
    // everything else on the timeline, silently. On a mix bus it is worse,
    // because the thing it drifts against is the rest of the song.
    setLatencySamples(chain.getLatencySamples());
}

double MasterPluginProcessor::getTailLengthSeconds() const
{
    const auto releaseMs = state.getRawParameterValue(parameterIdFor(mastering::Control::compRelease));
    const double compressorTail = releaseMs != nullptr ? (double) releaseMs->load() * 0.001 : 1.0;

    // The lookahead is already covered by the reported latency, so what is left
    // is how long the gain takes to come back. A tenth of a second on top, so
    // the answer is never quite zero even with everything wound down.
    return compressorTail + 0.1;
}

float MasterPluginProcessor::get(mastering::Control control) const
{
    if (const auto* value = state.getRawParameterValue(parameterIdFor(control)))
        return value->load();

    return 0.0f;
}

void MasterPluginProcessor::set(mastering::Control control, float value)
{
    auto* parameter = state.getParameter(parameterIdFor(control));
    if (parameter == nullptr)
        return;

    // Through the host rather than straight at the value. A DAW that is not
    // told a parameter moved keeps believing its own copy and writes it back
    // over this one at the next opportunity - which looks, from the outside,
    // exactly like a dial that will not stay where it is put.
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

void MasterPluginProcessor::updateChainParameters()
{
    using C = mastering::Control;

    chainParameters.bypass.store(get(C::enabled) > 0.5f ? 0.0f : 1.0f);

    chainParameters.saturationDrive.store(get(C::satDrive));
    chainParameters.saturationMix.store(get(C::satMix));

    chainParameters.compressorThresholdDb.store(get(C::compThreshold));
    chainParameters.compressorRatio.store(get(C::compRatio));
    chainParameters.compressorAttackMs.store(get(C::compAttack));
    chainParameters.compressorReleaseMs.store(get(C::compRelease));
    chainParameters.compressorMakeupDb.store(get(C::compMakeup));
    chainParameters.compressorMix.store(get(C::compMix));

    chainParameters.limiterCeilingDb.store(get(C::limitCeiling));
    chainParameters.limiterReleaseMs.store(get(C::limitRelease));

    chainParameters.outputGainDb.store(get(C::trim));
}

void MasterPluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Anything the host gave us beyond the input it filled. An effect with
    // matched buses has none, but clearing is what stops a stale block being
    // treated as signal if a host ever hands over a wider layout.
    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    updateChainParameters();
    chain.process(buffer, chainParameters);
}

juce::AudioProcessorEditor* MasterPluginProcessor::createEditor()
{
    return new MasterPluginEditor(*this);
}

void MasterPluginProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destination);
}

void MasterPluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace wavelathe

// The host's way in. Separate from the synth's, which has its own copy of this
// in PluginProcessor.cpp - two plugins in one build means two of these, and
// JUCE picks the right one per target from the module definitions.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new wavelathe::MasterPluginProcessor();
}
