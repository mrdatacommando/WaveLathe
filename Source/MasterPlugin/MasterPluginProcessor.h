// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Mastering/MasteringChain.h"
#include "Mastering/MasteringControls.h"

namespace wavelathe
{
// WaveLathe Master: the mastering chain as an effect plugin, on its own.
//
// The same library the synth's Master page drives, wrapped so it can sit on a
// mix bus and catch whatever is routed into it - other instruments, other
// people's plugins, a whole mix. That is the half the synth's own page cannot
// do, because a stage inside an instrument only ever hears that instrument.
//
// Everything of consequence lives in MasteringChain. This file is parameters,
// state and plumbing, which is the point: if it were more than that, the
// library boundary would not have been worth keeping.
class MasterPluginProcessor : public juce::AudioProcessor,
                              public mastering::Controls
{
public:
    MasterPluginProcessor();
    ~MasterPluginProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "WaveLathe Master"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    // A compressor with a 1000 ms release is still letting go a second after
    // the music stopped, and the limiter's lookahead holds a millisecond and a
    // half of signal that has not come out yet. Not long, but not zero, and a
    // host asked for zero would cut the last of a fade.
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    // ---- mastering::Controls -----------------------------------------------
    // Here the values ARE the host's parameters, which is the whole difference
    // between this product and the page inside the synth. There they live in
    // SynthParameters and travel with a preset, unautomatable because the
    // synth's registry has one slot left. Here every one of them is automatable
    // against a budget of sixty-four that nothing else is using.
    float get(mastering::Control control) const override;
    void set(mastering::Control control, float value) override;
    float compressorReductionDb() const override { return chain.getGainReductionDb(); }
    float limiterReductionDb() const override { return chain.getLimiterReductionDb(); }

    juce::AudioProcessorValueTreeState& getState() { return state; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout buildParameterLayout();

    // The parameter id for a control, which is also the id a host saves
    // automation against. Never changed once shipped.
    static const char* parameterIdFor(mastering::Control control);

    juce::AudioProcessorValueTreeState state;

    mastering::Chain chain;
    mastering::Parameters chainParameters;

    void updateChainParameters();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterPluginProcessor)
};
} // namespace wavelathe
